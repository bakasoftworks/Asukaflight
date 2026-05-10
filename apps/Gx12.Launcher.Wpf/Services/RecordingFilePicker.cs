using System;
using System.IO;
using Microsoft.Win32;

namespace Gx12.Launcher.Wpf.Services;

public sealed class RecordingFilePicker : IRecordingFilePicker
{
    private const string RecordingFilter =
        "GX12 recordings (*.gx12rec.csv)|*.gx12rec.csv|CSV files (*.csv)|*.csv|All files (*.*)|*.*";

    public string? PickRecordingOutput(string currentPath, string repoRoot)
    {
        var start = ResolveDialogStart(currentPath, repoRoot);
        var dialog = new SaveFileDialog
        {
            AddExtension = true,
            CheckPathExists = false,
            DefaultExt = "gx12rec.csv",
            FileName = string.IsNullOrWhiteSpace(start.FileName) ? "recording.gx12rec.csv" : start.FileName,
            Filter = RecordingFilter,
            InitialDirectory = start.InitialDirectory,
            OverwritePrompt = true,
            RestoreDirectory = true,
            Title = "Choose recording output"
        };

        return dialog.ShowDialog() == true ? ToDisplayPath(repoRoot, dialog.FileName) : null;
    }

    public string? PickPlaybackRecording(string currentPath, string repoRoot)
    {
        var start = ResolveDialogStart(currentPath, repoRoot);
        var dialog = new OpenFileDialog
        {
            CheckFileExists = true,
            Filter = RecordingFilter,
            FileName = start.FileName,
            InitialDirectory = start.InitialDirectory,
            Multiselect = false,
            RestoreDirectory = true,
            Title = "Choose playback recording"
        };

        return dialog.ShowDialog() == true ? ToDisplayPath(repoRoot, dialog.FileName) : null;
    }

    private static DialogStart ResolveDialogStart(string currentPath, string repoRoot)
    {
        var root = Directory.Exists(repoRoot) ? repoRoot : Environment.CurrentDirectory;
        var fullPath = TryResolveFullPath(root, currentPath);
        var directory = Directory.Exists(fullPath)
            ? fullPath
            : Path.GetDirectoryName(fullPath);
        if (string.IsNullOrWhiteSpace(directory) || !Directory.Exists(directory))
        {
            var logs = Path.Combine(root, "logs");
            directory = Directory.Exists(logs) ? logs : root;
        }

        var fileName = Directory.Exists(fullPath) ? "" : Path.GetFileName(fullPath);
        return new DialogStart(directory, fileName);
    }

    private static string TryResolveFullPath(string repoRoot, string value)
    {
        try
        {
            return ResolveFullPath(repoRoot, value);
        }
        catch
        {
            return Path.Combine(repoRoot, "logs");
        }
    }

    private static string ResolveFullPath(string repoRoot, string value)
    {
        if (string.IsNullOrWhiteSpace(value))
        {
            return Path.Combine(repoRoot, "logs");
        }

        return Path.IsPathRooted(value)
            ? Path.GetFullPath(value)
            : Path.GetFullPath(Path.Combine(repoRoot, value));
    }

    private static string ToDisplayPath(string repoRoot, string selectedPath)
    {
        var fullRoot = Path.GetFullPath(repoRoot)
            .TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar) +
            Path.DirectorySeparatorChar;
        var fullSelected = Path.GetFullPath(selectedPath);
        return fullSelected.StartsWith(fullRoot, StringComparison.OrdinalIgnoreCase)
            ? Path.GetRelativePath(repoRoot, fullSelected)
            : fullSelected;
    }

    private sealed record DialogStart(string InitialDirectory, string FileName);
}
