namespace Gx12.Launcher.Wpf.Services;

public interface IProfileValidator
{
    ProfileValidationResult Validate(AppPaths paths, string profilePath);
}
