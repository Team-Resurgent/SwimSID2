using System.Runtime.InteropServices;
using System.Runtime.Versioning;
using Avalonia;
using SwimSid.Player.Cli;

namespace SwimSid.Player;

internal static class Program
{
    // Initialization code. Don't use any Avalonia, third-party APIs or any
    // SynchronizationContext-reliant code before AppMain is called: things aren't
    // initialized yet and stuff might break.
    [STAThread]
    public static int Main(string[] args)
    {
        // No arguments -> open the Avalonia GUI.
        if (args.Length == 0)
        {
            if (OperatingSystem.IsWindows())
                ConsoleWindow.HideIfOwned();

            BuildAvaloniaApp().StartWithClassicDesktopLifetime(args);
            return 0;
        }

        // Arguments -> run as a console/CLI tool (output goes to the current terminal).
        return CliRunner.Invoke(args);
    }

    // Avalonia configuration, don't remove; also used by the visual designer.
    public static AppBuilder BuildAvaloniaApp()
        => AppBuilder.Configure<App>()
            .UsePlatformDetect()
            .WithInterFont()
            .LogToTrace();
}

/// <summary>
/// The app is a console-subsystem exe so CLI output works reliably. When it is
/// launched by double-clicking (Explorer allocates a fresh console just for us),
/// hide that console before showing the GUI. When launched from an existing
/// terminal, leave it alone.
/// </summary>
[SupportedOSPlatform("windows")]
internal static class ConsoleWindow
{
    private const int SW_HIDE = 0;

    [DllImport("kernel32.dll")]
    private static extern IntPtr GetConsoleWindow();

    [DllImport("kernel32.dll")]
    private static extern uint GetConsoleProcessList(uint[] processList, uint count);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);

    public static void HideIfOwned()
    {
        var hwnd = GetConsoleWindow();
        if (hwnd == IntPtr.Zero)
            return;

        var buffer = new uint[4];
        var count = GetConsoleProcessList(buffer, (uint)buffer.Length);

        // Only this process is attached => we own the console (Explorer launch); hide it.
        if (count <= 1)
            ShowWindow(hwnd, SW_HIDE);
    }
}
