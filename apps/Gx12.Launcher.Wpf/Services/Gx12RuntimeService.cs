using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading;

namespace Gx12.Launcher.Wpf.Services;

public sealed record RuntimeRunResult(
    bool IsSuccess,
    string Message,
    string CommandLine = "",
    bool ActiveRunRemaining = false);

public sealed record PlaybackBindCommand(
    string RecordingPath,
    string Trigger,
    string ChannelMask,
    bool BlockLiveInput = false);

public sealed class Gx12RuntimeService
{
    public const string StopEventName = @"Local\GX12MouseLauncherStop";

    private const string CompositeTrainerTitle = "GX12 Composite Trainer";
    private const string RecordingTitle = "GX12 Trainer Recording";
    private const string PlaybackTitle = "GX12 Trainer Playback";
    private const string PlaybackBankTitle = "GX12 Trainer Playback Bank";
    private Process? _activeConsoleProcess;

    public RuntimeRunResult StartCompositeTrainer(
        AppPaths paths,
        string profilePath,
        string recordingPath = "",
        int recordingDurationSeconds = 0,
        bool liveReload = true,
        string recordingToggleKey = "",
        bool playbackLoop = false,
        IReadOnlyList<PlaybackBindCommand>? playbackBindings = null,
        bool recordingOverwrite = false,
        string runtimeControlPath = "")
    {
        var activeBindings = NormalizePlaybackBindings(playbackBindings);
        var commandLine = BuildTrainerCommandLine(
            paths,
            profilePath,
            recordingPath,
            recordingDurationSeconds,
            liveReload,
            recordingToggleKey,
            playbackLoop,
            activeBindings,
            recordingOverwrite,
            runtimeControlPath);
        if (!File.Exists(paths.ExePath))
        {
            return new RuntimeRunResult(false, $"Missing executable: {paths.ExePath}", commandLine);
        }

        if (!File.Exists(profilePath))
        {
            return new RuntimeRunResult(false, $"Missing profile: {profilePath}", commandLine);
        }

        if (!string.IsNullOrWhiteSpace(recordingPath) && recordingDurationSeconds < 0)
        {
            return new RuntimeRunResult(false, "Recording duration is invalid.", commandLine);
        }

        foreach (var binding in activeBindings)
        {
            if (string.IsNullOrWhiteSpace(NormalizeOptionalKey(binding.Trigger)))
            {
                return new RuntimeRunResult(false, $"Playback bind for {binding.RecordingPath} needs a hotkey trigger.", commandLine);
            }
        }

        var stopped = StopActiveRun(paths);
        if (!stopped.IsSuccess)
        {
            return stopped with { CommandLine = commandLine };
        }

        try
        {
            if (!string.IsNullOrWhiteSpace(recordingPath))
            {
                EnsureOutputDirectory(paths, recordingPath);
            }
            return StartManagedConsole(
                paths,
                CompositeTrainerTitle,
                GetConsoleScriptPath(paths),
                commandLine,
                $"Profile: {profilePath}",
                $"Running {CompositeTrainerTitle} with {Path.GetFileName(profilePath)}. Recording and playback bind hotkeys are handled inside this run.");
        }
        catch (Exception exception)
        {
            return new RuntimeRunResult(false, $"Start failed: {exception.Message}", commandLine);
        }
    }

