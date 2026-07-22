using System.Reflection;
using System.Runtime.InteropServices;

namespace SwimSid.Player.Core;

/// <summary>
/// P/Invoke bindings for swinsid.dll (the self-contained emulator engine).
/// The DLL is resolved from <see cref="DllPath"/> so it can live in the repo's
/// tools/ folder rather than beside the managed assemblies.
/// </summary>
internal static class NativeMethods
{
    private const string Lib = "swinsid";

    /// <summary>Absolute path to swinsid.dll; set before the first native call.</summary>
    public static string? DllPath { get; set; }

    static NativeMethods()
    {
        NativeLibrary.SetDllImportResolver(typeof(NativeMethods).Assembly, Resolve);
    }

    private static IntPtr Resolve(string name, Assembly assembly, DllImportSearchPath? searchPath)
    {
        if (name == Lib && !string.IsNullOrEmpty(DllPath) && File.Exists(DllPath))
            return NativeLibrary.Load(DllPath);
        return IntPtr.Zero; // fall back to the default search
    }

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate void LogFn(IntPtr line, IntPtr user);

    [StructLayout(LayoutKind.Sequential)]
    public struct Options
    {
        public int Song;         // 1-based; 0 => tune's start song
        public double Seconds;
        public uint Rate;
        public int Filter8580;   // 1 = 8580, 0 = 6581
    }

    [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
    public static extern void swinsid_default_options(ref Options opt);

    [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
    public static extern int swinsid_render(
        [MarshalAs(UnmanagedType.LPUTF8Str)] string elfPath,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string sidPath,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string wavPath,
        ref Options opt, LogFn? log, IntPtr logUser);

    [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
    public static extern int swinsid_play(
        [MarshalAs(UnmanagedType.LPUTF8Str)] string elfPath,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string sidPath,
        ref Options opt, LogFn? log, IntPtr logUser);

    // libsidplayfp reference engine (no firmware ELF needed).
    [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
    public static extern int swinsid_ref_render(
        [MarshalAs(UnmanagedType.LPUTF8Str)] string sidPath,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string wavPath,
        ref Options opt, LogFn? log, IntPtr logUser);

    [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
    public static extern int swinsid_ref_play(
        [MarshalAs(UnmanagedType.LPUTF8Str)] string sidPath,
        ref Options opt, LogFn? log, IntPtr logUser);

    [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
    public static extern void swinsid_stop();
}
