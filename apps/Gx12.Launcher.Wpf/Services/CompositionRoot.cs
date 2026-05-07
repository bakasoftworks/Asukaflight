using Gx12.Launcher.Wpf.ViewModels;

namespace Gx12.Launcher.Wpf.Services;

public static class CompositionRoot
{
    public static MainViewModel CreateMainViewModel()
    {
        var directoryService = new ProfileDirectoryService();
        var repository = new ProfileRepository(directoryService);
        return new MainViewModel(repository, new ProfileFolderPicker());
    }
}
