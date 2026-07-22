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
         --> [PWM output OCR1AL/OCR1BL] --> out.wav (render) or speakers (play)
```

It also ships a **reference player** for A/B comparison:
[libsidplayfp](https://github.com/libsidplayfp/libsidplayfp) - a complete,
cycle-accurate C64 (real 6510 + CIA + VIC timing, with the reSIDfp SID), the
same engine `sidplayfp` uses. Unlike the firmware harness above, the reference
plays the tune *entirely itself*, so timing-sensitive tunes (CIA-driven
multispeed, sample players like Delta) sound correct. That makes it a "real
machine" ground truth to judge how close the firmware gets.

```
tune.sid --> [libsidplayfp: 6510 + CIA + VIC + reSIDfp] --> out.wav / speakers
```

The engine is built as a self-contained **`swinsid.dll`** exposing a small C API
([`src/swinsid.h`](src/swinsid.h)): `swinsid_render`, `swinsid_play` (firmware),
`swinsid_ref_render`, `swinsid_ref_play` (libsidplayfp reference), and
`swinsid_stop`. Both the `swinsid_sim.exe` CLI and the .NET player consume it;
the .NET player P/Invokes the DLL directly so playback starts and stops in-process.

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
       mingw-w64-ucrt-x86_64-libelf \
       mingw-w64-ucrt-x86_64-libsidplayfp
   ```

   `libsidplayfp` is the reference player; it is statically linked into
   `swinsid.dll`, so the finished DLL stays self-contained.

3. Fetch the simavr **submodule** and build it (skip the fetch if you cloned
   with `--recurse-submodules`):

   ```bash
   cd /path/to/swinsid

   git submodule update --init third_party/simavr
   ( cd third_party/simavr && make build-simavr )   # -> libsimavr.a
   ```

   miniaudio, used for real-time `--play`, is vendored as
   [`src/miniaudio.h`](src/miniaudio.h), so no download is needed.

## 2. Build the firmware (ELF the emulator loads)

From the repo root in the UCRT64 shell:

```bash
make                        # builds build/SwinSID88-{pal,ntsc}.elf + .hex
```

This assembles the source with `avr-gcc`/`avr-ld`. The emulator uses the `.elf`
(it carries the `.mmcu` section that tells simavr the MCU is an ATmega88 @
32 MHz).

