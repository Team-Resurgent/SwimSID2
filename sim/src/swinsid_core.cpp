/*
 * swinsid_core.cpp - SwinSID emulator engine (see swinsid.h).
 *
 * Two backends share one 6502 + PSID player and one C64 address space:
 *
 *   firmware  - the assembled SwinSID AVR firmware inside simavr. Each SID
 *               register write is injected onto the emulated C64 bus (PORTC/
 *               PORTD + INT0 chip-select) and the firmware's PWM output
 *               (OCR1AL/OCR1BL) is captured. This is what we are testing.
 *
 *   reference - the same register-write stream fed to the reSIDfp cycle-exact
 *               SID emulation, our "real SID chip" ground truth for A/B tests.
 *
 * Both drive the identical player/register-write timeline (the write sink is a
 * function pointer swapped per backend), so a difference between them is a
 * difference in the SID emulation, not the CPU/player.
 *
 * Firmware bus mapping (from the firmware source comments):
 *   PORTC: PC0-4 = A0-A4, PC5 = D2, PC6 = reset
 *   PORTD: PD0,PD1,PD3-7 = D0,D1,D3-7 ; PD2 = CS (INT0)
 *   PORTB: PB5 = RW (0 = write), PB0 = 6581/8580 select (high = 8580)
 */
#define SWINSID_BUILD
#include "swinsid.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <cstdint>

extern "C" {
#include "sim_avr.h"
#include "sim_elf.h"
#include "avr_ioport.h"

#include "cpu6502.h"
#include "sidfile.h"
#include "wav.h"
#include "realtime.h"
}

#include <sidplayfp/sidplayfp.h>
#include <sidplayfp/SidTune.h>
#include <sidplayfp/SidTuneInfo.h>
#include <sidplayfp/SidInfo.h>
#include <sidplayfp/SidConfig.h>
#include <sidplayfp/builders/residfp.h>

/* ATmega88 Timer1 PWM compare registers in data space. */
#define REG_OCR1AL 0x88   /* coarse audio byte (sample_h -> PB1) */
#define REG_OCR1BL 0x8a   /* fine   audio byte (sample_l -> PB2) */

/* ATmega88 port output registers in data space (used to read back the byte the
 * firmware drives onto the data bus during a register read). */
#define REG_PORTC  0x28
#define REG_PORTD  0x2b

#define C64_PAL_HZ      985248.0
#define C64_PAL_FRAME   19656.0     /* cycles per 50.125 Hz frame  */
#define C64_NTSC_HZ     1022727.0
#define C64_NTSC_FRAME  17095.0     /* cycles per 59.826 Hz frame  */
#define MAXPLAY_INSTR   2000000     /* runaway guard for init/play */

/* opt->region: 0 = PAL (default), 1 = NTSC. */
static inline int   region_is_ntsc(const swinsid_options *o) { return o && o->region == 1; }
static inline double region_hz(const swinsid_options *o)    { return region_is_ntsc(o) ? C64_NTSC_HZ    : C64_PAL_HZ; }
static inline double region_frame(const swinsid_options *o) { return region_is_ntsc(o) ? C64_NTSC_FRAME : C64_PAL_FRAME; }
static inline const char *region_name(const swinsid_options *o) { return region_is_ntsc(o) ? "NTSC" : "PAL"; }

/* ---------------------------------------------------------- shared session */
static uint8_t    g_mem[65536];      /* C64 address space for the 6502 */

/* Register-write sink, swapped per backend (firmware vs reference). */
static void (*g_sid_sink)(uint8_t reg, uint8_t val);

/* Voice solo for A/B diagnostics: 0 = full mix; 1/2/3 = only that voice is
 * allowed to reach the firmware (the other two voices' register writes are
 * dropped, so their gate never opens and they stay silent). */
static int g_voice_solo = 0;

