using System;
using System.Globalization;
using System.IO;

namespace Gx12.Launcher.Wpf.Models;

public sealed record DiagnosticHistoryItem(
    string Name,
    bool IsSuccess,
    string Message,
    string CommandLine,
    string LogPath,
    DateTimeOffset StartedAt,
    DateTimeOffset CompletedAt,
    string OutputPreview)
{
    public string StatusLabel => IsSuccess ? "OK" : "Failed";

    public string CompletedAtText => CompletedAt
        .ToLocalTime()
        .ToString("M/d HH:mm:ss", CultureInfo.InvariantCulture);

    public string DurationText
    {
        get
        {
            var duration = CompletedAt - StartedAt;
            return duration.TotalMilliseconds < 1000
                ? $"{Math.Max(0, duration.TotalMilliseconds):0} ms"
                : $"{Math.Max(0, duration.TotalSeconds):0.0} s";
        }
    }

    public string LogFileName => string.IsNullOrWhiteSpace(LogPath)
        ? ""
        : Path.GetFileName(LogPath);
}
