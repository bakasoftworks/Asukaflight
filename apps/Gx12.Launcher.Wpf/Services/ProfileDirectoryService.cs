using System;
using System.IO;
using System.Linq;

namespace Gx12.Launcher.Wpf.Services;

public sealed class ProfileDirectoryService
{
    private const string FallbackDefaultProfile = "whoop-fast.toml";
    public const string ReleaseMarkerFileName = "ASUKAFLIGHT-RELEASE.txt";
    public const string LegacyReleaseMarkerFileName = "GX12-RELEASE.txt";
    private readonly string? _explicitRepoRoot;

    public ProfileDirectoryService(string? explicitRepoRoot = null)
    {
        _explicitRepoRoot = string.IsNullOrWhiteSpace(explicitRepoRoot)
            ? null
            : Path.GetFullPath(explicitRepoRoot);
    }

    public AppPaths Resolve()
    {
        var repoRoot = _explicitRepoRoot ?? FindRepoRoot();
        var profileDirectoryMarker = Path.Combine(repoRoot, ".gx12-profile-dir");
        var defaultProfileMarker = Path.Combine(repoRoot, ".gx12-default-profile");
        var profileDirectory = ResolveProfileDirectory(repoRoot, profileDirectoryMarker);
        var defaultProfile = ResolveDefaultProfile(defaultProfileMarker);
        var exePath = Path.Combine(repoRoot, "runtime", "gx12mouse.exe");

        return new AppPaths(
            repoRoot,
            profileDirectory,
            defaultProfile,
            exePath,
            profileDirectoryMarker,
            defaultProfileMarker);
    }

    public AppPaths SetProfileDirectory(string path)
    {
        var paths = Resolve();
        var resolved = ResolveProfileDirectoryPath(paths.RepoRoot, path);
        Directory.CreateDirectory(resolved);
        File.WriteAllText(paths.ProfileDirectoryMarkerPath, resolved + Environment.NewLine, System.Text.Encoding.ASCII);
        return Resolve();
    }

    public AppPaths SetDefaultProfileFileName(string fileName)
    {
        var paths = Resolve();
        var defaultFileName = Path.GetFileName(fileName);
        if (string.IsNullOrWhiteSpace(defaultFileName))
        {
            throw new ArgumentException("Default profile filename is required.", nameof(fileName));
        }

        File.WriteAllText(paths.DefaultProfileMarkerPath, defaultFileName + Environment.NewLine, System.Text.Encoding.ASCII);
        return Resolve();
    }

    private static string FindRepoRoot()
    {
        var starts = new[]
            {
                Directory.GetCurrentDirectory(),
                AppContext.BaseDirectory
            }
            .Where(path => !string.IsNullOrWhiteSpace(path))
            .Select(Path.GetFullPath)
            .Distinct(StringComparer.OrdinalIgnoreCase);

        foreach (var start in starts)
        {
            var directory = new DirectoryInfo(start);
            while (directory is not null)
            {
                if (IsRecognizedRoot(directory.FullName))
                {
                    return directory.FullName;
                }

                directory = directory.Parent;
            }
        }

        return Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "..", "..", "..", ".."));
    }

    public static bool IsRecognizedRoot(string directory)
    {
        if (string.IsNullOrWhiteSpace(directory))
        {
            return false;
        }

        var root = Path.GetFullPath(directory);
        if (!Directory.Exists(Path.Combine(root, "profiles")))
        {
            return false;
        }

        return HasSourceRootMarker(root) ||
               HasReleaseMarker(root) ||
               File.Exists(Path.Combine(root, "runtime", "gx12mouse.exe"));
    }

    private static bool HasSourceRootMarker(string root)
    {
        return File.Exists(Path.Combine(root, "CMakeLists.txt")) &&
               File.Exists(Path.Combine(root, "apps", "Gx12.Launcher.Wpf", "Gx12.Launcher.Wpf.csproj"));
    }

    public static bool HasReleaseMarker(string directory)
    {
        if (string.IsNullOrWhiteSpace(directory))
        {
            return false;
        }

        var root = Path.GetFullPath(directory);
        return File.Exists(Path.Combine(root, ReleaseMarkerFileName)) ||
               File.Exists(Path.Combine(root, LegacyReleaseMarkerFileName));
    }

    private static string ResolveProfileDirectory(string repoRoot, string markerPath)
    {
        if (File.Exists(markerPath))
        {
            var stored = File.ReadLines(markerPath).FirstOrDefault();
            if (!string.IsNullOrWhiteSpace(stored))
            {
                return ResolveProfileDirectoryPath(repoRoot, stored);
            }
        }

        return ResolveProfileDirectoryPath(repoRoot, "profiles");
    }

    private static string ResolveProfileDirectoryPath(string repoRoot, string value)
    {
        var expanded = Environment.ExpandEnvironmentVariables(value.Trim().Trim('"'));
        if (string.IsNullOrWhiteSpace(expanded))
        {
            expanded = "profiles";
        }

        if (!Path.IsPathRooted(expanded))
        {
            expanded = Path.Combine(repoRoot, expanded);
        }

        return Path.GetFullPath(expanded);
    }

    private static string ResolveDefaultProfile(string markerPath)
    {
        if (File.Exists(markerPath))
        {
            var stored = File.ReadLines(markerPath).FirstOrDefault();
            if (!string.IsNullOrWhiteSpace(stored))
            {
                return Path.GetFileName(stored.Trim());
            }
        }

        return FallbackDefaultProfile;
    }
}
