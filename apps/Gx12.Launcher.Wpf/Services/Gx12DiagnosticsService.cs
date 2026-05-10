using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace Gx12.Launcher.Wpf.Services;

public sealed class Gx12DiagnosticsService
{
    public const string MouseDevicesGameInputDiagnosticName = "GameInput mouse devices";

    private const int ProcessExitDrainMilliseconds = 1500;

    public Gx12DiagnosticCommand BuildMouseDevicesGameInput(AppPaths paths, int seconds)
    {
        return BuildCommand(
            MouseDevicesGameInputDiagnosticName,
            new[] { "--mouse-devices-gameinput", seconds.ToString(System.Globalization.CultureInfo.InvariantCulture) },
            timeoutMilliseconds: Math.Max(5000, (seconds + 3) * 1000),
            paths);
    }

    public Gx12DiagnosticCommand BuildShowProfile(AppPaths paths, string profilePath)
    {
        return BuildCommand(
            "Show profile",
            new[] { "--show-profile", profilePath },
            timeoutMilliseconds: 10000,
            paths);
    }

    public Gx12DiagnosticCommand BuildMouseLeftDryRun(AppPaths paths, string profilePath, int seconds)
    {
        return BuildCommand(
            "Second mouse dry run",
            new[] { "--mouse-left-dry-run", profilePath, seconds.ToString(System.Globalization.CultureInfo.InvariantCulture) },
            timeoutMilliseconds: Math.Max(5000, (seconds + 3) * 1000),
            paths);
    }

    public Gx12DiagnosticCommand BuildGimbalPreview(AppPaths paths, string profilePath)
    {
        return BuildCommand(
            "Gimbal preview",
            new[] { "--gimbal-preview", profilePath },
            timeoutMilliseconds: 10000,
            paths);
    }

    public Gx12DiagnosticCommand BuildRecordingInfo(AppPaths paths, string recordingPath)
    {
        return BuildCommand(
            "Recording info",
            new[] { "--recording-info", recordingPath },
            timeoutMilliseconds: 10000,
            paths);
    }

    public Gx12DiagnosticResult RunGimbalPreview(AppPaths paths, string profilePath, int timeoutMilliseconds = 10000)
    {
        var command = BuildCommand(
            "Gimbal preview",
            new[] { "--gimbal-preview", profilePath },
            timeoutMilliseconds,
            paths);
        return RunAsync(paths, command, CancellationToken.None).GetAwaiter().GetResult();
    }

    public async Task<Gx12DiagnosticResult> RunAsync(
        AppPaths paths,
        Gx12DiagnosticCommand command,
        CancellationToken cancellationToken)
    {
        var startedAt = DateTimeOffset.Now;
        if (!File.Exists(paths.ExePath))
        {
            return Complete(command, startedAt, false, "gx12mouse.exe is missing.", "");
        }

        var profilePath = FindProfileArgument(command.Arguments);
        if (profilePath is not null && !File.Exists(profilePath))
        {
            return Complete(command, startedAt, false, "Profile file is missing.", "");
        }

        var recordingPath = FindRecordingInfoArgument(command.Arguments);
        if (recordingPath is not null && !File.Exists(ResolveRepoPath(paths, recordingPath)))
        {
            return Complete(command, startedAt, false, "Recording file is missing.", "");
        }

        try
        {
            using var process = new Process();
            process.StartInfo = new ProcessStartInfo
            {
                FileName = paths.ExePath,
                WorkingDirectory = paths.RepoRoot,
                UseShellExecute = false,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                CreateNoWindow = true
            };

            foreach (var argument in command.Arguments)
            {
                process.StartInfo.ArgumentList.Add(argument);
            }

            if (!process.Start())
            {
                return Complete(command, startedAt, false, "Diagnostic process did not start.", "");
            }

            var stdoutTask = process.StandardOutput.ReadToEndAsync();
            var stderrTask = process.StandardError.ReadToEndAsync();
            var exitTask = process.WaitForExitAsync();
            var waitTask = Task.Delay(command.TimeoutMilliseconds, cancellationToken);
            var completed = await Task.WhenAny(exitTask, waitTask).ConfigureAwait(false);
            if (completed != exitTask)
            {
                TryKill(process);
                await WaitForExitQuietly(process).ConfigureAwait(false);
                var partialOutput = await DrainOutput(stdoutTask, stderrTask).ConfigureAwait(false);
                var message = cancellationToken.IsCancellationRequested
                    ? "Diagnostic stopped."
                    : "Diagnostic timed out.";
                return Complete(command, startedAt, false, message, partialOutput);
            }

            var output = await DrainOutput(stdoutTask, stderrTask).ConfigureAwait(false);
            return process.ExitCode == 0
                ? Complete(command, startedAt, true, $"{command.Name} complete.", output)
                : Complete(command, startedAt, false, $"{command.Name} failed with exit code {process.ExitCode}.", output);
        }
        catch (Exception exception)
        {
            return Complete(command, startedAt, false, exception.Message, "");
        }
    }

