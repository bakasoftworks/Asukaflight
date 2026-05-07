namespace Gx12.Launcher.Wpf.Services;

public sealed record ProfileValidationResult(
    bool IsSuccess,
    bool IsSkipped,
    string Message)
{
    public static ProfileValidationResult Success(string message = "Profile validated.")
    {
        return new ProfileValidationResult(true, false, message);
    }

    public static ProfileValidationResult Skipped(string message)
    {
        return new ProfileValidationResult(true, true, message);
    }

    public static ProfileValidationResult Failure(string message)
    {
        return new ProfileValidationResult(false, false, message);
    }
}
