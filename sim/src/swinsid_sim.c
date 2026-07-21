/*
 * swinsid_sim.c - SwinSID firmware emulator / SID tune renderer.
 *
 * Loads the assembled SwinSID AVR firmware (ELF) into simavr, plays a .sid
 * tune through an emulated C64 6502 CPU, bridges every SID register write onto
 * the emulated C64 bus (PORTC/PORTD + INT0 chip-select) exactly as the real
 * hardware sees it, and captures the firmware's PWM audio output to a WAV file.
 *
 * Usage:
 *   swinsid_sim <firmware.elf> <tune.sid> <out.wav>
 *               [--song N] [--seconds S] [--rate R] [--6581] [--8580]
 *
 * Bus mapping (from the firmware source comments):
 *   PORTC: PC0-4 = A0-A4, PC5 = D2, PC6 = reset
 *   PORTD: PD0,PD1,PD3-7 = D0,D1,D3-7 ; PD2 = CS (INT0)
 *   PORTB: PB5 = RW (0 = write), PB0 = 6581/8580 select (high = 8580)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "sim_avr.h"
#include "sim_elf.h"
#include "avr_ioport.h"

#include "cpu6502.h"
#include "sidfile.h"
#include "wav.h"
#include "realtime.h"

/* ATmega88 Timer1 PWM compare registers in data space. */
#define REG_OCR1AL 0x88   /* coarse audio byte (sample_h -> PB1) */
#define REG_OCR1BL 0x8a   /* fine   audio byte (sample_l -> PB2) */

#define C64_PAL_HZ      985248.0
#define C64_PAL_FRAME   19656.0     /* cycles per 50.125 Hz frame */
#define MAXPLAY_INSTR   2000000     /* runaway guard for init/play */

/* ------------------------------------------------------------- global state */
static avr_t     *g_avr;
static uint8_t    g_mem[65536];      /* C64 address space for the 6502 */

/* Cached per-pin IO port IRQs. */
static avr_irq_t *g_pc[7];           /* PORTC pins 0..6 */
static avr_irq_t *g_pd[8];           /* PORTD pins 0..7 */
static avr_irq_t *g_pb[8];           /* PORTB pins 0..7 */

/* Timing bridge (C64 <-> AVR). */
static double     g_ratio;           /* AVR cycles per C64 cycle */
static uint64_t   g_frame_avr_base;  /* AVR cycle at start of current frame */
static uint64_t   g_cpu_at_frame;    /* cpu_clockticks at start of current frame */

/* Audio capture. */
static wav_writer_t g_wav;
static double     g_next_sample_avr;
static double     g_cycles_per_sample;
static uint64_t   g_samples_emitted;
static uint64_t   g_total_samples;
static int        g_done;

/* Optional in-memory PCM buffer for real-time playback (--play). */
static int        g_play;
static int16_t   *g_pcm;
static size_t     g_pcm_len;
static size_t     g_pcm_cap;

/* ---------------------------------------------------- 6502 memory callbacks */
uint8_t read6502(uint16_t a) {
    switch (a) {
    case 0xd011: return 0x1b;                                   /* screen ctrl default */
    case 0xd012: return (uint8_t)((cpu_clockticks / 63) % 312); /* rasterline (approx) */
    case 0xdc04: return (uint8_t)(cpu_clockticks);              /* CIA1 timer A lo */
    case 0xdc05: return (uint8_t)(cpu_clockticks >> 8);         /* CIA1 timer A hi */
    case 0xdc06: return (uint8_t)(cpu_clockticks);              /* CIA1 timer B lo */
    case 0xdc07: return (uint8_t)(cpu_clockticks >> 8);         /* CIA1 timer B hi */
    default:     return g_mem[a];
    }
}

static void sid_write(uint8_t reg, uint8_t val);   /* fwd decl */

void write6502(uint16_t a, uint8_t val) {
    g_mem[a] = val;
    if (a >= 0xd400 && a <= 0xd7ff)                 /* SID + mirrors */
        sid_write((uint8_t)(a & 0x1f), val);
}

/* ------------------------------------------------------------- pin driving */
static inline void set_pin(avr_irq_t *irq, int bit) {
    avr_raise_irq(irq, bit ? 1 : 0);
}

static void emit_sample(void) {
    uint8_t hi = g_avr->data[REG_OCR1AL];
    uint8_t lo = g_avr->data[REG_OCR1BL];
    /* Reconstruct the two-channel PWM DAC: PB1 is the coarse byte, PB2 the
     * fine byte (~1/256 weight). Output is centred at 0x8000. */
    int32_t v = ((int32_t)hi << 8) | lo;
    v -= 0x8000;
    if (v > 32767) v = 32767;
    if (v < -32768) v = -32768;
    wav_write(&g_wav, (int16_t)v);
    g_wav.nframes++;

    if (g_play) {
        if (g_pcm_len >= g_pcm_cap) {
            g_pcm_cap = g_pcm_cap ? g_pcm_cap * 2 : 65536;
            g_pcm = (int16_t *)realloc(g_pcm, g_pcm_cap * sizeof(int16_t));
        }
        if (g_pcm) g_pcm[g_pcm_len++] = (int16_t)v;
    }

    if (++g_samples_emitted >= g_total_samples) g_done = 1;
}