    public RuntimeRunResult StartTrainerRecording(
        AppPaths paths,
        string profilePath,
        string recordingPath,
        int durationSeconds,
        bool liveReload,
        string recordingToggleKey,
        bool recordingOverwrite = false,
        string runtimeControlPath = "")
    {
        var commandLine = BuildTrainerRecordCommandLine(
            paths,
            profilePath,
            recordingPath,
            durationSeconds,
            liveReload,
            recordingToggleKey,
            recordingOverwrite,
            runtimeControlPath);
        if (!File.Exists(paths.ExePath))
        {
            return new RuntimeRunResult(false, $"Missing executable: {paths.ExePath}", commandLine);
        }

        if (!File.Exists(profilePath))
        {
            return new RuntimeRunResult(false, $"Missing profile: {profilePath}", commandLine);
        }

        if (string.IsNullOrWhiteSpace(recordingPath))
        {
            return new RuntimeRunResult(false, "Recording path is empty.", commandLine);
        }

        var stopped = StopActiveRun(paths);
        if (!stopped.IsSuccess)
        {
            return stopped with { CommandLine = commandLine };
        }

        try
        {
            EnsureOutputDirectory(paths, recordingPath);
            var normalizedToggleKey = NormalizeOptionalKey(recordingToggleKey);
            var message = string.IsNullOrWhiteSpace(normalizedToggleKey)
                ? $"Recording {Path.GetFileName(recordingPath)} from {Path.GetFileName(profilePath)}. Stop sends neutral and ends the managed run."
                : $"Recording armed for {Path.GetFileName(recordingPath)}. Press {normalizedToggleKey} to start/stop capture; Stop ends the trainer run.";
            return StartManagedConsole(
                paths,
                RecordingTitle,
                GetRecordingConsoleScriptPath(paths),
                commandLine,
                $"Recording: {QuoteForCmd(recordingPath)}",
                message);
        }
        catch (Exception exception)
        {
            return new RuntimeRunResult(false, $"Recording start failed: {exception.Message}", commandLine);
        }
    }

    public RuntimeRunResult StartTrainerPlayback(
        AppPaths paths,
        string recordingPath,
        string port,
        bool loop,
        string channelMask,
        string trigger)
    {
        var commandLine = BuildTrainerPlaybackCommandLine(paths, recordingPath, port, loop, channelMask, trigger);
        if (!File.Exists(paths.ExePath))
        {
            return new RuntimeRunResult(false, $"Missing executable: {paths.ExePath}", commandLine);
        }

        if (string.IsNullOrWhiteSpace(recordingPath))
        {
            return new RuntimeRunResult(false, "Recording path is empty.", commandLine);
        }

        if (!File.Exists(ResolveRepoPath(paths, recordingPath)))
        {
            return new RuntimeRunResult(false, $"Missing recording: {recordingPath}", commandLine);
        }

        if (string.IsNullOrWhiteSpace(channelMask))
        {
            return new RuntimeRunResult(false, "Playback needs at least one selected channel.", commandLine);
        }

        var stopped = StopActiveRun(paths);
        if (!stopped.IsSuccess)
        {
            return stopped with { CommandLine = commandLine };
        }

        try
        {
            return StartManagedConsole(
                paths,
                PlaybackTitle,
                GetPlaybackConsoleScriptPath(paths),
                commandLine,
                $"Recording: {QuoteForCmd(recordingPath)}",
                $"Playing {Path.GetFileName(recordingPath)} on {NormalizePort(port)} with channels {channelMask}. Stop sends neutral and ends the managed run.");
        }
        catch (Exception exception)
        {
            return new RuntimeRunResult(false, $"Playback start failed: {exception.Message}", commandLine);
        }
    }

    public RuntimeRunResult StartTrainerPlaybackBank(
        AppPaths paths,
        string port,
        bool loop,
        IReadOnlyList<PlaybackBindCommand> bindings)
    {
        var commandLine = BuildTrainerPlaybackBankCommandLine(paths, port, loop, bindings);
        if (!File.Exists(paths.ExePath))
        {
            return new RuntimeRunResult(false, $"Missing executable: {paths.ExePath}", commandLine);
        }

        var activeBindings = NormalizePlaybackBindings(bindings);
        if (activeBindings.Count == 0)
        {
            return new RuntimeRunResult(false, "Playback bank needs at least one complete bind.", commandLine);
        }

        foreach (var binding in activeBindings)
        {
            if (string.IsNullOrWhiteSpace(NormalizeOptionalKey(binding.Trigger)))
            {
                return new RuntimeRunResult(false, $"Playback bind for {binding.RecordingPath} needs a hotkey trigger.", commandLine);
            }

            if (!File.Exists(ResolveRepoPath(paths, binding.RecordingPath)))
            {
                return new RuntimeRunResult(false, $"Missing playback recording: {binding.RecordingPath}", commandLine);
            }
        }

        var stopped = StopActiveRun(paths);
        if (!stopped.IsSuccess)
        {
            return stopped with { CommandLine = commandLine };
        }

        try
        {
            return StartManagedConsole(
                paths,
                PlaybackBankTitle,
                GetPlaybackBankConsoleScriptPath(paths),
                commandLine,
                $"Bindings: {activeBindings.Count}",
                $"Playback bank armed on {NormalizePort(port)} with {activeBindings.Count} bind(s). Press the active bind again to stop its playback; Stop sends neutral and ends the managed run.");
        }
        catch (Exception exception)
        {
            return new RuntimeRunResult(false, $"Playback bank start failed: {exception.Message}", commandLine);
        }
    }

