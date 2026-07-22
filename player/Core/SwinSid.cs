using System.Runtime.InteropServices;

namespace SwimSid.Player.Core;

public enum FilterMode
{
    M6581,
    M8580,
}

/// <summary>C64 video standard / SID master clock. Match how the firmware was built.</summary>
public enum Region
{
    /// <summary>PAL (985248 Hz) - the firmware's default build.</summary>
    Pal,
    /// <summary>NTSC (1022727 Hz) - requires a firmware built with make NTSC=1.</summary>
    Ntsc,
}

/// <summary>Which SID engine to drive.</summary>
public enum SidEngine
{
    /// <summary>The current SwinSID firmware under simavr (built from src/, the thing being worked on).</summary>
    Current,
    /// <summary>The frozen original SwinSID firmware baseline, for A/B against the current build.</summary>
    Original,
    /// <summary>libsidplayfp - a complete, cycle-accurate C64 (the "real machine" reference).</summary>
    Reference,
}

/// <summary>A single .sid file discovered in the tunes folder.</summary>
public sealed record SidTune(string Name, string FullPath, int Songs = 1, int StartSong = 1)
{
    public override string ToString() => Name;
}

/// <summary>Render/play parameters passed through to swinsid.dll.</summary>
public sealed class RenderSettings
{
    public int Song { get; set; } = 1;
    public double Seconds { get; set; } = 30;
    public int Rate { get; set; } = 44100;
    public FilterMode Filter { get; set; } = FilterMode.M8580;

    /// <summary>C64 clock for both the firmware timing and the reference; match the firmware build.</summary>
    public Region Region { get; set; } = Region.Pal;

    /// <summary>Current firmware (default), the original firmware baseline, or the libsidplayfp reference.</summary>
    public SidEngine Engine { get; set; } = SidEngine.Current;

    /// <summary>
    /// Optional render output override. When null/empty the default
    /// output/&lt;tune&gt;&lt;suffix&gt;.wav is used. A value ending in a path
    /// separator (or an existing directory) is treated as a folder; otherwise a
    /// value with a file extension is used verbatim as the output file.
    /// </summary>
    public string? OutputPath { get; set; }
}

/// <summary>Resolves the repo-relative locations the tool needs.</summary>
public sealed class SwinSidPaths
{
    public string RepoRoot { get; }

    public string EngineDll => Path.Combine(RepoRoot, "tools", "swinsid.dll");
    /// <summary>The current firmware (PAL build), freshly built from src/ into build/.</summary>
    public string CurrentPalElf => Path.Combine(RepoRoot, "build", "SwinSID88-pal.elf");
    /// <summary>The current firmware (NTSC build).</summary>
    public string CurrentNtscElf => Path.Combine(RepoRoot, "build", "SwinSID88-ntsc.elf");
    /// <summary>The current firmware ELF for the given region.</summary>
    public string CurrentElf(Region region) => region == Region.Ntsc ? CurrentNtscElf : CurrentPalElf;
    /// <summary>The frozen original firmware baseline, committed under tools/.</summary>
    public string OriginalElf => Path.Combine(RepoRoot, "tools", "SwinSID88.original.elf");
    public string TunesDir => Path.Combine(RepoRoot, "tunes");
    public string OutputDir => Path.Combine(RepoRoot, "output");

    /// <summary>
    /// The firmware ELF backing a given engine (null for the non-firmware
    /// reference). The current firmware honours the region so its baked-in pitch
    /// matches; the original baseline is a single region-agnostic snapshot.
    /// </summary>
    public string? ElfFor(SidEngine engine, Region region) => engine switch
    {
        SidEngine.Current => CurrentElf(region),
        SidEngine.Original => OriginalElf,
        _ => null,
    };

    private SwinSidPaths(string repoRoot) => RepoRoot = repoRoot;