/* Which SID voice (0..2) owns register 'reg', or -1 for the global regs. */
static inline int reg_voice(uint8_t reg) {
    if (reg <= 0x06) return 0;   /* voice 1: freq/pw/ctrl/ad/sr */
    if (reg <= 0x0d) return 1;   /* voice 2 */
    if (reg <= 0x14) return 2;   /* voice 3 */
    return -1;                   /* filter cutoff/res/vol, OSC3/ENV3 reads */
}

/* Register-read source: performs a real read bus-cycle against the firmware so
 * reads of $D41B (OSC3) etc. return what the firmware drives. Null (default)
 * means "no readable SID" and the 6502 falls back to the RAM shadow. */
static uint8_t (*g_sid_read_src)(uint8_t reg);

/* Frame timing shared bookkeeping. */
static uint64_t   g_cpu_at_frame;    /* cpu_clockticks at start of current frame */

/* Audio capture / output. */
static wav_writer_t g_wav;
static uint64_t   g_samples_emitted;
static uint64_t   g_total_samples;   /* 0 => no limit (play until stopped) */
static int        g_done;

/* Mode + stop flag. */
static int              g_play;      /* 1 = live playback, 0 = WAV render */
static realtime_stream *g_stream;
static volatile int     g_stop;      /* set from any thread by swinsid_stop() */

/* Logging sink. */
static swinsid_log_fn g_log;
static void          *g_log_user;

static void emit_log(const char *fmt, ...) {
    if (!g_log)
        return;
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    g_log(buf, g_log_user);
}

/* Output gain (level-match): the SwinSID firmware runs a fixed ~2.3x hotter
 * than reSIDfp's line-level convention. When level-matching is on we scale the
 * firmware down to the reference's level (never amplifying, so no clipping) so
 * current/original/reference are directly A/B comparable by ear. 1.0 = as-is. */
static double g_out_gain = 1.0;
#define SWINSID_MATCH_GAIN 0.44   /* firmware -> reference nominal level */

/* One output sample (shared by both backends). */
static inline void output_sample(int16_t v) {
    if (g_out_gain != 1.0) {
        double s = (double)v * g_out_gain;
        if (s > 32767.0) s = 32767.0; else if (s < -32768.0) s = -32768.0;
        v = (int16_t)s;
    }
    if (g_play) {
        realtime_stream_push(g_stream, v);   /* live, paces to real time */
    } else {
        wav_write(&g_wav, v);
        g_wav.nframes++;
    }
    /* g_total_samples == 0 means "no limit". */
    ++g_samples_emitted;
    if (g_total_samples && g_samples_emitted >= g_total_samples) g_done = 1;
}

/* ---------------------------------------------------- 6502 memory callbacks */
extern "C" uint8_t read6502(uint16_t a) {
    switch (a) {
    case 0xd011: return 0x1b;                                   /* screen ctrl default */
    case 0xd012: return (uint8_t)((cpu_clockticks / 63) % 312); /* rasterline (approx) */
    case 0xdc04: return (uint8_t)(cpu_clockticks);              /* CIA1 timer A lo */
    case 0xdc05: return (uint8_t)(cpu_clockticks >> 8);         /* CIA1 timer A hi */
    case 0xdc06: return (uint8_t)(cpu_clockticks);              /* CIA1 timer B lo */
    case 0xdc07: return (uint8_t)(cpu_clockticks >> 8);         /* CIA1 timer B hi */
    default:
        if (a >= 0xd400 && a <= 0xd7ff && g_sid_read_src)      /* SID + mirrors */
            return g_sid_read_src((uint8_t)(a & 0x1f));
        return g_mem[a];
    }
}

extern "C" void write6502(uint16_t a, uint8_t val) {
    g_mem[a] = val;
    if (a >= 0xd400 && a <= 0xd7ff && g_sid_sink) { /* SID + mirrors */
        uint8_t reg = (uint8_t)(a & 0x1f);
        if (g_voice_solo) {
            int v = reg_voice(reg);
            if (v >= 0 && v != g_voice_solo - 1) {
                /* Mute this voice but keep its oscillator running so hard-sync
                 * and ring-mod sources for the soloed voice still work (matching
                 * reSIDfp's mute, which only silences output). All writes pass
                 * through - freq/pulse/waveform drive the oscillator - except we
                 * force the GATE bit (ctrl bit 0) off so the envelope stays at
                 * zero and the voice makes no sound of its own. */
                if (reg == 0x04 || reg == 0x0b || reg == 0x12)
                    val &= (uint8_t)~0x01;
            }
        }
        g_sid_sink(reg, val);
    }
}

