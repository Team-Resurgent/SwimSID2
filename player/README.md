# SwimSID2 Player (.NET)

A small .NET tool that drives the [`sim/`](../sim) emulator engine
(`tools/swinsid.dll`) to list, **render**, and **play** SID tunes. It P/Invokes
the DLL directly, so playback runs in-process and **Stop** halts it immediately.

Each tune can be driven through either engine:

- **Firmware** (default) - the SwinSID firmware under simavr (what you're testing).
- **Reference** (`--reference`) - [libsidplayfp](https://github.com/libsidplayfp/libsidplayfp),
  a complete cycle-accurate C64 (6510/CIA/VIC + reSIDfp) - the "real machine"
  ground truth to compare the firmware against. This is the engine sidplayfp
  uses, so timing-sensitive tunes (e.g. Delta) play correctly.

- Run it **with arguments** and it behaves as a command-line tool
  ([System.CommandLine](https://learn.microsoft.com/dotnet/standard/commandline/)).
- Run it **with no arguments** and it opens a nice [Avalonia](https://avaloniaui.net/) GUI.

Renders are written to the repo-root `output/` folder as `output/<tune>.wav`
(the reference engine writes `output/<tune>.ref.wav`, so both sit side by side).

## Prerequisites

The player only orchestrates the engine, so first make sure the pieces it drives exist:

1. Build the firmware:  `make`  →  `build/SwinSID88.elf`
2. Build the engine:  `( cd sim && make )`  →  `tools/swinsid.dll`
   (needs `pacman -S mingw-w64-ucrt-x86_64-libsidplayfp` for the reference engine)
3. Put `.sid` files in the repo-root `tunes/` folder.

The prebuilt `tools/swinsid.dll` is committed, so you only need step 2 if you
change the engine.

`swinsid.dll` is statically linked against the GCC/pthread/elf runtimes, so it
depends only on system DLLs - no MSYS2 shell or `PATH` tweaks are needed to load
it. The player resolves it from `tools/swinsid.dll` via a native library resolver.

## Build

```bash
cd player
dotnet build
```

The build produces `player/bin/Debug/net10.0/swimsid.exe`.

## Command line

```bash
# List the tunes in tunes/
swimsid list

# Render 30 s of a tune to output/Commando.wav
swimsid render Commando

# Options: sub-song, duration, sample rate, filter mode
swimsid render Delta --song 12 --seconds 20 --rate 48000 --6581

# Play the whole tune live through the speakers (Ctrl-C to stop)
swimsid play Wizball

# Compare against the libsidplayfp reference player
swimsid render Commando --reference          # -> output/Commando.ref.wav
swimsid play Commando --reference
```

`<tune>` is either a name as shown by `list` (e.g. `Commando`) or a path to a
`.sid` file.

| Option | Meaning | Default |
| --- | --- | --- |
| `--song N`    | sub-song number (1-based) | 1 |
| `--seconds S` | render duration (ignored for `play`, which runs the whole tune) | 180 |
| `--rate R`    | output sample rate (Hz) | 44100 |
| `--6581`      | use 6581 filter mode | 8580 |
| `--reference` (`--ref`) | use the libsidplayfp reference player instead of the firmware | firmware |

Run `swimsid -h` (or `swimsid render -h`) for full help.

## GUI

```bash
swimsid          # or: dotnet run   (from the player/ folder)
```

Pick a tune from the list, adjust the options, then **Play** or **Render to WAV**.
Tick **Reference: libsidplayfp** to drive the reference player instead of the firmware.
**Play** streams live and starts almost immediately; **Stop** halts it at once
(it calls `swinsid_stop()` in the DLL). The log pane shows the engine output.

## Repo-root discovery

The tool locates the repo root by looking for `.git` / `tools/swinsid.dll`
walking up from the executable (and the working directory). Set the
`SWIMSID_ROOT` environment variable to override.
