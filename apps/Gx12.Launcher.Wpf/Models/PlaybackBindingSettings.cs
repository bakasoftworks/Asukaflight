namespace Gx12.Launcher.Wpf.Models;

public sealed class PlaybackBindingSettings
{
    public bool Enabled { get; set; } = true;

    public string RecordingPath { get; set; } = string.Empty;

    public string Trigger { get; set; } = "F5";

    public string ChannelMask { get; set; } = "ail,ele";

    public bool BlockLiveInput { get; set; }
}
