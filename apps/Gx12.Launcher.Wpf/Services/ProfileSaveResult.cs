namespace Gx12.Launcher.Wpf.Services;

public sealed record ProfileSaveResult(
    bool IsSuccess,
    bool Changed,
    string Message,
    ProfileValidationResult Validation)
{
    public static ProfileSaveResult Success(bool changed, string message, ProfileValidationResult validation)
    {
        return new ProfileSaveResult(true, changed, message, validation);
    }

    public static ProfileSaveResult Failure(string message, ProfileValidationResult validation)
    {
        return new ProfileSaveResult(false, false, message, validation);
    }
}
