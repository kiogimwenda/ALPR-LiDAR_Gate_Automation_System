// AppLog.cs — the app's own log: ILogger everywhere, written (in every
// build configuration) to <app data>/gatecontrol.log, newest last, kept
// under ~256 KB. Used where a fallback or failure must be visible in a
// Release build (e.g. ThemeReveal's capture fallback).

using Microsoft.Extensions.Logging;

namespace GateControl.Platform;

public static class AppLog
{
    private static ILoggerFactory? _factory;

    public static void Init(ILoggerFactory factory) => _factory = factory;

    public static ILogger For(string category) =>
        _factory?.CreateLogger(category) ?? Microsoft.Extensions.Logging.Abstractions.NullLogger.Instance;
}

public sealed class FileLoggerProvider : ILoggerProvider
{
    private const long MaxBytes = 256 * 1024;
    private static readonly object Gate = new();
    private readonly string _path = Path.Combine(FileSystem.AppDataDirectory, "gatecontrol.log");

    public ILogger CreateLogger(string categoryName) => new FileLogger(this, categoryName);

    public void Dispose()
    {
    }

    private void Write(string line)
    {
        lock (Gate)
        {
            try
            {
                var info = new FileInfo(_path);
                if (info.Exists && info.Length > MaxBytes)
                {
                    // Keep the newest half.
                    var text = File.ReadAllText(_path);
                    File.WriteAllText(_path, text[(text.Length / 2)..]);
                }
                File.AppendAllText(_path, line + Environment.NewLine);
            }
            catch (IOException)
            {
                // Logging must never take the app down.
            }
            catch (UnauthorizedAccessException)
            {
            }
        }
    }

    private sealed class FileLogger(FileLoggerProvider owner, string category) : ILogger
    {
        public IDisposable? BeginScope<TState>(TState state) where TState : notnull => null;

        public bool IsEnabled(LogLevel logLevel) => logLevel >= LogLevel.Information;

        public void Log<TState>(LogLevel logLevel, EventId eventId, TState state, Exception? exception,
                                Func<TState, Exception?, string> formatter)
        {
            if (!IsEnabled(logLevel))
            {
                return;
            }
            var line = $"{DateTimeOffset.Now:yyyy-MM-dd HH:mm:ss.fff} {logLevel} {category}: {formatter(state, exception)}";
            if (exception is not null)
            {
                line += logLevel >= LogLevel.Error
                    ? Environment.NewLine + exception
                    : $" ({exception.GetType().Name}: {exception.Message})";
            }
            owner.Write(line);
        }
    }
}
