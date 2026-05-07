using System;
using System.Globalization;

namespace Gx12.Launcher.Wpf.Models;

public sealed record GameInputMouseDeviceRecord(
    string RootToken,
    string DeviceToken,
    string VendorId,
    string ProductId,
    long TotalDx,
    long TotalDy,
    string Name,
    string PnpPath)
{
    public bool HasMovement => TotalDx != 0 || TotalDy != 0;

    public long Activity => SafeAbs(TotalDx) + SafeAbs(TotalDy);

    public string RootDisplay => ShortenToken(RootToken);

    public string DeviceDisplay => ShortenToken(DeviceToken);

    public string VendorProductDisplay => $"{VendorId}:{ProductId}";

    public string MovementSummary => string.Format(
        CultureInfo.InvariantCulture,
        "dx {0:+0;-0;0} / dy {1:+0;-0;0}",
        TotalDx,
        TotalDy);

    private static long SafeAbs(long value)
    {
        return value == long.MinValue ? long.MaxValue : Math.Abs(value);
    }

    private static string ShortenToken(string token)
    {
        if (string.IsNullOrWhiteSpace(token))
        {
            return "";
        }

        return token.Length <= 14
            ? token
            : $"{token[..6]}...{token[^6..]}";
    }
}
