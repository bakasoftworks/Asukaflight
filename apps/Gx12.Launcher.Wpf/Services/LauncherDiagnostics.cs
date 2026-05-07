using System;
using System.IO;
using System.Linq;

namespace Gx12.Launcher.Wpf.Services;

public static class LauncherDiagnostics
{
    public static int RunSelfTest()
    {
        var directoryService = new ProfileDirectoryService();
        var repository = new ProfileRepository(directoryService);
        var paths = directoryService.Resolve();
        var releaseInfo = ReleaseInfoService.Create(paths);
        var profiles = repository.LoadProfiles(paths);
        var failures = new[]
            {
                ProfileDirectoryService.IsRecognizedRoot(paths.RepoRoot) ? null : $"Missing repo/release marker: {paths.RepoRoot}",
                Directory.Exists(paths.ProfileDirectory) ? null : $"Missing profile directory: {paths.ProfileDirectory}",
                profiles.Count > 0 ? null : "No profiles found.",
                profiles.Any(profile => profile.IsDefault) ? null : $"Default profile not found: {paths.DefaultProfileFileName}"
            }
            .Where(failure => failure is not null)
            .Cast<string>()
            .ToList();

        failures.AddRange(profiles
            .Where(profile => !string.IsNullOrWhiteSpace(profile.LoadError))
            .Select(profile => $"{profile.FileName}: {profile.LoadError}"));

        WriteSelfTestLog(paths, profiles.Count, failures);
        return failures.Count == 0 ? 0 : 1;
    }

    private static void WriteSelfTestLog(AppPaths paths, int profileCount, System.Collections.Generic.IReadOnlyList<string> failures)
    {
        try
        {
            var logPath = Path.Combine(paths.RepoRoot, "logs", "wpf-launcher-self-test.txt");
            Directory.CreateDirectory(Path.GetDirectoryName(logPath) ?? ".");
            var releaseInfo = ReleaseInfoService.Create(paths);
            var lines = new[]
            {
                $"time={DateTimeOffset.Now:O}",
                $"repo_root={paths.RepoRoot}",
                $"profile_directory={paths.ProfileDirectory}",
                $"default_profile={paths.DefaultProfileFileName}",
                $"exe_path={paths.ExePath}",
                $"release_launcher={ReleaseInfoService.WpfLauncherFileName}",
                $"release_launcher_path={releaseInfo.WpfLauncherPath}",
                $"release_version={releaseInfo.Version}",
                $"readiness_summary={releaseInfo.ReadinessSummary}",
                $"profile_count={profileCount}",
                $"status={(failures.Count == 0 ? "ok" : "failed")}"
            }
            .Concat(releaseInfo.ReadinessChecks.Select(check => $"release_check={check.Label}|{check.Status}|{check.Kind}"))
            .Concat(failures.Select(failure => $"failure={failure}"));

            File.WriteAllLines(logPath, lines);
        }
        catch
        {
        }
    }
}
