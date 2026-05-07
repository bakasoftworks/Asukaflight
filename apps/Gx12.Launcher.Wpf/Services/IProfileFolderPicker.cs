namespace Gx12.Launcher.Wpf.Services;

public interface IProfileFolderPicker
{
    string? PickFolder(string currentDirectory, string repoRoot);
}
