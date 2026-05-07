using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text.RegularExpressions;
using System.Xml.Linq;
using Gx12.Launcher.Wpf.Models;

namespace Gx12.Launcher.Wpf.Services;

public sealed record ReleaseInfo(
    string ProductName,
    string Version,
    string Channel,
    string TargetFramework,
    string RuntimeStatus,
    string PrimaryLauncherName,
    string WpfLauncherFileName,
    string WpfLauncherPath,
    string PublishCommand,
    string SettingsBehavior,
    string ParityGateStatus,
    IReadOnlyList<ReleaseCheckItem> ReadinessChecks)
{
    public string DisplayVersion => $"{ProductName} {Version}";

    public string ShortStatus => $"{Channel} / {RuntimeStatus}";

    public string ReadinessSummary
    {
        get
        {
            var blockers = ReadinessChecks.Count(check => check.Kind.Equals("Danger", System.StringComparison.Ordinal));
            var warnings = ReadinessChecks.Count(check => check.Kind.Equals("Warn", System.StringComparison.Ordinal));
            if (blockers > 0)
            {
                return $"{blockers} release blocker(s), {warnings} warning(s)";
            }

            return warnings > 0
                ? $"{warnings} release warning(s), no blockers"
                : "Release checks clear for the current preview gate";
        }
    }
}

public static class ReleaseInfoService
{
    public const string ProductName = "Asukaflight";
    public const string Channel = "Primary";
    public const string TargetFramework = "net7.0-windows";
    public const string RuntimeIdentifier = "win-x64";
    public const string PublishedAppHostFileName = "Asukaflight.exe";
    public const string PublishedAppPayloadFileName = "Asukaflight.dll";
    public const string ReleaseLauncherFileName = "Asukaflight.exe";
    public const string PrimaryLauncherName = ReleaseLauncherFileName;
    public const string WpfLauncherFileName = ReleaseLauncherFileName;
    public const string DevLauncherFileName = "Start GX12 Launcher WPF.bat";
    public const string FallbackLauncherFileName = "Start GX12 Launcher V3.bat";
    public const string PackageDirectoryPrefix = "Asukaflight";