    /// <summary>
    /// Locate the repo root by honoring SWIMSID_ROOT, otherwise walking up from the
    /// executable location and the current directory looking for a repo marker.
    /// </summary>
    public static SwinSidPaths Discover()
    {
        var env = Environment.GetEnvironmentVariable("SWIMSID_ROOT");
        if (!string.IsNullOrWhiteSpace(env) && Directory.Exists(env))
            return new SwinSidPaths(Path.GetFullPath(env));

        foreach (var start in new[] { AppContext.BaseDirectory, Directory.GetCurrentDirectory() })
        {
            for (var dir = new DirectoryInfo(start); dir is not null; dir = dir.Parent)
            {
                bool isRoot =
                    File.Exists(Path.Combine(dir.FullName, "tools", "swinsid.dll")) ||
                    File.Exists(Path.Combine(dir.FullName, "src", "SwinSID88.asm")) ||
                    Directory.Exists(Path.Combine(dir.FullName, ".git"));
                if (isRoot)
                    return new SwinSidPaths(dir.FullName);
            }
        }

        throw new DirectoryNotFoundException(
            "Could not locate the SwimSID2 repo root. Set the SWIMSID_ROOT environment " +
            "variable or run from inside the repository.");
    }
}

/// <summary>Lists tunes and drives swinsid.dll to render or play them.</summary>
public sealed class SwinSidRunner
{
    public SwinSidPaths Paths { get; }

    public SwinSidRunner(SwinSidPaths paths)
    {
        Paths = paths;
        NativeMethods.DllPath = paths.EngineDll;
    }

    public SwinSidRunner() : this(SwinSidPaths.Discover()) { }

    public IReadOnlyList<SidTune> ListTunes()
    {
        if (!Directory.Exists(Paths.TunesDir))
            return Array.Empty<SidTune>();

        return Directory.EnumerateFiles(Paths.TunesDir, "*.sid", SearchOption.TopDirectoryOnly)
            .OrderBy(Path.GetFileName, StringComparer.OrdinalIgnoreCase)
            .Select(MakeTune)
            .ToList();
    }

    /// <summary>Build a SidTune, reading the song count/default song from the PSID header.</summary>
    private static SidTune MakeTune(string path)
    {
        var (songs, startSong) = ReadSongInfo(path);
        return new SidTune(Path.GetFileNameWithoutExtension(path), Path.GetFullPath(path), songs, startSong);
    }

    /// <summary>
    /// Read the number of sub-songs and the default start song from a PSID/RSID
    /// header (both are 16-bit big-endian at offsets 0x0E and 0x10). Falls back
    /// to a single song on any error.
    /// </summary>
    private static (int Songs, int StartSong) ReadSongInfo(string path)
    {
        try
        {
            using var fs = File.OpenRead(path);
            Span<byte> h = stackalloc byte[0x18];
            if (fs.Read(h) < h.Length)
                return (1, 1);
            if (!(h[0] == (byte)'P' || h[0] == (byte)'R') || h[1] != (byte)'S' || h[2] != (byte)'I' || h[3] != (byte)'D')
                return (1, 1);

            int songs = (h[0x0E] << 8) | h[0x0F];
            int start = (h[0x10] << 8) | h[0x11];
            if (songs < 1) songs = 1;
            if (start < 1) start = 1;
            if (start > songs) start = songs;
            return (songs, start);
        }
        catch
        {
            return (1, 1);
        }
    }

    /// <summary>Resolve a tune from a direct path or a name (with/without .sid) in the tunes folder.</summary>
    public SidTune ResolveTune(string nameOrPath)
    {
        if (File.Exists(nameOrPath))
            return MakeTune(nameOrPath);

        var match = ListTunes().FirstOrDefault(t =>
            string.Equals(t.Name, nameOrPath, StringComparison.OrdinalIgnoreCase) ||
            string.Equals(t.Name + ".sid", nameOrPath, StringComparison.OrdinalIgnoreCase) ||
            string.Equals(Path.GetFileName(t.FullPath), nameOrPath, StringComparison.OrdinalIgnoreCase));

        return match ?? throw new FileNotFoundException(
            $"Tune '{nameOrPath}' was not found in {Paths.TunesDir}.");
    }

    private static string WavSuffix(SidEngine engine) => engine switch
    {
        SidEngine.Original => ".orig.wav",
        SidEngine.Reference => ".ref.wav",
        _ => ".wav",
    };

