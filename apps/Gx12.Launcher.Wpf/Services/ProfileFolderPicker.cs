using System.IO;
using System.Windows.Forms;

namespace Gx12.Launcher.Wpf.Services;

public sealed class ProfileFolderPicker : IProfileFolderPicker
{
    public string? PickFolder(string currentDirectory, string repoRoot)
    {
        using var dialog = new FolderBrowserDialog
        {
            Description = "Choose the folder used for GX12 tuning profiles.",
            ShowNewFolderButton = true,
            SelectedPath = Directory.Exists(currentDirectory) ? currentDirectory : repoRoot
        };

        return dialog.ShowDialog() == DialogResult.OK ? dialog.SelectedPath : null;
    }
}
