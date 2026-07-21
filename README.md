# SwimSID2

**SwimSID2** is a [Team-Resurgent](https://github.com/Team-Resurgent) project (by
**EqUiNoX**) that continues development of the SwinSID Nano firmware and adds a
complete PC-based emulation/rendering toolchain, so the firmware can be tested
and improved entirely in software (no C64 or hardware programmer required).

## Credit / upstream

SwimSID2 builds directly on the excellent SwinSID firmware **source
reconstruction by Daniël Mantione (dmantione)**:

> https://github.com/dmantione/swinsid

All of the AVR firmware source in this repository derives from that
reconstruction. Huge thanks to dmantione for reverse-engineering and documenting
the firmware and making it buildable again. Please refer to the upstream
repository for the original work and history.

What SwimSID2 adds on top of the reconstruction:

- The firmware sources are focused on the "Lazy Jones fix" variant and tidied
  into a clean `src/` / `build/` layout.
- A [simavr](https://github.com/buserror/simavr)-based emulator (in [`sim/`](sim/))
  that runs the assembled firmware against real `.sid` tunes and renders the
  audio to a WAV file or plays it live through the speakers.

## Background

The SwinSID was developed between 2005 and 2012 by Swinkels. In 2014 Codekiller
released the well known "Lazy Jones fix" firmware, which fixed the audio in the
game Lazy Jones.

Development of the SwinSID stalled and no source code was ever released, making
it difficult for the community to improve the firmware. Swinkels disappeared from
the scene and his
[SwinSID website](http://web.archive.org/web/20191212101114/http://www.swinkels.tvtom.pl/swinsid/)
is gone. dmantione reconstructed the firmware source so it can be assembled into
a byte-exact copy of the original, and SwimSID2 continues from there.

## How to build

The firmware builds with `avr-gcc`/`avr-ld`. You need cross AVR binutils and
avr-libc installed.

- Debian/Ubuntu: `apt-get install binutils-avr gcc-avr avr-libc`
- Red Hat/CentOS: `yum install avr-binutils avr-libc`
- (Open)SuSE: `zypper install cross-avr-binutils avr-libc`
- Windows: via MSYS2 UCRT64 - see [`sim/README.md`](sim/README.md) for setup.

No include path needs configuring. Clone with submodules (for the emulator's
simavr dependency), then build:

```bash
git clone --recurse-submodules https://github.com/Team-Resurgent/SwimSID2.git
cd SwimSID2
make            # -> build/SwinSID88.elf + build/SwinSID88.hex
```

The result is `build/SwinSID88.hex` - the "Lazy Jones fix" firmware.

## Emulate and improve

The [`sim/`](sim/) directory contains a PC emulator (built on
[simavr](https://github.com/buserror/simavr)) that runs this firmware against
real `.sid` tunes and renders the audio to a WAV file or plays it live. This
enables a fast edit -> assemble -> render -> compare loop for working on the
firmware. See [`sim/README.md`](sim/README.md).

## Credits

- Original SwinSID hardware and firmware - **Swinkels**
- "Lazy Jones fix" firmware - **Codekiller**
- Firmware source reconstruction - **Daniël Mantione (dmantione)**,
  https://github.com/dmantione/swinsid
- SwimSID2 continuation and emulation tooling - **Team-Resurgent (EqUiNoX)**