    /// <summary>
    /// Output WAV for a tune. Each engine gets its own suffix so the current
    /// firmware, original baseline, and reference renders can sit side by side
    /// for A/B comparison: "&lt;name&gt;.wav", ".orig.wav", ".ref.wav".
    ///
    /// <paramref name="outputOverride"/> lets the caller redirect the render:
    /// a directory (existing, or ending in a separator) receives the suffixed
    /// file name; any other value is used verbatim as the output file path.
    /// </summary>
    public string OutputWavPath(SidTune tune, SidEngine engine = SidEngine.Current, string? outputOverride = null)
    {
        if (string.IsNullOrWhiteSpace(outputOverride))
            return Path.Combine(Paths.OutputDir, tune.Name + WavSuffix(engine));

        var full = Path.GetFullPath(outputOverride);
        bool asDirectory =
            Directory.Exists(full) ||
            outputOverride.EndsWith(Path.DirectorySeparatorChar) ||
            outputOverride.EndsWith(Path.AltDirectorySeparatorChar) ||
            !Path.HasExtension(full);

        return asDirectory ? Path.Combine(full, tune.Name + WavSuffix(engine)) : full;
    }

    private void EnsureReady(SidEngine engine, Region region)
    {
        if (!File.Exists(Paths.EngineDll))
            throw new FileNotFoundException(
                $"Engine not found: {Paths.EngineDll}\nBuild it with:  ( cd sim && make )");

        // The reference (libsidplayfp) engine needs no firmware ELF.
        var elf = Paths.ElfFor(engine, region);
        if (elf is not null && !File.Exists(elf))
            throw new FileNotFoundException(engine == SidEngine.Current
                ? $"Current firmware not built: {elf}\nBuild it with:  make"
                : $"Original firmware baseline missing: {elf}");
    }

    private static NativeMethods.Options ToOptions(RenderSettings s) => new()
    {
        Song = s.Song,
        Seconds = s.Seconds,
        Rate = (uint)s.Rate,
        Filter8580 = s.Filter == FilterMode.M8580 ? 1 : 0,
        Region = s.Region == Region.Ntsc ? 1 : 0,
    };

    public Task<int> RenderAsync(SidTune tune, RenderSettings settings, Action<string>? log, CancellationToken ct)
    {
        EnsureReady(settings.Engine, settings.Region);
        var wav = OutputWavPath(tune, settings.Engine, settings.OutputPath);
        var dir = Path.GetDirectoryName(wav);
        if (!string.IsNullOrEmpty(dir))
            Directory.CreateDirectory(dir);
        return RunNativeAsync(settings.Engine, settings.Region, play: false, tune.FullPath, wav, ToOptions(settings), log, ct);
    }

    public Task<int> PlayAsync(SidTune tune, RenderSettings settings, Action<string>? log, CancellationToken ct)
    {
        EnsureReady(settings.Engine, settings.Region);
        return RunNativeAsync(settings.Engine, settings.Region, play: true, tune.FullPath, null, ToOptions(settings), log, ct);
    }

    /// <summary>
    /// Invoke the blocking native render/play on a background thread. Cancellation
    /// calls swinsid_stop(), which unblocks the engine promptly (stopping audio).
    /// </summary>
    private Task<int> RunNativeAsync(SidEngine engine, Region region, bool play, string sidPath, string? wavPath,
        NativeMethods.Options options, Action<string>? log, CancellationToken ct)
    {
        var elf = Paths.ElfFor(engine, region) ?? "";

        return Task.Run(() =>
        {
            NativeMethods.DllPath = Paths.EngineDll;

            NativeMethods.LogFn? cb = null;
            if (log is not null)
                cb = (line, _) =>
                {
                    var s = Marshal.PtrToStringUTF8(line);
                    if (s is not null) log(s);
                };

            using var reg = ct.Register(NativeMethods.swinsid_stop);
            var opt = options;
            try
            {
                if (engine == SidEngine.Reference)
                    return play
                        ? NativeMethods.swinsid_ref_play(sidPath, ref opt, cb, IntPtr.Zero)
                        : NativeMethods.swinsid_ref_render(sidPath, wavPath ?? "", ref opt, cb, IntPtr.Zero);
                return play
                    ? NativeMethods.swinsid_play(elf, sidPath, ref opt, cb, IntPtr.Zero)
                    : NativeMethods.swinsid_render(elf, sidPath, wavPath ?? "", ref opt, cb, IntPtr.Zero);
            }
            finally
            {
                GC.KeepAlive(cb); // keep the callback alive across the native call
            }
        }, ct);
    }
}
