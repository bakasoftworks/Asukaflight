using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Text.RegularExpressions;
using Gx12.Launcher.Wpf.Models;

namespace Gx12.Launcher.Wpf.Services;

public static class GameInputMouseDeviceScan
{
    private static readonly Regex SummaryDeviceRegex = new(
        @"^\s+\[(?<index>\d+)\]\s+root=(?<root>\S+)\s+device=(?<device>\S+)\s+vid=(?<vid>0x[0-9a-fA-F]+)\s+pid=(?<pid>0x[0-9a-fA-F]+)\s+total_dx=(?<dx>[+-]?\d+)\s+total_dy=(?<dy>[+-]?\d+)\s+name='(?<name>.*)'\s*$",
        RegexOptions.CultureInvariant | RegexOptions.Compiled);

    private static readonly Regex SampleDeviceRegex = new(
        @"^\s+\[(?<index>\d+)\]\s+root=(?<root>\S+)\s+vid=(?<vid>0x[0-9a-fA-F]+)\s+pid=(?<pid>0x[0-9a-fA-F]+)\s+cb=\s*(?<callbacks>\d+)\s+dx=\s*(?<dx>[+-]?\d+)\s+dy=\s*(?<dy>[+-]?\d+)\s+buttons=(?<buttons>\S+)\s+name='(?<name>.*)'\s*$",
        RegexOptions.CultureInvariant | RegexOptions.Compiled);

    private static readonly Regex PnpRegex = new(
        @"^\s+pnp=(?<pnp>.+?)\s*$",
        RegexOptions.CultureInvariant | RegexOptions.Compiled);

    public static GameInputDeviceAssignmentReport Analyze(
        ProfileSummary profile,
        bool mouseLeftRequireDevice,
        string output)
    {
        return Analyze(
            profile.RightDevice,
            profile.LeftDevice,
            profile.MouseRightEnabled,
            profile.MouseLeftEnabled,
            mouseLeftRequireDevice,
            output);
    }

    public static GameInputDeviceAssignmentReport Analyze(
        string rightDeviceToken,
        string leftDeviceToken,
        bool mouseRightEnabled,
        bool mouseLeftEnabled,
        bool mouseLeftRequireDevice,
        string output)
    {
        var devices = ParseDevices(output);
        var statuses = BuildAssignmentStatuses(
            rightDeviceToken,
            leftDeviceToken,
            mouseRightEnabled,
            mouseLeftEnabled,
            mouseLeftRequireDevice,
            devices);
        var movedCount = devices.Count(device => device.HasMovement);
        var summary = devices.Count == 0
            ? "No GameInput mouse devices were found in the latest scan."
            : $"{devices.Count} GameInput mouse device(s); {movedCount} moved during the latest scan.";

        return new GameInputDeviceAssignmentReport(summary, devices, statuses);
    }

    public static IReadOnlyList<GameInputMouseDeviceRecord> ParseDevices(string output)
    {
        var devices = new Dictionary<string, MutableDevice>(StringComparer.OrdinalIgnoreCase);
        string? lastRoot = null;

        foreach (var line in SplitLines(output))
        {
            var summaryMatch = SummaryDeviceRegex.Match(line);
            if (summaryMatch.Success)
            {
                var root = summaryMatch.Groups["root"].Value;
                var item = GetOrCreate(devices, root);
                item.RootToken = root;
                item.DeviceToken = summaryMatch.Groups["device"].Value;
                item.VendorId = summaryMatch.Groups["vid"].Value;
                item.ProductId = summaryMatch.Groups["pid"].Value;
                item.TotalDx = ParseLong(summaryMatch.Groups["dx"].Value);
                item.TotalDy = ParseLong(summaryMatch.Groups["dy"].Value);
                item.Name = summaryMatch.Groups["name"].Value;
                item.HasSummary = true;
                lastRoot = root;
                continue;
            }

            var sampleMatch = SampleDeviceRegex.Match(line);
            if (sampleMatch.Success)
            {
                var root = sampleMatch.Groups["root"].Value;
                var item = GetOrCreate(devices, root);
                item.RootToken = root;
                item.VendorId = sampleMatch.Groups["vid"].Value;
                item.ProductId = sampleMatch.Groups["pid"].Value;
                item.Name = sampleMatch.Groups["name"].Value;
                if (!item.HasSummary)
                {
                    item.TotalDx += ParseLong(sampleMatch.Groups["dx"].Value);
                    item.TotalDy += ParseLong(sampleMatch.Groups["dy"].Value);
                }

                lastRoot = root;
                continue;
            }

            var pnpMatch = PnpRegex.Match(line);
            if (pnpMatch.Success && lastRoot is not null && devices.TryGetValue(lastRoot, out var pnpItem))
            {
                pnpItem.PnpPath = pnpMatch.Groups["pnp"].Value;
            }
        }

        return devices
            .Values
            .OrderByDescending(device => SafeAbs(device.TotalDx) + SafeAbs(device.TotalDy))
            .ThenBy(device => device.RootToken, StringComparer.OrdinalIgnoreCase)
            .Select(device => new GameInputMouseDeviceRecord(
                device.RootToken,
                device.DeviceToken,
                device.VendorId,
                device.ProductId,
                device.TotalDx,
                device.TotalDy,
                device.Name,
                device.PnpPath))
            .ToList();
    }

