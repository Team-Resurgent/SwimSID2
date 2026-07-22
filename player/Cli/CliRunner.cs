using System.CommandLine;
using SwimSid.Player.Core;

namespace SwimSid.Player.Cli;

/// <summary>System.CommandLine front-end. Used when the program is started with arguments.</summary>
public static class CliRunner
{
    public static int Invoke(string[] args)
        => BuildRootCommand().Parse(args).Invoke();

    private static RootCommand BuildRootCommand()
    {
        var root = new RootCommand(
            "SwimSID2 player - list, render, or play SID tunes. Run with no arguments to open the GUI.");

        // --- list ---
        var listCommand = new Command("list", "List the .sid tunes available in the tunes/ folder.");
        listCommand.SetAction(_ =>
        {
            try
            {
                var runner = new SwinSidRunner();
                var tunes = runner.ListTunes();
                if (tunes.Count == 0)
                {
                    Console.WriteLine($"No .sid files found in {runner.Paths.TunesDir}");
                    return 0;
                }

                Console.WriteLine($"Tunes in {runner.Paths.TunesDir}:");
                foreach (var t in tunes)
                    Console.WriteLine(t.Songs > 1
                        ? $"  {t.Name}  ({t.Songs} songs, default {t.StartSong})"
                        : $"  {t.Name}");
                return 0;
            }
            catch (Exception ex)
            {
                Console.Error.WriteLine("Error: " + ex.Message);
                return 1;
            }
        });
        root.Subcommands.Add(listCommand);

        // --- shared arg/options ---
        var tuneArgument = new Argument<string>("tune")
        {
            Description = "Tune name (as shown by 'list') or a path to a .sid file.",
        };
        var songOption = new Option<int>("--song")
        {
            Description = "Sub-song number (1-based). Defaults to the tune's own start song.",
            DefaultValueFactory = _ => 0,   // 0 => use the tune's default start song
        };
        var secondsOption = new Option<double>("--seconds")
        {
            Description = "Render duration in seconds (ignored for play, which runs the whole tune).",
            DefaultValueFactory = _ => 180,
        };
        var rateOption = new Option<int>("--rate")
        {
            Description = "Output sample rate in Hz.",
            DefaultValueFactory = _ => 44100,
        };
        var use6581Option = new Option<bool>("--6581")
        {
            Description = "Use 6581 filter mode (default is 8580).",
        };
        var engineOption = new Option<SidEngine>("--engine", "-e")
        {
            Description = "Engine: current (built firmware), original (frozen baseline), or reference (libsidplayfp).",
            DefaultValueFactory = _ => SidEngine.Current,
        };
        var regionOption = new Option<Region>("--region", "-r")
        {
            Description = "C64 clock: Pal (default) or Ntsc. Match how the firmware was built (make NTSC=1).",
            DefaultValueFactory = _ => Region.Pal,
        };
        var outOption = new Option<string?>("--out", "-o")
        {
            Description = "Render output path: a .wav file, or a folder to receive <tune><suffix>.wav. Defaults to the output/ folder.",
        };

        // --- render ---
        var renderCommand = new Command("render",
            "Render a tune to output/<tune>.wav (original -> <tune>.orig.wav, reference -> <tune>.ref.wav).");
        renderCommand.Arguments.Add(tuneArgument);
        renderCommand.Options.Add(songOption);
        renderCommand.Options.Add(secondsOption);
        renderCommand.Options.Add(rateOption);
        renderCommand.Options.Add(use6581Option);
        renderCommand.Options.Add(engineOption);
        renderCommand.Options.Add(regionOption);
        renderCommand.Options.Add(outOption);
        renderCommand.SetAction((parseResult, ct) => RunAsync(
            parseResult, tuneArgument, songOption, secondsOption, rateOption, use6581Option, engineOption, regionOption, outOption, play: false, ct));
        root.Subcommands.Add(renderCommand);

        // --- play ---
        var playCommand = new Command("play", "Play the whole tune live through the default audio device (Ctrl-C to stop).");
        playCommand.Arguments.Add(tuneArgument);
        playCommand.Options.Add(songOption);
        playCommand.Options.Add(secondsOption);
        playCommand.Options.Add(rateOption);
        playCommand.Options.Add(use6581Option);
        playCommand.Options.Add(engineOption);
        playCommand.Options.Add(regionOption);
        playCommand.SetAction((parseResult, ct) => RunAsync(
            parseResult, tuneArgument, songOption, secondsOption, rateOption, use6581Option, engineOption, regionOption, outOption: null, play: true, ct));
        root.Subcommands.Add(playCommand);

        return root;
    }

    private static async Task<int> RunAsync(
        ParseResult parseResult,
        Argument<string> tuneArgument,
        Option<int> songOption,
        Option<double> secondsOption,
        Option<int> rateOption,
        Option<bool> use6581Option,
        Option<SidEngine> engineOption,
        Option<Region> regionOption,
        Option<string?>? outOption,
        bool play,
        CancellationToken ct)
    {
        try
        {
            var runner = new SwinSidRunner();
            var tune = runner.ResolveTune(parseResult.GetValue(tuneArgument)!);

            // No --song (or an out-of-range value) => the tune's own default song.
            int song = parseResult.GetValue(songOption);
            if (song < 1 || song > tune.Songs)
                song = tune.StartSong;

            var settings = new RenderSettings
            {
                Song = song,
                Seconds = parseResult.GetValue(secondsOption),
                Rate = parseResult.GetValue(rateOption),
                Filter = parseResult.GetValue(use6581Option) ? FilterMode.M6581 : FilterMode.M8580,
                Engine = parseResult.GetValue(engineOption),
                Region = parseResult.GetValue(regionOption),
                OutputPath = outOption is null ? null : parseResult.GetValue(outOption),
            };
            Console.WriteLine($"{tune.Name}: song {song} of {tune.Songs} (default {tune.StartSong})");

            var engineName = EngineName(settings.Engine);
            void Log(string line) => Console.WriteLine(line);

            if (play)
            {
                Console.WriteLine($"Playing {tune.Name} (song {settings.Song}, {engineName}) - whole tune, Ctrl-C to stop...");
                return await runner.PlayAsync(tune, settings, Log, ct);
            }

            var outPath = runner.OutputWavPath(tune, settings.Engine, settings.OutputPath);
            Console.WriteLine($"Rendering {tune.Name} ({engineName}) -> {outPath}");
            var rc = await runner.RenderAsync(tune, settings, Log, ct);
            if (rc == 0)
                Console.WriteLine("Done: " + outPath);
            return rc;
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine("Error: " + ex.Message);
            return 1;
        }
    }

    private static string EngineName(SidEngine engine) => engine switch
    {
        SidEngine.Current => "current SwinSID firmware",
        SidEngine.Original => "original SwinSID firmware",
        SidEngine.Reference => "libsidplayfp reference",
        _ => engine.ToString(),
    };
}