/* ===================================================== FIRMWARE (simavr) === */
static avr_t     *g_avr;
static avr_irq_t *g_pc[7];           /* PORTC pins 0..6 */
static avr_irq_t *g_pd[8];           /* PORTD pins 0..7 */
static avr_irq_t *g_pb[8];           /* PORTB pins 0..7 */

static double     g_ratio;           /* AVR cycles per C64 cycle */
static uint64_t   g_frame_avr_base;  /* AVR cycle at start of current frame */
static double     g_next_sample_avr;
static double     g_cycles_per_sample;

static inline void set_pin(avr_irq_t *irq, int bit) {
    avr_raise_irq(irq, bit ? 1 : 0);
}

static void fw_emit_sample(void) {
    uint8_t hi = g_avr->data[REG_OCR1AL];
    uint8_t lo = g_avr->data[REG_OCR1BL];
    /* Reconstruct the two-channel PWM DAC: PB1 is the coarse byte, PB2 the
     * fine byte (~1/256 weight). Output is centred at 0x8000. */
    int32_t v = ((int32_t)hi << 8) | lo;
    v -= 0x8000;
    if (v > 32767) v = 32767;
    if (v < -32768) v = -32768;
    output_sample((int16_t)v);
}

/* Run the AVR until it reaches 'target' cycle, emitting audio along the way. */
static void advance_avr_to(uint64_t target) {
    while (!g_done && !g_stop && g_avr->cycle < target) {
        int st = avr_run(g_avr);
        while (g_avr->cycle >= (uint64_t)g_next_sample_avr) {
            fw_emit_sample();
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
static void fw_sid_write(uint8_t reg, uint8_t val) {
    /* Advance the AVR to the moment of this write (sub-frame accurate). */
    uint64_t target = g_frame_avr_base +
        (uint64_t)((double)(cpu_clockticks - g_cpu_at_frame) * g_ratio);
    advance_avr_to(target);
    if (g_done || g_stop) return;

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

    set_pin(g_pb[5], 0);            /* RW low = write */

    set_pin(g_pd[2], 1);            /* chip-select falling edge on PD2/INT0 */
    set_pin(g_pd[2], 0);
}

/* Advance the AVR by 'cycles', still emitting audio so playback timing holds. */
static void run_avr_cycles(uint64_t cycles) {
    uint64_t until = g_avr->cycle + cycles;
    while (g_avr->cycle < until && !g_done && !g_stop) {
        int st = avr_run(g_avr);
        while (g_avr->cycle >= (uint64_t)g_next_sample_avr) {
            fw_emit_sample();
            g_next_sample_avr += g_cycles_per_sample;
            if (g_done) break;
        }
        if (st == cpu_Done || st == cpu_Crashed) { g_done = 1; break; }
    }
}

/*
 * Perform one SID register READ bus-cycle against the firmware, mirroring what
 * a C64 does: put the address on A0-A4, drive R/W high, then assert /CS (INT0).
 * The firmware's read handler drives the data bus back on PORTD (+PC5 for D2);
 * we read that straight out of the AVR's PORT registers and reconstruct D0-D7.
 * Then we raise /CS so the firmware tri-states again.
 */
static uint8_t fw_sid_read(uint8_t reg) {
    uint64_t target = g_frame_avr_base +
        (uint64_t)((double)(cpu_clockticks - g_cpu_at_frame) * g_ratio);
    advance_avr_to(target);
    if (g_done || g_stop) return g_mem[0xd400 + reg];

    /* Address on A0-A4 (PC0-4), reset inactive (PC6). Leave PC5 (D2) alone so
     * it does not fight the firmware, which will drive it. */
    for (int i = 0; i < 5; i++) set_pin(g_pc[i], (reg >> i) & 1);
    set_pin(g_pc[6], 1);

    set_pin(g_pb[5], 1);            /* RW high = read */
    set_pin(g_pd[2], 1);            /* make sure /CS starts high ... */
    set_pin(g_pd[2], 0);            /* ... then falling edge triggers INT0 read */

    /* Let the read handler latch the address and drive the bus. */
    run_avr_cycles(48);

    /* Read back the driven byte from the AVR's own PORT registers (independent
     * of external nets): D0,D1,D3-D7 on PORTD; D2 on PC5. */
    uint8_t portd = g_avr->data[REG_PORTD];
    uint8_t portc = g_avr->data[REG_PORTC];
    uint8_t val = (uint8_t)(portd & 0xfb);          /* D0,D1,D3-D7 (mask out D2 slot) */
    val |= (uint8_t)(((portc >> 5) & 1) << 2);      /* D2 from PC5 */

    /* End the access: raise /CS so the firmware releases the bus. */
    set_pin(g_pd[2], 1);
    run_avr_cycles(16);
    set_pin(g_pb[5], 0);            /* back to write default for the next cycle */

    g_mem[0xd400 + reg] = val;      /* keep the RAM shadow coherent */
    return val;
}

/* -------------------------------------------------- shared per-call reset */
static void session_begin(int play, swinsid_log_fn log, void *log_user) {
    g_log             = log;
    g_log_user        = log_user;
    g_play            = play;
    g_stream          = nullptr;
    g_stop            = 0;
    g_done            = 0;
    g_samples_emitted = 0;
    g_sid_sink        = nullptr;
    g_sid_read_src    = nullptr;
    g_voice_solo      = 0;
    g_out_gain        = 1.0;
}

static int open_output(int play, const char *wav_path, uint32_t rate,
                       double seconds) {
    if (play) {
        g_stream = realtime_stream_open(rate);
        if (!g_stream) { emit_log("error: cannot open audio output device"); return 1; }
    } else if (wav_open(&g_wav, wav_path, rate, 1) != 0) {
        emit_log("error: cannot open output WAV: %s", wav_path);
        return 1;
    }
    /* Play is uncapped (runs until stopped); render is bounded by 'seconds'. */
    g_total_samples = play ? 0 : (uint64_t)(seconds * rate);
    return 0;
}

static void close_output(int play, const char *wav_path, uint32_t rate,
                         double seconds, const char *what) {
    int stopped = g_stop && !g_done;
    if (play) {
        realtime_stream_close(g_stream);
        g_stream = nullptr;
        double elapsed = (double)g_samples_emitted / (double)rate;
        emit_log(stopped ? "%s stopped (%llu samples, %.2f s)."
                         : "%s finished (%llu samples, %.2f s).",
                 what, (unsigned long long)g_samples_emitted, elapsed);
    } else {
        wav_close(&g_wav);
        emit_log("Wrote %s : %.2f s, %u Hz mono (%llu samples)%s",
                 wav_path, seconds, rate, (unsigned long long)g_samples_emitted,
                 stopped ? " [stopped early]" : "");
    }
}

/* Load the tune, resolve the sub-song, and print the header. Returns song #. */
static int load_tune(const char *sid_path, const swinsid_options *opt,
                     sid_info_t *sid, uint16_t *song_out) {
    if (sid_load(sid_path, g_mem, sid) != 0) {
        emit_log("error: cannot load SID file: %s", sid_path);
        return 1;
    }
    uint16_t song = opt->song ? (uint16_t)opt->song : sid->start_song;
    if (song < 1) song = 1;
    if (song > sid->songs) song = sid->songs ? sid->songs : 1;
    *song_out = song;

    emit_log("Tune : %s", sid->name);
    emit_log("By   : %s (%s)", sid->author, sid->released);
    emit_log("Type : %s v%u   songs=%u  playing song %u",
             sid->magic, sid->version, sid->songs, song);
    emit_log("Addr : load=$%04X init=$%04X play=$%04X",
             sid->load_addr, sid->init_addr, sid->play_addr);
    return 0;
}

static uint16_t resolve_play_addr(const sid_info_t *sid) {
    uint16_t play_addr = sid->play_addr;
    if (play_addr == 0) {            /* IRQ-driven tune: grab vector at $0314 */
        play_addr = (uint16_t)(g_mem[0x0314] | (g_mem[0x0315] << 8));
        emit_log("play=0 in header; using IRQ vector $%04X", play_addr);
    }
    return play_addr;
}

static void player_init(uint16_t song) {
    cpu_sp     = 0xff;
    cpu_status = 0x24;               /* U + I set */
    cpu_a      = (uint8_t)(song - 1);
    cpu_x      = 0;
    cpu_y      = 0;
    g_cpu_at_frame = cpu_clockticks;
}

/* =========================================================== FIRMWARE run == */
static int run_firmware(const char *elf_path, const char *sid_path,
                        const char *wav_path, const swinsid_options *opt,
                        int play, swinsid_log_fn log, void *log_user) {
    swinsid_options def;
    if (!opt) { swinsid_default_options(&def); opt = &def; }
    session_begin(play, log, log_user);

    uint32_t rate    = opt->rate ? opt->rate : 44100;
    double   seconds = opt->seconds > 0 ? opt->seconds : 30.0;
    int      mode    = opt->filter8580 ? 1 : 0;   /* PB0 high = 8580 */

    sid_info_t sid;
    uint16_t song;
    if (load_tune(sid_path, opt, &sid, &song) != 0) return 1;

    /* --- Set up simavr with the firmware ELF. --- */
    elf_firmware_t fw;
    memset(&fw, 0, sizeof fw);
    if (elf_read_firmware(elf_path, &fw) != 0) {
        emit_log("error: failed to read firmware ELF: %s", elf_path);
        return 1;
    }
    if (fw.mmcu[0] == 0) strcpy(fw.mmcu, "atmega88");

    g_avr = avr_make_mcu_by_name(fw.mmcu);
    if (!g_avr) { emit_log("error: unknown MCU: %s", fw.mmcu); return 1; }
    avr_init(g_avr);
    avr_load_firmware(g_avr, &fw);
    g_avr->options.no_pullups = 1;   /* we drive every input pin ourselves */

    if (g_avr->frequency == 0) g_avr->frequency = 32000000;
    emit_log("MCU  : %s @ %u Hz   filter mode=%s   region=%s",
             fw.mmcu, g_avr->frequency, mode ? "8580" : "6581", region_name(opt));

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
    set_pin(g_pb[0], mode);          /* 6581/8580 select */

    /* Warm up: let the firmware run its reset/init and start the timer. */
    uint64_t warm_target = g_avr->cycle + (uint64_t)(g_avr->frequency * 0.02);
    while (!g_stop && g_avr->cycle < warm_target) {
        if (avr_run(g_avr) == cpu_Crashed) {
            emit_log("error: firmware crashed during warmup");
            avr_terminate(g_avr); g_avr = nullptr;
            return 1;
        }
    }

    if (open_output(play, wav_path, rate, seconds) != 0) {
        avr_terminate(g_avr); g_avr = nullptr;
        return 1;
    }
    if (play) emit_log("Playing the whole tune (firmware) live at %u Hz (stop to end)...", rate);

    g_ratio             = (double)g_avr->frequency / region_hz(opt);
    g_cycles_per_sample = (double)g_avr->frequency / (double)rate;
    g_next_sample_avr   = (double)g_avr->cycle;

    g_sid_sink     = fw_sid_write;
    g_sid_read_src = fw_sid_read;   /* answer $D41B (OSC3) & co. from the firmware */
    g_voice_solo   = (opt->voice >= 1 && opt->voice <= 3) ? opt->voice : 0;
    if (g_voice_solo) emit_log("Solo: voice %d only (others muted)", g_voice_solo);
    if (opt->match_level) {
        g_out_gain = SWINSID_MATCH_GAIN;   /* bring firmware down to reSIDfp level */
        emit_log("Level-match: firmware output x%.2f (matched to reference)", g_out_gain);
    }

    /* Run the tune's init routine (song select in A). */
    player_init(song);
    g_frame_avr_base = g_avr->cycle;
    cpu_call(sid.init_addr, MAXPLAY_INSTR);

    uint16_t play_addr = resolve_play_addr(&sid);

    /* Frames start now (after init). */
    g_frame_avr_base = g_avr->cycle;
    uint64_t avr_per_frame = (uint64_t)(region_frame(opt) * g_ratio);

    while (!g_done && !g_stop) {
        g_cpu_at_frame = cpu_clockticks;
        cpu_call(play_addr, MAXPLAY_INSTR);
        uint64_t next = g_frame_avr_base + avr_per_frame;
        advance_avr_to(next);
        g_frame_avr_base = next;
    }

    g_sid_sink     = nullptr;
    g_sid_read_src = nullptr;
    int stopped = g_stop && !g_done;
    close_output(play, wav_path, rate, seconds, "Playback");
    avr_terminate(g_avr);
    g_avr = nullptr;
    return stopped ? 2 : 0;
}

/* ========================================================== REFERENCE run == */
/*
 * The reference uses libsidplayfp as a *complete* C64 (accurate 6510 + CIA +
 * VIC timing) driving reSIDfp - i.e. it plays the tune itself rather than
 * reusing our simplified 6502/PSID harness. This is the "real machine" ground
 * truth (the same engine sidplayfp/VLC use) to compare the firmware against.
 * No C64 ROMs are loaded, which is fine for the PSID tunes we test.
 */
static const char *info_str(const SidTuneInfo *ti, unsigned i) {
    return (ti && i < ti->numberOfInfoStrings() && ti->infoString(i))
        ? ti->infoString(i) : "";
}

static int run_reference(const char *sid_path, const char *wav_path,
                         const swinsid_options *opt, int play,
                         swinsid_log_fn log, void *log_user) {
    swinsid_options def;
    if (!opt) { swinsid_default_options(&def); opt = &def; }
    session_begin(play, log, log_user);

    uint32_t rate    = opt->rate ? opt->rate : 44100;
    double   seconds = opt->seconds > 0 ? opt->seconds : 30.0;
    int      mode    = opt->filter8580 ? 1 : 0;

    SidTune tune(sid_path);
    if (!tune.getStatus()) {
        emit_log("error: cannot load SID file: %s (%s)", sid_path, tune.statusString());
        return 1;
    }
    tune.selectSong(opt->song > 0 ? (unsigned)opt->song : 0);  /* 0 = default */
    const SidTuneInfo *ti = tune.getInfo();

    emit_log("Tune : %s", info_str(ti, 0));
    emit_log("By   : %s (%s)", info_str(ti, 1), info_str(ti, 2));
    emit_log("Type : %s   songs=%u  playing song %u",
             ti->formatString(), ti->songs(), ti->currentSong());
    emit_log("Addr : load=$%04X init=$%04X play=$%04X",
             ti->loadAddr(), ti->initAddr(), ti->playAddr());

    sidplayfp engine;
    ReSIDfpBuilder builder("reSIDfp");
    builder.create(engine.info().maxsids());
    if (!builder.getStatus()) {
        emit_log("error: reSIDfp builder: %s", builder.error());
        return 1;
    }

    SidConfig cfg = engine.config();
    cfg.frequency       = rate;
    cfg.playback        = SidConfig::MONO;
    cfg.samplingMethod  = SidConfig::RESAMPLE_INTERPOLATE;
    cfg.sidEmulation    = &builder;
    /* Honour the tune's own SID model when it specifies one; the --6581/--8580
     * option only decides the model for tunes that leave it unspecified. */
    cfg.defaultSidModel = mode ? SidConfig::MOS8580 : SidConfig::MOS6581;
    cfg.forceSidModel   = false;
    /* Match the firmware's region so an A/B pitch comparison is fair: force the
     * C64 clock to PAL or NTSC rather than following the tune's own header. */
    cfg.defaultC64Model = region_is_ntsc(opt) ? SidConfig::NTSC : SidConfig::PAL;
    cfg.forceC64Model   = true;
    if (!engine.config(cfg)) {
        emit_log("error: engine config: %s", engine.error());
        return 1;
    }
    if (!engine.load(&tune)) {
        emit_log("error: engine load: %s", engine.error());
        return 1;
    }
    engine.filter(0, true);

    /* Voice solo (A/B diagnostics): mute the two voices we are not soloing so
     * the reference plays the same single channel as the firmware solo does. */
    int solo = (opt->voice >= 1 && opt->voice <= 3) ? opt->voice : 0;
    if (solo) {
        /* mute(): enable=true UNmutes, false mutes. Unmute only the soloed one. */
        for (unsigned v = 0; v < 3; v++)
            engine.mute(0, v, v == (unsigned)(solo - 1));
        emit_log("Solo: voice %d only (others muted)", solo);
    }

    /* Report the SID model actually in use. We keep forceSidModel off so the
     * tune's own model wins (correct sound); --6581/--8580 only decides it for
     * tunes that don't specify one. */
    const SidInfo &si = engine.info();
    const char *model = mode ? "8580" : "6581";
    if (si.numberOfSIDs() > 0) {
        switch (si.sidModel(0)) {
        case SidTuneInfo::SIDMODEL_6581: model = "6581"; break;
        case SidTuneInfo::SIDMODEL_8580: model = "8580"; break;
        default: break;   /* unknown/any -> our default (already set) */
        }
    }
    emit_log("Reference: libsidplayfp %s (reSIDfp, %s, %s) @ %u Hz",
             si.version(), model, region_name(opt), rate);

    if (open_output(play, wav_path, rate, seconds) != 0) return 1;
    if (play) emit_log("Playing the whole tune (libsidplayfp) live at %u Hz (stop to end)...", rate);

    const uint_least32_t N = 4096;
    short buf[N];
    while (!g_done && !g_stop) {
        uint_least32_t n = engine.play(buf, N);
        for (uint_least32_t i = 0; i < n && !g_done; i++)
            output_sample(buf[i]);
        if (n < N) {                       /* short read => stopped or errored */
            if (!engine.isPlaying())
                emit_log("engine stopped: %s", engine.error());
            break;
        }
    }

    int stopped = g_stop && !g_done;
    close_output(play, wav_path, rate, seconds, "Reference playback");
    return stopped ? 2 : 0;
}

/* --------------------------------------------------------------- public API */
void swinsid_default_options(swinsid_options *opt) {
    if (!opt)
        return;
    opt->song       = 0;
    opt->seconds    = 30.0;
    opt->rate       = 44100;
    opt->filter8580 = 1;
    opt->region     = 0;   /* PAL */
    opt->voice      = 0;   /* full mix */
    opt->match_level = 0;  /* truthful output; the player opts in for A/B */
}

int swinsid_render(const char *elf_path, const char *sid_path,
                   const char *wav_path, const swinsid_options *opt,
                   swinsid_log_fn log, void *log_user) {
    return run_firmware(elf_path, sid_path, wav_path, opt, 0, log, log_user);
}

int swinsid_play(const char *elf_path, const char *sid_path,
                 const swinsid_options *opt,
                 swinsid_log_fn log, void *log_user) {
    return run_firmware(elf_path, sid_path, nullptr, opt, 1, log, log_user);
}

int swinsid_ref_render(const char *sid_path, const char *wav_path,
                       const swinsid_options *opt,
                       swinsid_log_fn log, void *log_user) {
    return run_reference(sid_path, wav_path, opt, 0, log, log_user);
}

int swinsid_ref_play(const char *sid_path, const swinsid_options *opt,
                     swinsid_log_fn log, void *log_user) {
    return run_reference(sid_path, nullptr, opt, 1, log, log_user);
}

void swinsid_stop(void) {
    g_stop = 1;
    realtime_stream_abort(g_stream);
}
