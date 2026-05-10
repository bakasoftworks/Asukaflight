using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;

namespace Gx12.Launcher.Wpf.Models;

public sealed class ProfileSummary
{
    public required string Name { get; init; }
    public required string FileName { get; init; }
    public required string FullPath { get; init; }
    public required string FrameRateHz { get; init; }
    public required string StopKey { get; init; }
    public required string FreezeKey { get; init; }
    public required string ControlMode { get; init; }
    public required string RollGain { get; init; }
    public required string PitchGain { get; init; }
    public required string MaxOutput { get; init; }
    public required string RightTuningSummary { get; init; }
    public required string LeftYawTuningSummary { get; init; }
    public required string MouseAimTuningSummary { get; init; }
    public required string RightDevice { get; init; }
    public required string LeftDevice { get; init; }
    public required string KeyboardInputSource { get; init; }
    public required DateTime LastWriteTime { get; init; }
    public required IReadOnlyList<ProfileBadge> Badges { get; init; }
    public bool IsDefault { get; init; }
    public bool MouseRightEnabled { get; init; }
    public bool KeyboardEnabled { get; init; }
    public bool MouseLeftEnabled { get; init; }
    public bool RightMouseLeftEnabled { get; init; }
    public bool OutputShapingEnabled { get; init; }
    public bool ReturnShapingEnabled { get; init; }
    public bool MouseLeftYawShapingEnabled { get; init; }
    public string? LoadError { get; init; }

    public string DisplayName => IsDefault ? $"{Name} [default]" : Name;

    public string ModeLabel => ControlMode.Equals("drone_mouse_aim", StringComparison.OrdinalIgnoreCase)
        ? "Reticle aim"
        : "Direct mouse";

    public string RightSourceLabel => MouseRightEnabled ? "GameInput mouse" : "Right stick off";

    public string LeftSourceLabel
    {
        get
        {
            if (MouseLeftEnabled)
            {
                return "Second mouse";
            }

            if (RightMouseLeftEnabled)
            {
                return "Right mouse buttons/scroll";
            }

            if (!KeyboardEnabled)
            {
                return "Off";
            }

            return KeyboardInputSource.Equals("wooting_analog", StringComparison.OrdinalIgnoreCase)
                ? "Wooting analog"
                : "Keyboard / Wooting";
        }
    }

    public string SafetySummary => $"Start/stop {StopKey} / Freeze {FreezeKey}";

    public string GainSummary => $"Roll {RollGain} / Pitch {PitchGain} / Max {MaxOutput}";

    public string RightDeviceDisplay => string.IsNullOrWhiteSpace(RightDevice) ? "auto" : RightDevice;

    public string LeftDeviceDisplay => string.IsNullOrWhiteSpace(LeftDevice) ? "auto" : LeftDevice;

    public string DeviceSummary
    {
        get
        {
            return $"Right {RightDeviceDisplay} / Left {LeftDeviceDisplay}";
        }
    }

    public string LastWriteSummary => LastWriteTime.ToString("yyyy-MM-dd HH:mm");

    public string SearchText => string.Join(
        " ",
        new[]
        {
            Name,
            FileName,
            ModeLabel,
            RightSourceLabel,
            LeftSourceLabel,
            SafetySummary,
            DeviceSummary,
            string.Join(" ", Badges.Select(b => b.Label))
        });

    public string RelativeOrFullPath
    {
        get
        {
            var profileDirectory = Path.GetDirectoryName(FullPath);
            return string.IsNullOrWhiteSpace(profileDirectory)
                ? FullPath
                : Path.Combine(Path.GetFileName(profileDirectory), FileName);
        }
    }
}