    public static ReleaseInfo Create(AppPaths paths)
    {
        var version = ResolveVersion();
        var isPackagedRoot = ProfileDirectoryService.HasReleaseMarker(paths.RepoRoot);
        var packageRoot = isPackagedRoot
            ? paths.RepoRoot
            : Path.Combine(paths.RepoRoot, "dist", BuildPackageDirectoryName(version));
        var wpfLauncherPath = Path.Combine(packageRoot, WpfLauncherFileName);
        var devLauncherPath = Path.Combine(paths.RepoRoot, DevLauncherFileName);
        var fallbackLauncherPath = Path.Combine(paths.RepoRoot, FallbackLauncherFileName);
        var releaseScriptPath = Path.Combine(paths.RepoRoot, "tools", "publish-gx12-distribution.ps1");
        var projectPath = Path.Combine(paths.RepoRoot, "apps", "Gx12.Launcher.Wpf", "Gx12.Launcher.Wpf.csproj");
        var appIconPath = Path.Combine(paths.RepoRoot, "apps", "Gx12.Launcher.Wpf", "Assets", "gx12.ico");
        var isAppIconConfigured = File.Exists(appIconPath)
            && ProjectUsesApplicationIcon(projectPath, @"Assets\gx12.ico");
        var publishOutputPath = Path.Combine(
            paths.RepoRoot,
            "apps",
            "Gx12.Launcher.Wpf",
            "bin",
            "Release",
            TargetFramework,
            RuntimeIdentifier,
            "publish",
            PublishedAppHostFileName);
        var readinessChecks = new List<ReleaseCheckItem>
        {
            Check(
                "Release exe",
                File.Exists(wpfLauncherPath),
                "Present",
                "Missing",
                wpfLauncherPath,
                wpfLauncherPath,
                "Danger"),
            Check(
                "Runtime exe",
                File.Exists(paths.ExePath),
                "Found",
                "Missing",
                paths.ExePath,
                paths.ExePath,
                "Danger"),
            Check(
                "Profiles",
                Directory.Exists(paths.ProfileDirectory),
                "Found",
                "Missing",
                paths.ProfileDirectory,
                paths.ProfileDirectory,
                "Danger")
        };

        if (isPackagedRoot)
        {
            readinessChecks.Add(Check(
                "Package marker",
                File.Exists(Path.Combine(paths.RepoRoot, ProfileDirectoryService.ReleaseMarkerFileName)),
                "Present",
                "Missing",
                Path.Combine(paths.RepoRoot, ProfileDirectoryService.ReleaseMarkerFileName),
                Path.Combine(paths.RepoRoot, ProfileDirectoryService.ReleaseMarkerFileName),
                "Danger"));
            readinessChecks.Add(Check(
                "App payload",
                File.Exists(Path.Combine(paths.RepoRoot, PublishedAppPayloadFileName)),
                "Found",
                "Missing",
                Path.Combine(paths.RepoRoot, PublishedAppPayloadFileName),
                Path.Combine(paths.RepoRoot, PublishedAppPayloadFileName),
                "Danger"));
        }
        else
        {
            readinessChecks.Add(Check(
                "Release script",
                File.Exists(releaseScriptPath),
                "Present",
                "Missing",
                releaseScriptPath,
                releaseScriptPath,
                "Danger"));
            readinessChecks.Add(Check(
                "Dev batch",
                File.Exists(devLauncherPath),
                "Present",
                "Missing",
                devLauncherPath,
                devLauncherPath,
                "Warn"));
            readinessChecks.Add(Check(
                "V3 fallback",
                File.Exists(fallbackLauncherPath),
                "Present",
                "Missing",
                fallbackLauncherPath,
                fallbackLauncherPath,
                "Warn"));
            readinessChecks.Add(Check(
                "Publish output",
                File.Exists(publishOutputPath),
                "Built",
                "Run release script",
                publishOutputPath,
                publishOutputPath,
                "Warn"));
            readinessChecks.Add(Check(
                "Package output",
                File.Exists(wpfLauncherPath),
                "Built",
                "Run release script",
                wpfLauncherPath,
                wpfLauncherPath,
                "Warn"));
            readinessChecks.Add(Check(
                "App icon",
                isAppIconConfigured,
                "Configured",
                "Pending",
                appIconPath,
                "Add apps\\Gx12.Launcher.Wpf\\Assets\\gx12.ico and set ApplicationIcon before an external release.",
                "Warn"));
        }

        return new ReleaseInfo(
            ProductName,
            version,
            Channel,
            TargetFramework,
            "Runtime start/stop enabled",
            PrimaryLauncherName,
            WpfLauncherFileName,
            wpfLauncherPath,
            @"powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools\publish-gx12-distribution.ps1",
            @"Portable release root: keep profiles, runtime, and .gx12-* markers beside Asukaflight.exe; UI settings stay under .gx12-ui.",
            "GitHub release users run Asukaflight.exe; the repo batch and V3 remain developer/fallback paths.",
            readinessChecks);
    }

    public static string BuildPackageDirectoryName(string version)
    {
        return $"{PackageDirectoryPrefix}-{SanitizePackageToken(version)}-{RuntimeIdentifier}";
    }

    private static string SanitizePackageToken(string value)
    {
        var safeChars = value
            .Select(ch => char.IsLetterOrDigit(ch) || ch is '.' or '-' ? ch : '-')
            .ToArray();
        var safe = new string(safeChars).Trim('-', '.');
        safe = Regex.Replace(safe, "(?i)(^|[.-])preview[.-]?([0-9]+)", "-p$2").Trim('-', '.');
        return string.IsNullOrWhiteSpace(safe) ? "0.0.0" : safe;
    }

    private static ReleaseCheckItem Check(
        string label,
        bool isOk,
        string okStatus,
        string missingStatus,
        string okDetail,
        string missingDetail,
        string missingKind)
    {
        return new ReleaseCheckItem(
            label,
            isOk ? okStatus : missingStatus,
            isOk ? okDetail : missingDetail,
            isOk ? "Accent" : missingKind);
    }

    private static bool ProjectUsesApplicationIcon(string projectPath, string expectedIconPath)
    {
        if (!File.Exists(projectPath))
        {
            return false;
        }

        try
        {
            var project = XDocument.Load(projectPath);
            return project.Descendants("ApplicationIcon")
                .Any(element => string.Equals(
                    element.Value.Trim(),
                    expectedIconPath,
                    System.StringComparison.OrdinalIgnoreCase));
        }
        catch
        {
            return false;
        }
    }

    private static string ResolveVersion()
    {
        var assembly = typeof(ReleaseInfoService).Assembly;
        var informational = assembly
            .GetCustomAttribute<AssemblyInformationalVersionAttribute>()?
            .InformationalVersion;
        if (!string.IsNullOrWhiteSpace(informational))
        {
            return informational;
        }

        return assembly.GetName().Version?.ToString() ?? "0.0.0";
    }
}
