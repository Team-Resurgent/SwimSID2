# Prebuilt firmware

Ready-to-flash SwinSID Nano firmware built from [`src/SwinSID88.asm`](../src/SwinSID88.asm)
(the "Lazy Jones fix" variant, with the SwimSID2 fixes: PAL/NTSC pitch
correction, filter loudness/output rolloff, and `$D41B` register-read support).

| File | Video standard | Notes |
|------|----------------|-------|
| `SwinSID88-pal.elf` / `.hex`  | **PAL** (default)  | 985,248 Hz C64 clock |
| `SwinSID88-ntsc.elf` / `.hex` | **NTSC**           | 1,022,727 Hz C64 clock |

Pick the variant that matches your machine. The `.hex` is what most AVR
flashers/programmers expect; the `.elf` is the same firmware with symbols (also
what the emulator/player load).

These are ATmega88 images (~6 KB, well under the 8 KB limit). Rebuild them
yourself at any time with `make` from the repo root - see the main
[README](../README.md#how-to-build).
