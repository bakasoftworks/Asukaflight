using System;
using System.Diagnostics;
using System.IO;
using System.Threading;

namespace Gx12.Launcher.Wpf.Services;

public sealed record RuntimeRunResult(
    bool IsSuccess,
    string Message,
    string CommandLine = "",
    bool ActiveRunRemaining = false);

public sealed class Gx12RuntimeService
{
    public const string StopEventName = @"Local\GX12MouseLauncherStop";

    private const string CompositeTrainerTitle = "GX12 Composite Trainer";
    private Process? _activeConsoleProcess;

    public RuntimeRunResult StartCompositeTrainer(AppPaths paths, string profilePath)
    {
        var commandLine = BuildTrainerCommandLine(paths, profilePath);
        if (!File.Exists(paths.ExePath))
        {
            return new RuntimeRunResult(false, $"Missing executable: {paths.ExePath}", commandLine);
        }

        if (!File.Exists(profilePath))
        {
            return new RuntimeRunResult(false, $"Missing profile: {profilePath}", commandLine);
        }

        var stopped = StopActiveRun(paths);
        if (!stopped.IsSuccess)
        {
            return stopped with { CommandLine = commandLine };
        }

        try
        {
            ResetLauncherStopEvent();
            var consoleScriptPath = PrepareConsoleScript(paths, profilePath);
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
                return new RuntimeRunResult(false, "Composite trainer console did not start.", commandLine);
            }

            DisposeActiveConsoleProcess();
            _activeConsoleProcess = process;
            return new RuntimeRunResult(
                true,
                $"Running {CompositeTrainerTitle} with {Path.GetFileName(profilePath)}. Starting another profile will stop this run first.",
                commandLine,
                ActiveRunRemaining: true);
        }
        catch (Exception exception)
        {
            return new RuntimeRunResult(false, $"Start failed: {exception.Message}", commandLine);
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

    public static string BuildTrainerCommandLine(AppPaths paths, string profilePath)
    {
        return Gx12DiagnosticsService.BuildCommandLine(
            paths.ExePath,
            new[] { "--trainer-profile", profilePath, "live" });
    }

    private static string BuildConsoleCommand(AppPaths paths, string profilePath)
    {
        var commandLine = BuildTrainerCommandLine(paths, profilePath);
        return string.Join(
            Environment.NewLine,
            new[]
            {
                "@echo off",
                $"title {CompositeTrainerTitle}",
                $"cd /d {QuoteForCmd(paths.RepoRoot)}",
                "if errorlevel 1 goto gx12_after_run",
                $"echo {CompositeTrainerTitle}",
                $"echo Profile: {QuoteForCmd(profilePath)}",
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

    private static string PrepareConsoleScript(AppPaths paths, string profilePath)
    {
        var scriptPath = GetConsoleScriptPath(paths);
        Directory.CreateDirectory(Path.GetDirectoryName(scriptPath) ?? ".");
        File.WriteAllText(scriptPath, BuildConsoleScriptText(paths, profilePath));
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