    public RuntimeRunResult StopActiveRun(AppPaths paths, int timeoutMilliseconds = 3000)
    {
        try
        {
            var initialCount = CountManagedGx12Processes(paths);
            using (var stopEvent = OpenLauncherStopEvent())
            {
                stopEvent.Set();
            }

            var remainingCount = WaitForManagedGx12Exit(paths, timeoutMilliseconds);
            StopActiveConsoleProcess();

            if (remainingCount == 0)
            {
                var message = initialCount == 0
                    ? "Stop signal sent; no matching gx12mouse run was active."
                    : "Stopped active gx12mouse run.";
                return new RuntimeRunResult(true, message, ActiveRunRemaining: false);
            }

            return new RuntimeRunResult(
                false,
                $"Stop signal sent, but {remainingCount} matching gx12mouse process(es) are still running.",
                ActiveRunRemaining: true);
        }
        catch (Exception exception)
        {
            return new RuntimeRunResult(false, $"Stop failed: {exception.Message}", ActiveRunRemaining: true);
        }
    }

    public void StopActiveRunOnExit(AppPaths paths)
    {
        StopActiveRun(paths, timeoutMilliseconds: 50);
    }

    public static string BuildTrainerCommandLine(
        AppPaths paths,
        string profilePath,
        string recordingPath = "",
        int recordingDurationSeconds = 0,
        bool liveReload = true,
        string recordingToggleKey = "",
        bool playbackLoop = false,
        IReadOnlyList<PlaybackBindCommand>? playbackBindings = null,
        bool recordingOverwrite = false,
        string runtimeControlPath = "")
    {
        var arguments = new List<string>
        {
            "--trainer-profile",
            profilePath
        };
        if (liveReload)
        {
            arguments.Add("live");
        }

        if (!string.IsNullOrWhiteSpace(recordingPath))
        {
            arguments.Add("--recording");
            arguments.Add(recordingPath.Trim());
            if (recordingDurationSeconds > 0)
            {
                arguments.Add($"--record-duration={recordingDurationSeconds.ToString(CultureInfo.InvariantCulture)}");
            }

            var normalizedToggleKey = NormalizeOptionalKey(recordingToggleKey);
            if (!string.IsNullOrWhiteSpace(normalizedToggleKey))
            {
                arguments.Add($"--record-toggle={normalizedToggleKey}");
            }

            if (recordingOverwrite)
            {
                arguments.Add("--record-overwrite");
            }
        }

        if (!string.IsNullOrWhiteSpace(runtimeControlPath))
        {
            arguments.Add("--runtime-control");
            arguments.Add(runtimeControlPath.Trim());
        }

        var activeBindings = NormalizePlaybackBindings(playbackBindings);
        if (activeBindings.Count > 0)
        {
            arguments.Add(playbackLoop ? "--playback-loop" : "--playback-once");
            foreach (var binding in activeBindings)
            {
                arguments.Add(binding.BlockLiveInput ? "--bind-block" : "--bind");
                arguments.Add(binding.Trigger);
                arguments.Add(binding.ChannelMask);
                arguments.Add(binding.RecordingPath);
            }
        }

        return Gx12DiagnosticsService.BuildCommandLine(
            paths.ExePath,
            arguments);
    }

    public static string BuildTrainerRecordCommandLine(
        AppPaths paths,
        string profilePath,
        string recordingPath,
        int durationSeconds,
        bool liveReload,
        string recordingToggleKey = "",
        bool recordingOverwrite = false,
        string runtimeControlPath = "")
    {
        var arguments = new List<string>
        {
            "--trainer-record",
            profilePath,
            recordingPath,
            Math.Max(1, durationSeconds).ToString(CultureInfo.InvariantCulture)
        };
        if (liveReload)
        {
            arguments.Add("live");
        }

        var normalizedToggleKey = NormalizeOptionalKey(recordingToggleKey);
        if (!string.IsNullOrWhiteSpace(normalizedToggleKey))
        {
            arguments.Add($"--record-toggle={normalizedToggleKey}");
        }

        if (recordingOverwrite)
        {
            arguments.Add("--record-overwrite");
        }

        if (!string.IsNullOrWhiteSpace(runtimeControlPath))
        {
            arguments.Add("--runtime-control");
            arguments.Add(runtimeControlPath.Trim());
        }

        return Gx12DiagnosticsService.BuildCommandLine(paths.ExePath, arguments);
    }

