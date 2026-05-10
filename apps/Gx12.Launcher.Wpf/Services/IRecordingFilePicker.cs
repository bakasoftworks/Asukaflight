namespace Gx12.Launcher.Wpf.Services;

public interface IRecordingFilePicker
{
    string? PickRecordingOutput(string currentPath, string repoRoot);

    string? PickPlaybackRecording(string currentPath, string repoRoot);
}
