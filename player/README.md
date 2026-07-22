# SwimSID2 Player (.NET)

A small .NET tool that drives the [`sim/`](../sim) emulator engine
(`tools/swinsid.dll`) to list, **render**, and **play** SID tunes. It P/Invokes
the DLL directly, so playback runs in-process and **Stop** halts it immediately.

Each tune can be driven through one of three engines (`--engine`):

- **current** (default) - the SwinSID firmware you are working on, freshly built
  from `src/` into `build/SwinSID88-pal.elf` / `-ntsc.elf`. The region (and so
  which of the two builds is loaded) is auto-detected from each tune's SID
  header, so its baked-in pitch matches; `--region Pal`/`Ntsc` forces it.
- **original** - the frozen, unmodified SwinSID firmware baseline
  (`tools/SwinSID88.original.elf`, committed). A/B this against **current** to
  hear exactly what your firmware changes did.
- **reference** - [libsidplayfp](https://github.com/libsidplayfp/libsidplayfp),
  a complete cycle-accurate C64 (6510/CIA/VIC + reSIDfp) - the "real machine"
  ground truth to compare the firmware against. This is the engine sidplayfp
  uses, so timing-sensitive tunes (e.g. Delta) play correctly.

The SwinSID firmware runs a fixed ~2.3x hotter than reSIDfp's line level, so by
default the player **level-matches** the firmware down to the reference (the
*Match level* checkbox, on by default; `--raw-level` on the CLI to disable) so
switching between current/original/reference is a fair A/B by ear. It only
scales the firmware engines - the reference is left untouched.

- Run it **with arguments** and it behaves as a command-line tool
  ([System.CommandLine](https://learn.microsoft.com/dotnet/standard/commandline/)).
- Run it **with no arguments** and it opens a nice [Avalonia](https://avaloniaui.net/) GUI.

Renders are written to the repo-root `output/` folder, one suffix per engine so
they sit side by side for comparison: `output/<tune>.wav` (current),
`<tune>.orig.wav` (original), `<tune>.ref.wav` (reference).

## Prerequisites

The player only orchestrates the engine, so first make sure the pieces it drives exist:

1. Build the firmware:  `make`  →  `build/SwinSID88-{pal,ntsc}.elf`  (the **current** engine)
2. Build the engine:  `( cd sim && make )`  →  `tools/swinsid.dll`
   (needs `pacman -S mingw-w64-ucrt-x86_64-libsidplayfp` for the reference engine)
3. Put `.sid` files in the repo-root `tunes/` folder.

The **original** engine uses the committed `tools/SwinSID88.original.elf`
baseline, so it works out of the box (no build needed).

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

# Options: sub-song, duration, sample rate, chip model
swimsid render Delta --song 12 --seconds 20 --rate 48000 --6581

# Play the whole tune live through the speakers (Ctrl-C to stop)
swimsid play Wizball

# Compare the current firmware against the original baseline and the reference
swimsid render Commando --engine original    # -> output/Commando.orig.wav
swimsid render Commando --engine reference   # -> output/Commando.ref.wav
swimsid play Commando --engine reference

# Render to a custom location (a .wav file, or a folder to drop the file in)
swimsid render Commando --out C:\tmp\take1.wav
swimsid render Commando -o C:\tmp\renders\      # -> C:\tmp\renders\Commando.wav
```

`<tune>` is either a name as shown by `list` (e.g. `Commando`) or a path to a
`.sid` file.

| Option | Meaning | Default |
| --- | --- | --- |
| `--song N`    | sub-song number (1-based) | 1 |
| `--seconds S` | render duration (ignored for `play`, which runs the whole tune) | 180 |
| `--rate R`    | output sample rate (Hz) | 44100 |
| `--6581` / `--8580` | force the SID chip model | auto (from the SID header) |
| `--engine E` (`-e`) | engine to drive: `current`, `original`, or `reference` | current |
| `--region R` (`-r`) | C64 clock: `Auto`, `Pal`, or `Ntsc`; `Auto` follows the SID header and loads the matching firmware build | auto (from the SID header) |
| `--out P` (`-o`) | render output: a `.wav` file, or a folder to receive `<tune><suffix>.wav` (render only) | `output/` |
| `--raw-level` | keep the firmware's native (louder) output instead of level-matching it to the reference for A/B | off (matched) |

Run `swimsid -h` (or `swimsid render -h`) for full help.

## GUI

```bash
swimsid          # or: dotnet run   (from the player/ folder)
```

Pick a tune from the list, adjust the options, then **Play** or **Render to WAV**.
Use the **Engine** dropdown to switch between the current firmware, the original
baseline, and the libsidplayfp reference. Leave **Output** blank to use the
default `output/` folder, or point it at a `.wav` file or a folder.
**Play** streams live and starts almost immediately; **Stop** halts it at once
(it calls `swinsid_stop()` in the DLL). The log pane shows the engine output.

## Repo-root discovery

The tool locates the repo root by looking for `.git` / `tools/swinsid.dll`
walking up from the executable (and the working directory). Set the
`SWIMSID_ROOT` environment variable to override.