    public static string BuildTrainerPlaybackCommandLine(
        AppPaths paths,
        string recordingPath,
        string port,
        bool loop,
        string channelMask,
        string trigger)
    {
        var arguments = new List<string>
        {
            "--trainer-playback",
            recordingPath,
            NormalizePort(port),
            loop ? "loop" : "once",
            $"--channels={channelMask}"
        };

        var normalizedTrigger = NormalizeOptionalKey(trigger);
        if (!string.IsNullOrWhiteSpace(normalizedTrigger))
        {
            arguments.Add($"--trigger={normalizedTrigger}");
        }

        return Gx12DiagnosticsService.BuildCommandLine(paths.ExePath, arguments);
    }

    public static string BuildTrainerPlaybackBankCommandLine(
        AppPaths paths,
        string port,
        bool loop,
        IReadOnlyList<PlaybackBindCommand> bindings)
    {
        var arguments = new List<string>
        {
            "--trainer-playback-bank",
            NormalizePort(port),
            loop ? "loop" : "once"
        };

        foreach (var binding in NormalizePlaybackBindings(bindings))
        {
            arguments.Add(binding.BlockLiveInput ? "--bind-block" : "--bind");
            arguments.Add(binding.Trigger);
            arguments.Add(binding.ChannelMask);
            arguments.Add(binding.RecordingPath);
        }

        return Gx12DiagnosticsService.BuildCommandLine(paths.ExePath, arguments);
    }

    public static string BuildRecordingInfoCommandLine(AppPaths paths, string recordingPath)
    {
        return Gx12DiagnosticsService.BuildCommandLine(
            paths.ExePath,
            new[] { "--recording-info", recordingPath });
    }

    private static string BuildConsoleCommand(AppPaths paths, string profilePath)
    {
        var commandLine = BuildTrainerCommandLine(paths, profilePath);
        return BuildConsoleCommand(paths, CompositeTrainerTitle, commandLine, $"Profile: {QuoteForCmd(profilePath)}");
    }

    private static string BuildConsoleCommand(
        AppPaths paths,
        string title,
        string commandLine,
        string subjectLine)
    {
        return string.Join(
            Environment.NewLine,
            new[]
            {
                "@echo off",
                $"title {title}",
                $"cd /d {QuoteForCmd(paths.RepoRoot)}",
                "if errorlevel 1 goto gx12_after_run",
                $"echo {title}",
                $"echo {subjectLine}",
                "echo.",
                commandLine,
                ":gx12_after_run",
                "set \"GX12_RESULT=%ERRORLEVEL%\"",
                "echo.",
                "echo gx12mouse exited with code %GX12_RESULT%.",
                "echo.",
                "echo You can close this console or return to the WPF launcher.",
                "exit /b %GX12_RESULT%"
            }) + Environment.NewLine;
    }

    public static string BuildConsoleScriptText(AppPaths paths, string profilePath)
    {
        return BuildConsoleCommand(paths, profilePath);
    }

    public static string GetConsoleScriptPath(AppPaths paths)
    {
        return Path.Combine(paths.RepoRoot, "logs", "gx12-wpf-composite-trainer.cmd");
    }

    public static string GetRecordingConsoleScriptPath(AppPaths paths)
    {
        return Path.Combine(paths.RepoRoot, "logs", "gx12-wpf-recording.cmd");
    }

    public static string GetPlaybackConsoleScriptPath(AppPaths paths)
    {
        return Path.Combine(paths.RepoRoot, "logs", "gx12-wpf-playback.cmd");
    }

    public static string GetPlaybackBankConsoleScriptPath(AppPaths paths)
    {
        return Path.Combine(paths.RepoRoot, "logs", "gx12-wpf-playback-bank.cmd");
    }

    public static string GetRuntimeControlPath(AppPaths paths)
    {
        return Path.Combine(paths.RepoRoot, ".gx12-ui", "runtime-control.tsv");
    }

