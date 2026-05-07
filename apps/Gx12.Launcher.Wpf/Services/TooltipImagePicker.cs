using System.IO;
using Microsoft.Win32;

namespace Gx12.Launcher.Wpf.Services;

public sealed class TooltipImagePicker : ITooltipImagePicker
{
    public string? PickTooltipImage(string initialDirectory)
    {
        var dialog = new OpenFileDialog
        {
            CheckFileExists = true,
            Filter = "Image files (*.png;*.jpg;*.jpeg;*.bmp)|*.png;*.jpg;*.jpeg;*.bmp|All files (*.*)|*.*",
            InitialDirectory = Directory.Exists(initialDirectory) ? initialDirectory : "",
            Multiselect = false,
            Title = "Choose a tooltip image"
        };

        return dialog.ShowDialog() == true ? dialog.FileName : null;
    }
}
