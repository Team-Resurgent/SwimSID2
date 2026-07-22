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
    private SidTune? _selectedTune;

    [ObservableProperty] private decimal _song = 1;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(SongRangeText))]
    private decimal _songMax = 1;

    /// <summary>Label showing the selected tune's available sub-song range.</summary>
    public string SongRangeText => SongMax > 1 ? $"Sub-song (1\u2013{(int)SongMax})" : "Sub-song";

    [ObservableProperty] private decimal _seconds = 180;
    [ObservableProperty] private decimal _rate = 44100;
    [ObservableProperty] private bool _use6581;

    /// <summary>Drive the libsidplayfp reference player instead of the SwinSID firmware.</summary>
    [ObservableProperty] private bool _useReference;

    [ObservableProperty]
    [NotifyCanExecuteChangedFor(nameof(RenderCommand))]
    [NotifyCanExecuteChangedFor(nameof(PlayCommand))]
    private bool _isBusy;

    [ObservableProperty] private string _log = "";
    [ObservableProperty] private string _status = "Ready";
    [ObservableProperty] private string _outputFolder = "";

    public MainWindowViewModel()
    {
        try
        {
            _runner = new SwinSidRunner();
            OutputFolder = _runner.Paths.OutputDir;
            RefreshTunes();

            if (!File.Exists(_runner.Paths.EngineDll))
                AppendLog($"Engine not found: {_runner.Paths.EngineDll}\nBuild it:  ( cd sim && make )");
            if (!File.Exists(_runner.Paths.FirmwareElf))
                AppendLog($"Firmware not built: {_runner.Paths.FirmwareElf}\nBuild it:  make");
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
        Filter = Use6581 ? FilterMode.M6581 : FilterMode.M8580,
        Engine = UseReference ? SidEngine.Reference : SidEngine.Firmware,
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
        var engineName = UseReference ? "libsidplayfp" : "firmware";
        Status = play ? $"Playing {tune.Name} ({engineName})..." : $"Rendering {tune.Name} ({engineName})...";
        AppendLog(play
            ? $"--- play: {tune.Name} (song {(int)Song}, whole tune, {(int)Rate} Hz, {(Use6581 ? "6581" : "8580")}, {engineName}) ---"
            : $"--- render: {tune.Name} (song {(int)Song}, {(double)Seconds:0.##}s, {(int)Rate} Hz, {(Use6581 ? "6581" : "8580")}, {engineName}) ---");

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
                Status = play ? "Playback finished" : "Saved " + _runner.OutputWavPath(tune, Settings.Engine);
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
