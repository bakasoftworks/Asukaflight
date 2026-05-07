namespace Gx12.Launcher.Wpf.Services;

public sealed record Gx12DiagnosticResult(
    bool IsSuccess,
    string Message,
    string CommandLine,
    string Output,
    string Name = "",
    System.DateTimeOffset StartedAt = default,
    System.DateTimeOffset CompletedAt = default);
