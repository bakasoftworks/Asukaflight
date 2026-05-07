using System;
using System.Collections.Generic;

namespace Gx12.Launcher.Wpf.Models;

public sealed record DeviceAssignmentStatus(
    string Role,
    string ConfiguredToken,
    string Kind,
    string Message)
{
    public string StatusLabel => Kind switch
    {
        "Ok" => "OK",
        "Warn" => "Check",
        "Danger" => "Problem",
        "Muted" => "Off",
        _ => Kind
    };

    public string TokenDisplay => ShortenToken(NormalizeToken(ConfiguredToken));

    private static string NormalizeToken(string token)
    {
        return string.IsNullOrWhiteSpace(token) ? "auto" : token.Trim();
    }

    private static string ShortenToken(string token)
    {
        if (token.Length <= 14 ||
            token.Equals("auto", StringComparison.OrdinalIgnoreCase) ||
            token.Equals("*", StringComparison.OrdinalIgnoreCase))
        {
            return token;
        }

        return $"{token[..6]}...{token[^6..]}";
    }
}

public sealed record GameInputDeviceAssignmentReport(
    string Summary,
    IReadOnlyList<GameInputMouseDeviceRecord> Devices,
    IReadOnlyList<DeviceAssignmentStatus> Statuses);