    private static IReadOnlyList<DeviceAssignmentStatus> BuildAssignmentStatuses(
        string rightDeviceToken,
        string leftDeviceToken,
        bool mouseRightEnabled,
        bool mouseLeftEnabled,
        bool mouseLeftRequireDevice,
        IReadOnlyList<GameInputMouseDeviceRecord> devices)
    {
        return new[]
        {
            BuildRightStatus(rightDeviceToken, leftDeviceToken, mouseRightEnabled, mouseLeftEnabled, devices),
            BuildLeftStatus(rightDeviceToken, leftDeviceToken, mouseLeftEnabled, mouseLeftRequireDevice, devices)
        };
    }

    private static DeviceAssignmentStatus BuildRightStatus(
        string profileRightDevice,
        string profileLeftDevice,
        bool mouseRightEnabled,
        bool mouseLeftEnabled,
        IReadOnlyList<GameInputMouseDeviceRecord> devices)
    {
        var rightToken = NormalizeBinding(profileRightDevice, "auto");
        if (!mouseRightEnabled)
        {
            return new DeviceAssignmentStatus(
                "Right",
                rightToken,
                "Muted",
                "Right-stick mouse input is disabled in this profile.");
        }

        var movedDevices = devices.Where(device => device.HasMovement).ToList();
        if (IsAutoBinding(rightToken))
        {
            if (movedDevices.Count == 0)
            {
                return new DeviceAssignmentStatus(
                    "Right",
                    rightToken,
                    "Warn",
                    "Right is auto, but the latest scan did not see a moved mouse.");
            }

            if (mouseLeftEnabled && IsAutoBinding(profileLeftDevice))
            {
                return movedDevices.Count >= 2
                    ? new DeviceAssignmentStatus(
                        "Right",
                        rightToken,
                        "Ok",
                        $"Right/left auto split has {movedDevices.Count} moved devices to choose from.")
                    : new DeviceAssignmentStatus(
                        "Right",
                        rightToken,
                        "Warn",
                        "Right and left are both auto; move two distinct mice during the scan to verify the split.");
            }

            if (mouseLeftEnabled && !IsAutoBinding(profileLeftDevice))
            {
                var nonLeftMoved = movedDevices.Count(device => !TokenEquals(device.RootToken, profileLeftDevice));
                return nonLeftMoved > 0
                    ? new DeviceAssignmentStatus(
                        "Right",
                        rightToken,
                        "Ok",
                        $"Right auto saw {nonLeftMoved} moved device(s) outside the configured left token.")
                    : new DeviceAssignmentStatus(
                        "Right",
                        rightToken,
                        "Warn",
                        "Right auto did not see a moved device outside the configured left token.");
            }

            return new DeviceAssignmentStatus(
                "Right",
                rightToken,
                "Ok",
                $"Right auto saw {movedDevices.Count} moved mouse device(s).");
        }

        var rightDevice = FindByRoot(devices, rightToken);
        if (rightDevice is null)
        {
            return new DeviceAssignmentStatus(
                "Right",
                rightToken,
                "Danger",
                "Configured right token was not found in the latest GameInput scan.");
        }

        return rightDevice.HasMovement
            ? new DeviceAssignmentStatus(
                "Right",
                rightToken,
                "Ok",
                $"Configured right token moved: {rightDevice.MovementSummary}.")
            : new DeviceAssignmentStatus(
                "Right",
                rightToken,
                "Warn",
                "Configured right token is present, but no movement was recorded.");
    }

