using System;
using System.Diagnostics;
using System.IO;
using System.Text;

namespace Gx12.Launcher.Wpf.Services;

public sealed class Gx12EngineProfileValidator : IProfileValidator
{
    private const int ValidationTimeoutMs = 8000;

    public ProfileValidationResult Validate(AppPaths paths, string profilePath)
    {
        if (!File.Exists(paths.ExePath))
        {
            return ProfileValidationResult.Skipped($"gx12mouse.exe missing: {paths.ExePath}");
        }

        if (!File.Exists(profilePath))
        {
            return ProfileValidationResult.Failure($"Profile file missing: {profilePath}");
        }

        try
        {
            var output = new StringBuilder();
            using var process = new Process
            {
                StartInfo = new ProcessStartInfo
                {
                    FileName = paths.ExePath,
                    UseShellExecute = false,
                    RedirectStandardOutput = true,
                    RedirectStandardError = true,
                    CreateNoWindow = true,
                    WorkingDirectory = paths.RepoRoot
                },
                EnableRaisingEvents = false
            };

            process.StartInfo.ArgumentList.Add("--show-profile");
            process.StartInfo.ArgumentList.Add(profilePath);

            process.OutputDataReceived += (_, args) =>
            {
                if (args.Data is not null)
                {
                    output.AppendLine(args.Data);
                }
            };
            process.ErrorDataReceived += (_, args) =>
            {
                if (args.Data is not null)
                {
                    output.AppendLine(args.Data);
                }
            };

            process.Start();
            process.BeginOutputReadLine();
            process.BeginErrorReadLine();

            if (!process.WaitForExit(ValidationTimeoutMs))
            {
                try
                {
                    process.Kill(entireProcessTree: true);
                }
                catch
                {
                }

                return ProfileValidationResult.Failure("Profile validation timed out.");
            }

            process.WaitForExit();

            if (process.ExitCode == 0)
            {
                return ProfileValidationResult.Success("Profile validated with gx12mouse.exe --show-profile.");
            }

            var message = output.ToString().Trim();
            if (string.IsNullOrWhiteSpace(message))
            {
                message = $"gx12mouse.exe --show-profile failed with exit code {process.ExitCode}.";
            }

            return ProfileValidationResult.Failure(message);
        }
        catch (Exception exception)
        {
            return ProfileValidationResult.Failure(exception.Message);
        }
    }
}