    public static string BuildRuntimeControlText(
        string recordingPath,
        int recordingDurationSeconds,
        string recordingToggleKey,
        bool recordingOverwrite,
        bool playbackLoop,
        IReadOnlyList<PlaybackBindCommand>? playbackBindings)
    {
        var lines = new List<string>
        {
            "# gx12_runtime_control=1",
            $"recording_path\t{SanitizeRuntimeControlField(recordingPath)}",
            $"record_duration\t{Math.Max(0, recordingDurationSeconds).ToString(CultureInfo.InvariantCulture)}",
            $"record_toggle\t{SanitizeRuntimeControlField(NormalizeOptionalKey(recordingToggleKey))}",
            $"record_overwrite\t{(recordingOverwrite ? "1" : "0")}",
            $"playback_loop\t{(playbackLoop ? "1" : "0")}"
        };

        foreach (var binding in NormalizePlaybackBindings(playbackBindings))
        {
            lines.Add(
                "bind\t" +
                SanitizeRuntimeControlField(binding.Trigger) + "\t" +
                SanitizeRuntimeControlField(binding.ChannelMask) + "\t" +
                SanitizeRuntimeControlField(binding.RecordingPath) + "\t" +
                (binding.BlockLiveInput ? "1" : "0"));
        }

        return string.Join(Environment.NewLine, lines) + Environment.NewLine;
    }

    public void WriteRuntimeControlFile(
        AppPaths paths,
        string recordingPath,
        int recordingDurationSeconds,
        string recordingToggleKey,
        bool recordingOverwrite,
        bool playbackLoop,
        IReadOnlyList<PlaybackBindCommand>? playbackBindings)
    {
        var controlPath = GetRuntimeControlPath(paths);
        Directory.CreateDirectory(Path.GetDirectoryName(controlPath) ?? ".");
        var text = BuildRuntimeControlText(
            recordingPath,
            recordingDurationSeconds,
            recordingToggleKey,
            recordingOverwrite,
            playbackLoop,
            playbackBindings);
        var tempPath = controlPath + ".tmp";
        File.WriteAllText(tempPath, text, Encoding.UTF8);
        File.Move(tempPath, controlPath, overwrite: true);
    }

    private static string SanitizeRuntimeControlField(string? value)
    {
        return (value ?? string.Empty)
            .Replace('\t', ' ')
            .Replace('\r', ' ')
            .Replace('\n', ' ')
            .Trim();
    }

    private static string PrepareConsoleScript(AppPaths paths, string profilePath)
    {
        var scriptPath = GetConsoleScriptPath(paths);
        Directory.CreateDirectory(Path.GetDirectoryName(scriptPath) ?? ".");
        File.WriteAllText(scriptPath, BuildConsoleScriptText(paths, profilePath));
        return scriptPath;
    }

