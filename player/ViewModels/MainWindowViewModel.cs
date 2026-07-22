using System.Collections.ObjectModel;
using Avalonia.Threading;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using SwimSid.Player.Core;

namespace SwimSid.Player.ViewModels;

public partial class MainWindowViewModel : ObservableObject
{
    private readonly SwinSidRunner? _runner;
    private CancellationTokenSource? _cts;

    public ObservableCollection<SidTune> Tunes { get; } = new();

    [ObservableProperty]
    [NotifyCanExecuteChangedFor(nameof(RenderCommand))]
    [NotifyCanExecuteChangedFor(nameof(PlayCommand))]
    [NotifyPropertyChangedFor(nameof(ResolvedModelText))]
    [NotifyPropertyChangedFor(nameof(ResolvedRegionText))]
    private SidTune? _selectedTune;

    [ObservableProperty] private decimal _song = 1;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(SongRangeText))]
    private decimal _songMax = 1;

    /// <summary>Label showing the selected tune's available sub-song range.</summary>
    public string SongRangeText => SongMax > 1 ? $"Sub-song (1\u2013{(int)SongMax})" : "Sub-song";

    [ObservableProperty] private decimal _seconds = 180;
    [ObservableProperty] private decimal _rate = 44100;

    /// <summary>Chip models: Auto (follow the SID header, default), or force 6581/8580.</summary>
    public string[] Models { get; } = { "Auto", "6581", "8580" };

    /// <summary>The chip model to emulate; Auto follows each tune's own header.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(ResolvedModelText))]
    private string _selectedModel = "Auto";

    /// <summary>The FilterMode the "Chip model" selection maps to.</summary>
    public FilterMode SelectedFilterMode =>
        SelectedModel switch { "6581" => FilterMode.M6581, "8580" => FilterMode.M8580, _ => FilterMode.Auto };

    /// <summary>Live read-out of the model the engine will actually use for the selected tune.</summary>
    public string ResolvedModelText =>
        SelectedTune is null ? "" : $"\u2192 {SelectedTune.ResolvedModel(SelectedFilterMode)}";

    /// <summary>Engines the user can choose between: current firmware, original baseline, reference.</summary>
    public SidEngine[] Engines { get; } = { SidEngine.Current, SidEngine.Original, SidEngine.Reference };

    /// <summary>The engine to drive.</summary>
    [ObservableProperty] private SidEngine _selectedEngine = SidEngine.Current;

    /// <summary>C64 video standards: Auto (follow the SID header, default), or force PAL/NTSC.</summary>
    public Region[] Regions { get; } = { Region.Auto, Region.Pal, Region.Ntsc };

    /// <summary>The C64 clock used for both firmware timing and the reference; Auto follows each tune's header.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(ResolvedRegionText))]
    private Region _selectedRegion = Region.Auto;

    /// <summary>Live read-out of the region the engine will actually use for the selected tune.</summary>
    public string ResolvedRegionText =>
        SelectedTune is null ? "" : $"\u2192 {SelectedTune.ResolvedRegionText(SelectedRegion)}";

    /// <summary>Scale the firmware down to reSIDfp's level so current/original/reference
    /// are loudness-matched for A/B. On by default; no effect on the reference itself.</summary>
    [ObservableProperty] private bool _matchLevel = true;

    [ObservableProperty]
    [NotifyCanExecuteChangedFor(nameof(RenderCommand))]
    [NotifyCanExecuteChangedFor(nameof(PlayCommand))]
    private bool _isBusy;

    [ObservableProperty] private string _log = "";
    [ObservableProperty] private string _status = "Ready";
    [ObservableProperty] private string _outputFolder = "";

    /// <summary>Optional render output override (a .wav file or a folder). Empty = default output/ folder.</summary>
    [ObservableProperty] private string _outputPath = "";

    public MainWindowViewModel()
    {
        try
        {
            _runner = new SwinSidRunner();
            OutputFolder = _runner.Paths.OutputDir;
            RefreshTunes();

            if (!File.Exists(_runner.Paths.EngineDll))
                AppendLog($"Engine not found: {_runner.Paths.EngineDll}\nBuild it:  ( cd sim && make )");
            if (!File.Exists(_runner.Paths.CurrentPalElf))
                AppendLog($"Current firmware not built: {_runner.Paths.CurrentPalElf}\nBuild it:  make");
        }
        catch (Exception ex)
        {
            Status = "Error";
            AppendLog(ex.Message);
        }
    }

    private RenderSettings Settings => new()
    {
        Song = (int)Song,
        Seconds = (double)Seconds,
        Rate = (int)Rate,
        Filter = SelectedFilterMode,
        Engine = SelectedEngine,
        Region = SelectedRegion,
        MatchLevel = MatchLevel,
        OutputPath = string.IsNullOrWhiteSpace(OutputPath) ? null : OutputPath.Trim(),
    };

    private static string EngineName(SidEngine engine) => engine switch
    {
        SidEngine.Current => "current firmware",
        SidEngine.Original => "original firmware",
        SidEngine.Reference => "libsidplayfp",
        _ => engine.ToString(),
    };

    // When a tune is selected, jump to its default song and expose its range.
    partial void OnSelectedTuneChanged(SidTune? value)
    {
        SongMax = value is null ? 1 : Math.Max(1, value.Songs);
        Song = value is null ? 1 : Math.Clamp(value.StartSong, 1, value.Songs);
    }

    private bool CanRun() => !IsBusy && SelectedTune is not null && _runner is not null;

    [RelayCommand]
    private void Refresh() => RefreshTunes();

    private void RefreshTunes()
    {
        if (_runner is null)
            return;

        var previous = SelectedTune?.Name;
        Tunes.Clear();
        foreach (var t in _runner.ListTunes())
            Tunes.Add(t);

        SelectedTune = Tunes.FirstOrDefault(t => t.Name == previous) ?? Tunes.FirstOrDefault();
        Status = Tunes.Count == 0
            ? $"No tunes in {_runner.Paths.TunesDir}"
            : $"{Tunes.Count} tune(s) in {_runner.Paths.TunesDir}";
    }

    [RelayCommand(CanExecute = nameof(CanRun))]
    private Task RenderAsync() => RunAsync(play: false);

    [RelayCommand(CanExecute = nameof(CanRun))]
    private Task PlayAsync() => RunAsync(play: true);

    [RelayCommand]
    private void Stop()
    {
        _cts?.Cancel();
        Status = "Stopping...";
    }

    private async Task RunAsync(bool play)
    {
        if (_runner is null || SelectedTune is null)
            return;

        var tune = SelectedTune;
        IsBusy = true;
        _cts = new CancellationTokenSource();
        var engineName = EngineName(SelectedEngine);
        var modelName = SelectedModel.ToLowerInvariant();
        Status = play ? $"Playing {tune.Name} ({engineName})..." : $"Rendering {tune.Name} ({engineName})...";
        AppendLog(play
            ? $"--- play: {tune.Name} (song {(int)Song}, whole tune, {(int)Rate} Hz, {modelName}, {engineName}) ---"
            : $"--- render: {tune.Name} (song {(int)Song}, {(double)Seconds:0.##}s, {(int)Rate} Hz, {modelName}, {engineName}) ---");

        try
        {
            void Log(string line) => Dispatcher.UIThread.Post(() => AppendLog(line));

            var rc = play
                ? await _runner.PlayAsync(tune, Settings, Log, _cts.Token)
                : await _runner.RenderAsync(tune, Settings, Log, _cts.Token);

            if (_cts.IsCancellationRequested)
                Status = "Stopped";
            else if (rc != 0)
                Status = $"Emulator exited with code {rc}";
            else
                Status = play ? "Playback finished"
                              : "Saved " + _runner.OutputWavPath(tune, Settings.Engine, Settings.OutputPath);
        }
        catch (Exception ex)
        {
            AppendLog("Error: " + ex.Message);
            Status = "Error";
        }
        finally
        {
            IsBusy = false;
            _cts?.Dispose();
            _cts = null;
        }
    }

    private void AppendLog(string line) => Log += (Log.Length == 0 ? "" : "\n") + line;
}
