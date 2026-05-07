namespace Gx12.Launcher.Wpf.Services;

public sealed record AppPaths(
    string RepoRoot,
    string ProfileDirectory,
    string DefaultProfileFileName,
    string ExePath,
    string ProfileDirectoryMarkerPath,
    string DefaultProfileMarkerPath);
