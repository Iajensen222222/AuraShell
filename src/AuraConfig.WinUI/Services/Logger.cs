using System;
using System.IO;
using System.Text;
using System.Threading;

namespace AuraConfig.Services;

/// <summary>
/// Thread-safe rolling file logger for the WinUI app. Writes to
/// %LOCALAPPDATA%\AuraShell\app.log. Rotates the file to app.log.1 when it
/// exceeds 5 MB. All writes are queued through a lock and never throw.
///
/// Usage:
///   Logger.Info("DashboardPage", "Loaded");
///   Logger.Warn("ServiceManager", "Pipe connection refused");
///   Logger.Error("VisualsPage", "Hex parse failed", ex);
/// </summary>
public static class Logger
{
    public enum Level { Trace, Debug, Info, Warn, Error }

    /// <summary>Lowest level that will be written. Default Debug in dev, Info in release.</summary>
    public static Level MinimumLevel { get; set; } =
#if DEBUG
        Level.Debug;
#else
        Level.Info;
#endif

    private static readonly string LogDir = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "AuraShell");

    private static readonly string LogPath  = Path.Combine(LogDir, "app.log");
    private static readonly string PrevPath = Path.Combine(LogDir, "app.log.1");

    private const long MaxBytes = 5L * 1024 * 1024; // 5 MB

    private static readonly object _gate = new();
    private static int _sessionMarked;

    // ── Public API ─────────────────────────────────────────────────────────

    public static void Trace(string tag, string message)      => Write(Level.Trace, tag, message, null);
    public static void Debug(string tag, string message)      => Write(Level.Debug, tag, message, null);
    public static void Info (string tag, string message)      => Write(Level.Info,  tag, message, null);
    public static void Warn (string tag, string message)      => Write(Level.Warn,  tag, message, null);
    public static void Error(string tag, string message, Exception? ex = null)
                                                              => Write(Level.Error, tag, message, ex);

    /// <summary>Returns the absolute path of the log file so UI can link to it.</summary>
    public static string LogFilePath => LogPath;

    // ── Internals ──────────────────────────────────────────────────────────

    private static void Write(Level lvl, string tag, string message, Exception? ex)
    {
        if (lvl < MinimumLevel) return;
        try
        {
            lock (_gate)
            {
                EnsureDirAndSessionHeader();
                MaybeRotate();

                var sb = new StringBuilder(256);
                sb.Append('[').Append(DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss.fff")).Append("] ");
                sb.Append('[').Append(LevelTag(lvl)).Append("] ");
                sb.Append('[').Append(tag).Append("] ");
                sb.Append(message);
                if (ex != null) sb.Append(" — ").Append(ex.GetType().Name).Append(": ").Append(ex.Message);
                sb.Append('\n');

                File.AppendAllText(LogPath, sb.ToString());

                if (ex != null)
                {
                    File.AppendAllText(LogPath,
                        "  StackTrace:\n  " + (ex.StackTrace ?? "(no stack)").Replace("\n", "\n  ") + "\n");
                }
            }
        }
        catch { /* never throw from logger */ }
    }

    private static void EnsureDirAndSessionHeader()
    {
        if (!Directory.Exists(LogDir)) Directory.CreateDirectory(LogDir);
        if (Interlocked.Exchange(ref _sessionMarked, 1) == 0)
        {
            try
            {
                var header = new StringBuilder();
                header.Append("\n========================================\n");
                header.Append("=== Session start ").Append(DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss")).Append('\n');
                header.Append("=== PID ").Append(Environment.ProcessId)
                      .Append("  CLR ").Append(Environment.Version)
                      .Append("  OS ").Append(Environment.OSVersion.VersionString).Append('\n');
                header.Append("========================================\n");
                File.AppendAllText(LogPath, header.ToString());
            }
            catch { }
        }
    }

    private static void MaybeRotate()
    {
        try
        {
            var info = new FileInfo(LogPath);
            if (!info.Exists || info.Length < MaxBytes) return;
            if (File.Exists(PrevPath)) File.Delete(PrevPath);
            File.Move(LogPath, PrevPath);
        }
        catch { }
    }

    private static string LevelTag(Level l) => l switch
    {
        Level.Trace => "TRACE",
        Level.Debug => "DEBUG",
        Level.Info  => "INFO ",
        Level.Warn  => "WARN ",
        Level.Error => "ERROR",
        _           => "?    ",
    };
}