    private static Gx12DiagnosticResult Complete(
        Gx12DiagnosticCommand command,
        DateTimeOffset startedAt,
        bool isSuccess,
        string message,
        string output)
    {
        return new Gx12DiagnosticResult(
            isSuccess,
            message,
            command.CommandLine,
            output,
            command.Name,
            startedAt,
            DateTimeOffset.Now);
    }

    private static Gx12DiagnosticCommand BuildCommand(
        string name,
        IReadOnlyList<string> arguments,
        int timeoutMilliseconds,
        AppPaths paths)
    {
        return new Gx12DiagnosticCommand(
            name,
            arguments,
            BuildCommandLine(paths.ExePath, arguments),
            timeoutMilliseconds);
    }

    public static string BuildCommandLine(string executablePath, IEnumerable<string> arguments)
    {
        var builder = new StringBuilder();
        builder.Append(QuoteForCommandLine(executablePath));
        foreach (var argument in arguments)
        {
            builder.Append(' ');
            builder.Append(QuoteForCommandLine(argument));
        }

        return builder.ToString();
    }

    private static string QuoteForCommandLine(string value)
    {
        if (string.IsNullOrEmpty(value))
        {
            return "\"\"";
        }

        return value.IndexOfAny(new[] { ' ', '\t', '"' }) >= 0
            ? $"\"{value.Replace("\"", "\\\"", StringComparison.Ordinal)}\""
            : value;
    }

    private static string? FindProfileArgument(IReadOnlyList<string> arguments)
    {
        for (var index = 0; index < arguments.Count - 1; index++)
        {
            var argument = arguments[index];
            if (argument.Equals("--show-profile", StringComparison.OrdinalIgnoreCase) ||
                argument.Equals("--mouse-left-dry-run", StringComparison.OrdinalIgnoreCase) ||
                argument.Equals("--gimbal-preview", StringComparison.OrdinalIgnoreCase))
            {
                return arguments[index + 1];
            }
        }

        return null;
    }

    private static string? FindRecordingInfoArgument(IReadOnlyList<string> arguments)
    {
        for (var index = 0; index < arguments.Count - 1; index++)
        {
            if (arguments[index].Equals("--recording-info", StringComparison.OrdinalIgnoreCase))
            {
                return arguments[index + 1];
            }
        }

        return null;
    }

    private static string ResolveRepoPath(AppPaths paths, string value)
    {
        return Path.IsPathRooted(value)
            ? value
            : Path.Combine(paths.RepoRoot, value);
    }

    private static async Task<string> DrainOutput(Task<string> stdoutTask, Task<string> stderrTask)
    {
        var stdout = await stdoutTask.ConfigureAwait(false);
        var stderr = await stderrTask.ConfigureAwait(false);
        return CombineOutput(stdout, stderr);
    }

    private static string CombineOutput(string stdout, string stderr)
    {
        if (string.IsNullOrWhiteSpace(stderr))
        {
            return stdout;
        }

        if (string.IsNullOrWhiteSpace(stdout))
        {
            return stderr;
        }

        var builder = new StringBuilder();
        builder.Append(stdout.TrimEnd());
        builder.AppendLine();
        builder.AppendLine();
        builder.AppendLine("[stderr]");
        builder.Append(stderr);
        return builder.ToString();
    }

    private static void TryKill(Process process)
    {
        try
        {
            process.Kill(entireProcessTree: true);
        }
        catch
        {
        }
    }

    private static async Task WaitForExitQuietly(Process process)
    {
        try
        {
            var exitTask = process.WaitForExitAsync();
            var completed = await Task.WhenAny(exitTask, Task.Delay(ProcessExitDrainMilliseconds)).ConfigureAwait(false);
            if (completed == exitTask)
            {
                await exitTask.ConfigureAwait(false);
            }
        }
        catch
        {
        }
    }
}
