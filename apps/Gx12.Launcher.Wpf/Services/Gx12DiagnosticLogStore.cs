using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.Json;
using Gx12.Launcher.Wpf.Models;

namespace Gx12.Launcher.Wpf.Services;

public sealed class Gx12DiagnosticLogStore
{
    private const int PreviewCharacterLimit = 220;
    private const string DiagnosticDirectoryName = "wpf-diagnostics";
    private const string HistoryFileName = "history.jsonl";

    public DiagnosticHistoryItem Save(
        AppPaths paths,
        Gx12DiagnosticCommand command,
        Gx12DiagnosticResult result)
    {
        var completedAt = result.CompletedAt == default
            ? DateTimeOffset.Now
            : result.CompletedAt;
        var startedAt = result.StartedAt == default
            ? completedAt
            : result.StartedAt;
        var logDirectory = GetLogDirectory(paths);
        Directory.CreateDirectory(logDirectory);

        var suffix = Guid.NewGuid().ToString("N")[..6];
        var logPath = Path.Combine(
            logDirectory,
            $"{completedAt:yyyyMMdd-HHmmss-fff}-{MakeSafeFileName(command.Name)}-{suffix}.txt");
        File.WriteAllText(
            logPath,
            BuildLogText(command, result, startedAt, completedAt),
            Encoding.UTF8);

        var item = new DiagnosticHistoryItem(
            command.Name,
            result.IsSuccess,
            result.Message,
            result.CommandLine,
            logPath,
            startedAt,
            completedAt,
            BuildOutputPreview(result.Output));

        File.AppendAllText(
            GetHistoryPath(paths),
            JsonSerializer.Serialize(item) + Environment.NewLine,
            Encoding.UTF8);
        return item;
    }

    public IReadOnlyList<DiagnosticHistoryItem> LoadRecent(AppPaths paths, int maxCount = 12)
    {
        var historyPath = GetHistoryPath(paths);
        if (!File.Exists(historyPath))
        {
            return Array.Empty<DiagnosticHistoryItem>();
        }

        return File.ReadLines(historyPath)
            .Where(line => !string.IsNullOrWhiteSpace(line))
            .Select(TryParseHistoryItem)
            .Where(item => item is not null)
            .Cast<DiagnosticHistoryItem>()
            .Reverse()
            .Take(Math.Max(1, maxCount))
            .ToList();
    }

    public string ReadLog(DiagnosticHistoryItem item)
    {
        return File.Exists(item.LogPath)
            ? File.ReadAllText(item.LogPath, Encoding.UTF8)
            : $"Log file is missing: {item.LogPath}";
    }

    private static string GetLogDirectory(AppPaths paths)
    {
        return Path.Combine(paths.RepoRoot, "logs", DiagnosticDirectoryName);
    }

    private static string GetHistoryPath(AppPaths paths)
    {
        return Path.Combine(GetLogDirectory(paths), HistoryFileName);
    }

    private static DiagnosticHistoryItem? TryParseHistoryItem(string line)
    {
        try
        {
            return JsonSerializer.Deserialize<DiagnosticHistoryItem>(line);
        }
        catch
        {
            return null;
        }
    }

    private static string BuildLogText(
        Gx12DiagnosticCommand command,
        Gx12DiagnosticResult result,
        DateTimeOffset startedAt,
        DateTimeOffset completedAt)
    {
        var builder = new StringBuilder();
        builder.AppendLine($"name={command.Name}");
        builder.AppendLine($"status={(result.IsSuccess ? "ok" : "failed")}");
        builder.AppendLine($"started_at={startedAt:O}");
        builder.AppendLine($"completed_at={completedAt:O}");
        builder.AppendLine($"message={result.Message}");
        builder.AppendLine($"command_line={result.CommandLine}");
        builder.AppendLine();
        builder.AppendLine("[output]");
        builder.AppendLine(string.IsNullOrWhiteSpace(result.Output) ? "(no output)" : result.Output.TrimEnd());
        return builder.ToString();
    }

    private static string BuildOutputPreview(string output)
    {
        var firstLine = output
            .Replace("\r\n", "\n", StringComparison.Ordinal)
            .Split('\n')
            .Select(line => line.Trim())
            .FirstOrDefault(line => !string.IsNullOrWhiteSpace(line));
        if (string.IsNullOrWhiteSpace(firstLine))
        {
            return "(no output)";
        }

        return firstLine.Length <= PreviewCharacterLimit
            ? firstLine
            : firstLine[..PreviewCharacterLimit] + "...";
    }

    private static string MakeSafeFileName(string value)
    {
        var invalid = Path.GetInvalidFileNameChars().ToHashSet();
        var builder = new StringBuilder();
        foreach (var character in value.Trim().ToLowerInvariant())
        {
            if (char.IsWhiteSpace(character) || character == '-' || character == '_')
            {
                builder.Append('-');
            }
            else if (!invalid.Contains(character) && char.IsLetterOrDigit(character))
            {
                builder.Append(character);
            }
        }

        return builder.Length == 0 ? "diagnostic" : builder.ToString();
    }
}
