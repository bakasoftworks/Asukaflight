using System.Collections.Generic;

namespace Gx12.Launcher.Wpf.Models;

public sealed class UiSettings
{
    public Dictionary<string, string> TooltipImages { get; set; } = new();

    public bool AboveBarSpriteRandomReturnDelay { get; set; } = true;

    public int AboveBarSpriteFixedReturnDelaySeconds { get; set; } = 60;
}
