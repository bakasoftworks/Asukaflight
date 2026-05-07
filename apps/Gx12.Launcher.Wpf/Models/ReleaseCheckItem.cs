namespace Gx12.Launcher.Wpf.Models;

public sealed record ReleaseCheckItem(
    string Label,
    string Status,
    string Detail,
    string Kind);