    private static DeviceAssignmentStatus BuildLeftStatus(
        string profileRightDevice,
        string profileLeftDevice,
        bool mouseLeftEnabled,
        bool mouseLeftRequireDevice,
        IReadOnlyList<GameInputMouseDeviceRecord> devices)
    {
        var leftToken = NormalizeBinding(profileLeftDevice, "auto");
        var rightToken = NormalizeBinding(profileRightDevice, "auto");
        if (!mouseLeftEnabled)
        {
            return new DeviceAssignmentStatus(
                "Left",
                leftToken,
                "Muted",
                "Second-mouse left stick is disabled in this profile.");
        }

        if (!IsAutoBinding(leftToken) &&
            !IsAutoBinding(rightToken) &&
            TokenEquals(leftToken, rightToken))
        {
            return new DeviceAssignmentStatus(
                "Left",
                leftToken,
                "Danger",
                "Left and right are configured to the same explicit token.");
        }

        var movedDevices = devices.Where(device => device.HasMovement).ToList();
        if (IsAutoBinding(leftToken))
        {
            if (devices.Count == 0)
            {
                return new DeviceAssignmentStatus(
                    "Left",
                    leftToken,
                    mouseLeftRequireDevice ? "Danger" : "Warn",
                    "Second mouse is enabled, but the latest scan found no GameInput mouse devices.");
            }

            if (IsAutoBinding(rightToken))
            {
                return movedDevices.Count >= 2
                    ? new DeviceAssignmentStatus(
                        "Left",
                        leftToken,
                        "Ok",
                        $"Left auto can bind after {movedDevices.Count} distinct moved devices were seen.")
                    : new DeviceAssignmentStatus(
                        "Left",
                        leftToken,
                        "Warn",
                        "Left auto needs a second distinct moved mouse when right is also auto.");
            }

            var nonRightMoved = movedDevices.Count(device => !TokenEquals(device.RootToken, rightToken));
            return nonRightMoved > 0
                ? new DeviceAssignmentStatus(
                    "Left",
                    leftToken,
                    "Ok",
                    $"Left auto saw {nonRightMoved} moved device(s) outside the configured right token.")
                : new DeviceAssignmentStatus(
                    "Left",
                    leftToken,
                    "Warn",
                    "Left auto did not see a moved device outside the configured right token.");
        }

        var leftDevice = FindByRoot(devices, leftToken);
        if (leftDevice is null)
        {
            return new DeviceAssignmentStatus(
                "Left",
                leftToken,
                mouseLeftRequireDevice ? "Danger" : "Warn",
                mouseLeftRequireDevice
                    ? "Configured left token was not found, and require_device is enabled."
                    : "Configured left token was not found in the latest GameInput scan.");
        }

        return leftDevice.HasMovement
            ? new DeviceAssignmentStatus(
                "Left",
                leftToken,
                "Ok",
                $"Configured left token moved: {leftDevice.MovementSummary}.")
            : new DeviceAssignmentStatus(
                "Left",
                leftToken,
                "Warn",
                "Configured left token is present, but no movement was recorded.");
    }

    private static MutableDevice GetOrCreate(Dictionary<string, MutableDevice> devices, string root)
    {
        if (!devices.TryGetValue(root, out var item))
        {
            item = new MutableDevice { RootToken = root };
            devices[root] = item;
        }

        return item;
    }

    private static IEnumerable<string> SplitLines(string text)
    {
        return (text ?? "")
            .Replace("\r\n", "\n", StringComparison.Ordinal)
            .Replace('\r', '\n')
            .Split('\n');
    }

    private static long ParseLong(string text)
    {
        return long.TryParse(
            text,
            NumberStyles.AllowLeadingSign,
            CultureInfo.InvariantCulture,
            out var value)
            ? value
            : 0;
    }

    private static long SafeAbs(long value)
    {
        return value == long.MinValue ? long.MaxValue : Math.Abs(value);
    }

    private static string NormalizeBinding(string binding, string autoValue)
    {
        return string.IsNullOrWhiteSpace(binding) ? autoValue : binding.Trim();
    }

    private static bool IsAutoBinding(string binding)
    {
        return string.IsNullOrWhiteSpace(binding) ||
               binding.Equals("auto", StringComparison.OrdinalIgnoreCase) ||
               binding.Equals("*", StringComparison.OrdinalIgnoreCase);
    }

    private static GameInputMouseDeviceRecord? FindByRoot(
        IReadOnlyList<GameInputMouseDeviceRecord> devices,
        string token)
    {
        return devices.FirstOrDefault(device => TokenEquals(device.RootToken, token));
    }

    private static bool TokenEquals(string left, string right)
    {
        return left.Trim().Equals(right.Trim(), StringComparison.OrdinalIgnoreCase);
    }

    private sealed class MutableDevice
    {
        public string RootToken { get; set; } = "";
        public string DeviceToken { get; set; } = "";
        public string VendorId { get; set; } = "";
        public string ProductId { get; set; } = "";
        public long TotalDx { get; set; }
        public long TotalDy { get; set; }
        public string Name { get; set; } = "";
        public string PnpPath { get; set; } = "";
        public bool HasSummary { get; set; }
    }
}