/* Run the AVR until it reaches 'target' cycle, emitting audio along the way. */
static void advance_avr_to(uint64_t target) {
    while (!g_done && g_avr->cycle < target) {
        int st = avr_run(g_avr);
        while (g_avr->cycle >= (uint64_t)g_next_sample_avr) {
            emit_sample();
            g_next_sample_avr += g_cycles_per_sample;
            if (g_done) break;
        }
        if (st == cpu_Done || st == cpu_Crashed) { g_done = 1; break; }
    }
}

/*
 * Inject one SID register write over the emulated bus. The chip-select line
 * (PD2/INT0) is pulsed low; the firmware's INT0 handler then latches the bus
 * the next time the AVR runs (during the next advance_avr_to). Pins are left
 * stable until the following write so the handler always reads valid data.
 */
static void sid_write(uint8_t reg, uint8_t val) {
    /* Advance the AVR to the moment of this write (sub-frame accurate). */
    uint64_t target = g_frame_avr_base +
        (uint64_t)((double)(cpu_clockticks - g_cpu_at_frame) * g_ratio);
    advance_avr_to(target);
    if (g_done) return;

    /* Address bus A0-A4 on PC0-4, data bit 2 on PC5, reset (PC6) inactive. */
    for (int i = 0; i < 5; i++) set_pin(g_pc[i], (reg >> i) & 1);
    set_pin(g_pc[5], (val >> 2) & 1);
    set_pin(g_pc[6], 1);

    /* Data bus D0,D1 on PD0,PD1 and D3-D7 on PD3-PD7 (PD2 is CS). */
    set_pin(g_pd[0], (val >> 0) & 1);
    set_pin(g_pd[1], (val >> 1) & 1);
    set_pin(g_pd[3], (val >> 3) & 1);
    set_pin(g_pd[4], (val >> 4) & 1);
    set_pin(g_pd[5], (val >> 5) & 1);
    set_pin(g_pd[6], (val >> 6) & 1);
    set_pin(g_pd[7], (val >> 7) & 1);

    /* RW low = write. */
    set_pin(g_pb[5], 0);

    /* Chip-select falling edge on PD2/INT0. */
    set_pin(g_pd[2], 1);
    set_pin(g_pd[2], 0);
}

/* --------------------------------------------------------------------- main */
static void usage(const char *p) {
    fprintf(stderr,
        "Usage: %s <firmware.elf> <tune.sid> <out.wav>\n"
        "         [--song N] [--seconds S] [--rate R] [--6581] [--8580] [--play]\n", p);
}