    private static string PrepareConsoleScript(
        AppPaths paths,
        string scriptPath,
        string title,
        string commandLine,
        string subjectLine)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(scriptPath) ?? ".");
        File.WriteAllText(scriptPath, BuildConsoleCommand(paths, title, commandLine, subjectLine));
        return scriptPath;
    }

    private static string QuoteForCmd(string value)
    {
        return $"\"{value.Replace("\"", "\"\"", StringComparison.Ordinal)}\"";
    }

    private static EventWaitHandle OpenLauncherStopEvent()
    {
        return new EventWaitHandle(
            false,
            EventResetMode.ManualReset,
            StopEventName);
    }

    private static void ResetLauncherStopEvent()
    {
        using var stopEvent = OpenLauncherStopEvent();
        stopEvent.Reset();
    }

    private RuntimeRunResult StartManagedConsole(
        AppPaths paths,
        string title,
        string scriptPath,
        string commandLine,
        string subjectLine,
        string successMessage)
    {
        ResetLauncherStopEvent();
        var consoleScriptPath = PrepareConsoleScript(paths, scriptPath, title, commandLine, subjectLine);
        var process = new Process
        {
            StartInfo = new ProcessStartInfo
            {
                FileName = "cmd.exe",
                WorkingDirectory = paths.RepoRoot,
                UseShellExecute = false,
                CreateNoWindow = false,
                WindowStyle = ProcessWindowStyle.Normal
            },
            EnableRaisingEvents = true
        };

        process.StartInfo.ArgumentList.Add("/d");
        process.StartInfo.ArgumentList.Add("/k");
        process.StartInfo.ArgumentList.Add(consoleScriptPath);

        if (!process.Start())
        {
            return new RuntimeRunResult(false, $"{title} console did not start.", commandLine);
        }

        DisposeActiveConsoleProcess();
        _activeConsoleProcess = process;
        return new RuntimeRunResult(true, successMessage, commandLine, ActiveRunRemaining: true);
    }

    private static void EnsureOutputDirectory(AppPaths paths, string recordingPath)
    {
        var fullPath = ResolveRepoPath(paths, recordingPath);
        var directory = Path.GetDirectoryName(fullPath);
        if (!string.IsNullOrWhiteSpace(directory))
        {
            Directory.CreateDirectory(directory);
        }
    }

    private static string ResolveRepoPath(AppPaths paths, string value)
    {
        return Path.IsPathRooted(value)
            ? value
            : Path.Combine(paths.RepoRoot, value);
    }

    private static string NormalizePort(string port)
    {
        return string.IsNullOrWhiteSpace(port) ? "auto" : port.Trim();
    }

    private static string NormalizeOptionalKey(string trigger)
    {
        if (string.IsNullOrWhiteSpace(trigger))
        {
            return "";
        }

        var normalized = trigger.Trim();
        return normalized.Equals("immediate", StringComparison.OrdinalIgnoreCase) ||
               normalized.Equals("none", StringComparison.OrdinalIgnoreCase) ||
               normalized.Equals("off", StringComparison.OrdinalIgnoreCase)
            ? ""
            : normalized;
    }

    private static IReadOnlyList<PlaybackBindCommand> NormalizePlaybackBindings(IReadOnlyList<PlaybackBindCommand>? bindings)
    {
        if (bindings is null || bindings.Count == 0)
        {
            return Array.Empty<PlaybackBindCommand>();
        }

        return bindings
            .Where(binding =>
                !string.IsNullOrWhiteSpace(binding.RecordingPath) &&
                !string.IsNullOrWhiteSpace(binding.Trigger) &&
                !string.IsNullOrWhiteSpace(binding.ChannelMask))
            .Take(UiSettingsService.MaxPlaybackBindingSlots)
            .Select(binding => new PlaybackBindCommand(
                binding.RecordingPath.Trim(),
                binding.Trigger.Trim(),
                binding.ChannelMask.Trim(),
                binding.BlockLiveInput))
            .ToList();
    }

    private static int WaitForManagedGx12Exit(AppPaths paths, int timeoutMilliseconds)
    {
        var deadline = DateTime.UtcNow.AddMilliseconds(Math.Max(0, timeoutMilliseconds));
        int remaining;
        do
        {
            remaining = CountManagedGx12Processes(paths);
            if (remaining == 0)
            {
                return 0;
            }

            Thread.Sleep(100);
        } while (DateTime.UtcNow < deadline);

        return remaining;
    }

    private static int CountManagedGx12Processes(AppPaths paths)
    {
        var expectedPath = Path.GetFullPath(paths.ExePath);
        var processName = Path.GetFileNameWithoutExtension(paths.ExePath);
        var count = 0;

        foreach (var process in Process.GetProcessesByName(processName))
        {
            using (process)
            {
                if (IsSameExecutable(process, expectedPath))
                {
                    count++;
                }
            }
        }

        return count;
    }

    private static bool IsSameExecutable(Process process, string expectedPath)
    {
        try
        {
            var processPath = process.MainModule?.FileName;
            return processPath is not null &&
                   string.Equals(
                       Path.GetFullPath(processPath),
                       expectedPath,
                       StringComparison.OrdinalIgnoreCase);
        }
        catch
        {
            return false;
        }
    }

    private void StopActiveConsoleProcess()
    {
        var process = _activeConsoleProcess;
        _activeConsoleProcess = null;
        if (process is null)
        {
            return;
        }

        try
        {
            process.Refresh();
            if (!process.HasExited)
            {
                process.Kill(entireProcessTree: true);
            }
        }
        catch
        {
        }
        finally
        {
            process.Dispose();
        }
    }

    private void DisposeActiveConsoleProcess()
    {
        var process = _activeConsoleProcess;
        _activeConsoleProcess = null;
        process?.Dispose();
    }
}
