# SwinSID emulator / SID renderer (Windows)

This directory contains a host-side test harness that runs the **assembled
SwinSID firmware** inside [simavr](https://github.com/buserror/simavr), plays a
`.sid` tune into it over an emulated C64 bus, and captures the firmware's PWM
audio output. It lets you emulate and improve the firmware (the way the
"Lazy Jones fix" was developed) entirely on an x86 Windows PC, with no hardware.

```
tune.sid --> [6502 + PSID player] --> SID register writes
         --> [emulated C64 bus: PORTC/PORTD + INT0 chip-select]
         --> [simavr running the ATmega88 firmware ELF]
         --> [PWM output OCR1AL/OCR1BL] --> out.wav  (and/or speakers)
```

## 1. One-time setup (MSYS2 UCRT64)

1. Install MSYS2 (e.g. `winget install MSYS2.MSYS2`).
2. Open the **MSYS2 UCRT64** shell (important: not the plain MSYS shell) and
   install the toolchain:

   ```bash
   pacman -Syu   # (run twice if it asks to close the terminal)
   pacman -S --needed git make diffutils \
       mingw-w64-ucrt-x86_64-gcc \
       mingw-w64-ucrt-x86_64-avr-binutils \
       mingw-w64-ucrt-x86_64-avr-gcc \
       mingw-w64-ucrt-x86_64-avr-libc \
       mingw-w64-ucrt-x86_64-libelf
   ```

3. Build simavr, the one external dependency. It is a git **submodule** under
   `third_party/simavr`, so fetch it (if you didn't already clone this repo with
   `--recurse-submodules`) and build `libsimavr.a`:

   ```bash
   cd /path/to/swinsid

   git submodule update --init third_party/simavr
   ( cd third_party/simavr && make build-simavr )
   ```

   (miniaudio, used for real-time `--play`, is vendored as
   [`src/miniaudio.h`](src/miniaudio.h), so no download is needed.)

## 2. Build the firmware (ELF the emulator loads)

From the repo root in the UCRT64 shell:

```bash
make                        # builds build/SwinSID88.elf + build/SwinSID88.hex
```

This assembles the source with `avr-gcc`/`avr-ld`. The emulator uses the `.elf`
(it carries the `.mmcu` section that tells simavr the MCU is an ATmega88 @
32 MHz).

Firmware built (all output lands in `build/`):

| ELF | Source define | Description |
| --- | --- | --- |
| `build/SwinSID88.elf` | `LAZY_JONES_FIX` (set in the Makefile) | CodeKiller's Lazy Jones fix (most common) |

## 3. Build the harness

```bash
cd sim
make            # -> ../tools/swinsid_sim.exe and ../tools/wavstat.exe
```

The harness sources live in `sim/src/`, objects build into `sim/build/`, and the
finished executables are placed in the committed `tools/` folder at the repo root
(so you can run them without rebuilding).

## 4. Render / play a tune

Put your `.sid` files in the repo-root `tunes/` folder (git-ignored - it holds
HVSC content). Run from the repo root:

```bash
# Render 30 s of a tune to a WAV
tools/swinsid_sim.exe build/SwinSID88.elf tunes/Commando.sid out.wav --seconds 30

# Play it live through the speakers as well
tools/swinsid_sim.exe build/SwinSID88.elf tunes/Commando.sid out.wav --play
```

Options:

| Option | Meaning |
| --- | --- |
| `--song N`    | select sub-song N (1-based) |
| `--seconds S` | length to render (default 10) |
| `--rate R`    | output sample rate (default 44100) |
| `--6581` / `--8580` | select filter mode (drives PB0; default 8580) |
| `--play`      | also play through the default audio device |

`tools/wavstat.exe` (built by `make` in `sim/`, from `sim/src/wavstat.c`) prints
min/max/RMS of a WAV for quick sanity checks.

## 5. The improve loop (how to work on a fix)

The whole point is a fast edit -> assemble -> render -> compare cycle:

1. Edit the assembly in [`../src/SwinSID88.asm`](../src/SwinSID88.asm)
   (guard experimental changes behind a `#define`, exactly like `LAZY_JONES_FIX`).
2. Re-assemble: `( cd .. && make elf )`.
3. Re-render the same tune with the old and new ELF to two WAVs.
4. Compare by ear (`--play`) or numerically (`tools/wavstat.exe`, or diff the WAVs).

For example, the Lazy Jones fix lives in the conditional blocks of
`irq_chipselect` and the ADSR gate handling inside the `gen_voice` macro (guarded
by `LAZY_JONES_FIX`, which the Makefile passes via `-DLAZY_JONES_FIX`). Dropping
that flag and re-rendering a tune lets you hear the original, unfixed behavior.

## How the bus bridge works

The firmware's chip-select ISR reads the C64 bus from the AVR ports:

* `PORTC`: PC0-4 = A0-A4, **PC5 = data bit 2**, PC6 = reset
* `PORTD`: PD0,PD1,PD3-7 = D0,D1,D3-7, **PD2 = CS (INT0)**
* `PORTB`: PB5 = R/W (0 = write), PB0 = 6581/8580 select

For each SID register write the harness sets those pins with
`avr_raise_irq(...)` (with `avr->options.no_pullups = 1` so the firmware's
reset-time internal pull-ups don't override the injected values) and pulses
PD2 low to trigger the INT0 chip-select interrupt. The AVR is advanced with
sub-frame cycle accuracy (C64 PAL clock scaled to the 32 MHz AVR clock) so
timing-sensitive behaviour (digis, the Lazy Jones timing bug) is reproduced.

Audio is reconstructed from the Timer1 PWM compare registers: `OCR1AL` is the
coarse byte (PB1) and `OCR1BL` the fine byte (PB2), combined into a 16-bit
sample centred at `0x8000` (approximating the hardware resistor DAC).

## Limitations

* The embedded 6502 implements all official opcodes plus the common
  undocumented ones (SLO/RLA/SRE/RRA/DCP/ISC/LAX/SAX); rarer illegal opcodes
  run as NOPs. Most HVSC tunes work; a few that rely on exotic illegals may not.
* PSID tunes with `playAddress = 0` (IRQ-driven) are handled best-effort by
  reading the IRQ vector at `$0314` after init; plain `playAddress` tunes are
  the well-supported case.
* The PWM->analog resistor-network weighting is approximated; tweak the
  reconstruction in `emit_sample()` if matching a specific reference recording.
