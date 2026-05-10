using System.Collections.Generic;

namespace Gx12.Launcher.Wpf.Models;

public sealed class UiSettings
{
    public string RecordingPath { get; set; } = string.Empty;

    public bool RecordingOverwrite { get; set; }

    public bool PlaybackAileron { get; set; } = true;

    public bool PlaybackElevator { get; set; } = true;

    public bool PlaybackThrottle { get; set; }

    public bool PlaybackRudder { get; set; }

    public bool PlaybackRadioRightGimbal { get; set; }

    public bool PlaybackRecordedTrainerRight { get; set; } = true;

    public bool PlaybackRadioLeftGimbal { get; set; } = true;

    public bool PlaybackRecordedTrainerLeft { get; set; }

    public bool PlaybackBlockLiveInput { get; set; }

    public Dictionary<string, string> TooltipImages { get; set; } = new();

    public List<PlaybackBindingSettings> PlaybackBindings { get; set; } = new();

    public bool AboveBarSpriteRandomReturnDelay { get; set; } = true;

    public int AboveBarSpriteFixedReturnDelaySeconds { get; set; } = 60;
}