Firmware built (all output lands in `build/`), both with `LAZY_JONES_FIX`
(CodeKiller's Lazy Jones fix):

| ELF | Extra define | Description |
| --- | --- | --- |
| `build/SwinSID88-pal.elf`  | *(none)*        | PAL timing (985248 Hz) - the default |
| `build/SwinSID88-ntsc.elf` | `SWINSID_NTSC`  | NTSC timing (1022727 Hz) |

## 3. Build the harness

```bash
cd sim
make            # -> ../tools/swinsid.dll, swinsid_sim.exe, wavstat.exe
```

The harness sources live in `sim/src/`, objects and the import library build into
`sim/build/`, and the finished DLL + executables are placed in the committed
`tools/` folder at the repo root (so you can run them without rebuilding). The
GCC/pthread/elf runtimes are statically linked, so `swinsid.dll` depends only on
system DLLs and runs outside an MSYS2 shell.

## 4. Render / play a tune

Put your `.sid` files in the repo-root `tunes/` folder (git-ignored - it holds
HVSC content). Run from the repo root:

```bash
# Render 30 s of a tune to a WAV (no audio device touched)
tools/swinsid_sim.exe build/SwinSID88-pal.elf tunes/Commando.sid out.wav --seconds 30

# Play the whole tune live through the speakers (streams in real time; Ctrl-C to stop)
tools/swinsid_sim.exe build/SwinSID88-pal.elf tunes/Commando.sid --play

# Same tune through the libsidplayfp reference (no firmware ELF needed) for comparison
tools/swinsid_sim.exe --reference tunes/Commando.sid ref.wav --seconds 30
tools/swinsid_sim.exe --reference tunes/Commando.sid --play
```

Render writes a WAV of `--seconds`; `--play` streams the **whole tune** straight
to the default audio device until you stop it (no WAV is written, so the output
path is optional with `--play`). With `--reference` the firmware ELF is not used,
so the positional arguments are just `<tune.sid> [out.wav]`.

Options:

| Option | Meaning |
| --- | --- |
| `--song N`    | select sub-song N (1-based) |
| `--seconds S` | render length in seconds (CLI default 180; ignored for `--play`) |
| `--rate R`    | output sample rate (default 44100) |
| `--6581` / `--8580` | select filter mode (6581 vs 8580 chip model; default 8580) |
| `--pal` / `--ntsc` | C64 clock for firmware timing and the reference (default PAL; match how the firmware was built) |
| `--voice N`   | solo a single SID channel (1-3) for A/B channel comparison; 0 = full mix. Muted voices keep their oscillators running (so hard-sync / ring-mod still drive the soloed voice) but their gate is forced off |
| `--match-level` | scale the firmware down to reSIDfp's line level (~x0.44) so an A/B is loudness-matched; the firmware runs a fixed ~2.3x hotter otherwise. No effect on `--reference` |
| `--play`      | stream the whole tune live to the audio device instead of writing a WAV |
| `--reference` (`--ref`) | drive the libsidplayfp reference player instead of the SwinSID firmware |

`tools/wavstat.exe` (built by `make` in `sim/`, from `sim/src/wavstat.c`) prints
min/max/RMS of a WAV for quick sanity checks.

## 5. The improve loop (how to work on a fix)

The whole point is a fast edit -> assemble -> render -> compare cycle:

1. Edit the assembly in [`../src/SwinSID88.asm`](../src/SwinSID88.asm)
   (guard experimental changes behind a `#define`, exactly like `LAZY_JONES_FIX`).
2. Re-assemble: `( cd .. && make elf )`.
3. Re-render the same tune with the old and new ELF to two WAVs.
4. Compare by ear (`--play`) or numerically (`tools/wavstat.exe`, or diff the WAVs).

Use `--reference` as a "known good" target: render the same tune through
libsidplayfp and compare the firmware output against it to judge how close the
firmware gets to a real C64.

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
sample centred at `0x8000` (approximating the hardware resistor DAC) in
`fw_emit_sample()` in [`src/swinsid_core.cpp`](src/swinsid_core.cpp). Render mode
writes each sample to the WAV; play mode pushes it into a ring buffer that
[`src/realtime.c`](src/realtime.c) streams to the audio device, pacing the
(faster-than-real-time) simulation to real time.

## The reference (libsidplayfp) player

The reference path in `swinsid_core.cpp` does **not** use the firmware harness at
all. It hands the `.sid` straight to [libsidplayfp](https://github.com/libsidplayfp/libsidplayfp)
(`SidTune` + `sidplayfp` + `ReSIDfpBuilder`), which runs a complete, cycle-accurate
C64 - real 6510 CPU, CIA timers and VIC raster - driving the reSIDfp SID, then
pulls mono samples via `sidplayfp::play()` into the same WAV/stream output path.
No C64 ROMs are loaded, which is fine for the PSID tunes we test.

This is why the reference is trustworthy where the firmware harness is not: tunes
whose playback depends on accurate CIA-timer/raster behaviour (Rob Hubbard's
Delta, multispeed tunes, sample players) are timed correctly by libsidplayfp,
whereas the firmware harness only approximates those timers.

libsidplayfp (GPL-2.0-or-later) is statically linked into `swinsid.dll` (along
with libstdc++ and libgcrypt), so the finished DLL stays self-contained. It comes
from the MSYS2 package `mingw-w64-ucrt-x86_64-libsidplayfp`.

## Limitations

* The embedded 6502 implements all official opcodes plus the common
  undocumented ones (SLO/RLA/SRE/RRA/DCP/ISC/LAX/SAX); rarer illegal opcodes
  run as NOPs. Most HVSC tunes work; a few that rely on exotic illegals may not.
* PSID tunes with `playAddress = 0` (IRQ-driven) are handled best-effort by
  reading the IRQ vector at `$0314` after init; plain `playAddress` tunes are
  the well-supported case.
* The PWM->analog resistor-network weighting is approximated; tweak the
  reconstruction in `emit_sample()` if matching a specific reference recording.
