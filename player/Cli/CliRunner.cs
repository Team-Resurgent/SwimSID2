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
        var referenceOption = new Option<bool>("--reference", "--ref")
        {
            Description = "Use the libsidplayfp reference player (accurate C64 + reSIDfp) instead of the SwinSID firmware.",
        };

        // --- render ---
        var renderCommand = new Command("render", "Render a tune to output/<tune>.wav (reference -> <tune>.ref.wav).");
        renderCommand.Arguments.Add(tuneArgument);
        renderCommand.Options.Add(songOption);
        renderCommand.Options.Add(secondsOption);
        renderCommand.Options.Add(rateOption);
        renderCommand.Options.Add(use6581Option);
        renderCommand.Options.Add(referenceOption);
        renderCommand.SetAction((parseResult, ct) => RunAsync(
            parseResult, tuneArgument, songOption, secondsOption, rateOption, use6581Option, referenceOption, play: false, ct));
        root.Subcommands.Add(renderCommand);

        // --- play ---
        var playCommand = new Command("play", "Play the whole tune live through the default audio device (Ctrl-C to stop).");
        playCommand.Arguments.Add(tuneArgument);
        playCommand.Options.Add(songOption);
        playCommand.Options.Add(secondsOption);
        playCommand.Options.Add(rateOption);
        playCommand.Options.Add(use6581Option);
        playCommand.Options.Add(referenceOption);
        playCommand.SetAction((parseResult, ct) => RunAsync(
            parseResult, tuneArgument, songOption, secondsOption, rateOption, use6581Option, referenceOption, play: true, ct));
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
        Option<bool> referenceOption,
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
                Engine = parseResult.GetValue(referenceOption) ? SidEngine.Reference : SidEngine.Firmware,
            };
            Console.WriteLine($"{tune.Name}: song {song} of {tune.Songs} (default {tune.StartSong})");

            var engineName = settings.Engine == SidEngine.Reference ? "libsidplayfp reference" : "SwinSID firmware";
            void Log(string line) => Console.WriteLine(line);

            if (play)
            {
                Console.WriteLine($"Playing {tune.Name} (song {settings.Song}, {engineName}) - whole tune, Ctrl-C to stop...");
                return await runner.PlayAsync(tune, settings, Log, ct);
            }

            var outPath = runner.OutputWavPath(tune, settings.Engine);
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
}
