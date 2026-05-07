using System.Collections.Generic;

namespace Gx12.Launcher.Wpf.Services;

public sealed record Gx12DiagnosticCommand(
    string Name,
    IReadOnlyList<string> Arguments,
    string CommandLine,
    int TimeoutMilliseconds);