int main(int argc, char **argv) {
    if (argc < 4) { usage(argv[0]); return 1; }

    const char *elf_path = argv[1];
    const char *sid_path = argv[2];
    const char *wav_path = argv[3];

    int      opt_song    = 0;        /* 0 => use tune's start song */
    double   opt_seconds = 10.0;
    uint32_t opt_rate    = 44100;
    int      opt_mode    = 1;        /* 1 = 8580 (PB0 high), 0 = 6581 */

    for (int i = 4; i < argc; i++) {
        if (!strcmp(argv[i], "--song") && i + 1 < argc)         opt_song = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--seconds") && i + 1 < argc) opt_seconds = atof(argv[++i]);
        else if (!strcmp(argv[i], "--rate") && i + 1 < argc)    opt_rate = (uint32_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--6581"))                    opt_mode = 0;
        else if (!strcmp(argv[i], "--8580"))                    opt_mode = 1;
        else if (!strcmp(argv[i], "--play"))                    g_play = 1;
        else { fprintf(stderr, "unknown option: %s\n", argv[i]); usage(argv[0]); return 1; }
    }

    /* --- Load the SID tune into C64 memory. --- */
    sid_info_t sid;
    if (sid_load(sid_path, g_mem, &sid) != 0) return 1;

    uint16_t song = opt_song ? (uint16_t)opt_song : sid.start_song;
    if (song < 1) song = 1;
    if (song > sid.songs) song = sid.songs ? sid.songs : 1;

    printf("Tune : %s\n", sid.name);
    printf("By   : %s (%s)\n", sid.author, sid.released);
    printf("Type : %s v%u   songs=%u  playing song %u\n",
           sid.magic, sid.version, sid.songs, song);
    printf("Addr : load=$%04X init=$%04X play=$%04X\n",
           sid.load_addr, sid.init_addr, sid.play_addr);

    /* --- Set up simavr with the firmware ELF. --- */
    elf_firmware_t fw = {0};
    if (elf_read_firmware(elf_path, &fw) != 0) {
        fprintf(stderr, "failed to read firmware ELF: %s\n", elf_path);
        return 1;
    }
    if (fw.mmcu[0] == 0) strcpy(fw.mmcu, "atmega88");

    g_avr = avr_make_mcu_by_name(fw.mmcu);
    if (!g_avr) { fprintf(stderr, "unknown MCU: %s\n", fw.mmcu); return 1; }
    avr_init(g_avr);
    avr_load_firmware(g_avr, &fw);
    g_avr->options.no_pullups = 1;   /* we drive every input pin ourselves */

    if (g_avr->frequency == 0) g_avr->frequency = 32000000;
    printf("MCU  : %s @ %u Hz   filter mode=%s\n",
           fw.mmcu, g_avr->frequency, opt_mode ? "8580" : "6581");

    /* Cache per-pin IO IRQs. */
    for (int i = 0; i < 7; i++)
        g_pc[i] = avr_io_getirq(g_avr, AVR_IOCTL_IOPORT_GETIRQ('C'), i);
    for (int i = 0; i < 8; i++) {
        g_pd[i] = avr_io_getirq(g_avr, AVR_IOCTL_IOPORT_GETIRQ('D'), i);
        g_pb[i] = avr_io_getirq(g_avr, AVR_IOCTL_IOPORT_GETIRQ('B'), i);
    }

    /* Initial idle bus state. */
    for (int i = 0; i < 6; i++) set_pin(g_pc[i], 0);
    set_pin(g_pc[6], 1);              /* reset inactive */
    for (int i = 0; i < 8; i++) if (i != 2) set_pin(g_pd[i], 0);
    set_pin(g_pd[2], 1);             /* CS idle high */
    set_pin(g_pb[5], 0);             /* RW = write */
    set_pin(g_pb[0], opt_mode);      /* 6581/8580 select */

    /* --- Warm up: let the firmware run its reset/init and start the timer. --- */
    uint64_t warm_target = g_avr->cycle + (uint64_t)(g_avr->frequency * 0.02); /* 20 ms */
    while (g_avr->cycle < warm_target) {
        if (avr_run(g_avr) == cpu_Crashed) {
            fprintf(stderr, "firmware crashed during warmup\n");
            return 1;
        }
    }

    /* --- Open output and set up the timing/audio bridge. --- */
    if (wav_open(&g_wav, wav_path, opt_rate, 1) != 0) {
        fprintf(stderr, "cannot open output WAV: %s\n", wav_path);
        return 1;
    }
    g_ratio             = (double)g_avr->frequency / C64_PAL_HZ;
    g_cycles_per_sample = (double)g_avr->frequency / (double)opt_rate;
    g_next_sample_avr   = (double)g_avr->cycle;
    g_total_samples     = (uint64_t)(opt_seconds * opt_rate);
    g_samples_emitted   = 0;
    g_done              = 0;

    /* --- Run the tune's init routine (song select in A). --- */
    cpu_sp     = 0xff;
    cpu_status = 0x24;               /* U + I set */
    cpu_a      = (uint8_t)(song - 1);
    cpu_x      = 0;
    cpu_y      = 0;
    g_frame_avr_base = g_avr->cycle;
    g_cpu_at_frame   = cpu_clockticks;
    cpu_call(sid.init_addr, MAXPLAY_INSTR);

    uint16_t play = sid.play_addr;
    if (play == 0) {                 /* IRQ-driven tune: grab vector at $0314 */
        play = (uint16_t)(g_mem[0x0314] | (g_mem[0x0315] << 8));
        printf("play=0 in header; using IRQ vector $%04X\n", play);
    }

    /* Frames start now (after init). */
    g_frame_avr_base = g_avr->cycle;
    uint64_t avr_per_frame = (uint64_t)(C64_PAL_FRAME * g_ratio);

    /* --- Main 50 Hz playback loop. --- */
    while (!g_done) {
        g_cpu_at_frame = cpu_clockticks;
        cpu_call(play, MAXPLAY_INSTR);

        uint64_t next = g_frame_avr_base + avr_per_frame;
        advance_avr_to(next);
        g_frame_avr_base = next;
    }

    wav_close(&g_wav);
    printf("Wrote %s : %.2f s, %u Hz mono (%llu samples)\n",
           wav_path, opt_seconds, opt_rate, (unsigned long long)g_samples_emitted);

    if (g_play && g_pcm_len) {
        printf("Playing %llu frames through the speakers...\n",
               (unsigned long long)g_pcm_len);
        realtime_play(g_pcm, g_pcm_len, opt_rate);
    }
    free(g_pcm);
    return 0;
}
