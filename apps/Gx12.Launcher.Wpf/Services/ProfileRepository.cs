using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using Gx12.Launcher.Wpf.Models;

namespace Gx12.Launcher.Wpf.Services;

public sealed class ProfileRepository
{
    private readonly ProfileDirectoryService _directoryService;
    private readonly IProfileValidator _validator;

    public ProfileRepository(ProfileDirectoryService directoryService, IProfileValidator? validator = null)
    {
        _directoryService = directoryService;
        _validator = validator ?? new Gx12EngineProfileValidator();
    }

    public AppPaths Paths => _directoryService.Resolve();

    public IReadOnlyList<ProfileSummary> LoadProfiles(AppPaths paths)
    {
        if (!Directory.Exists(paths.ProfileDirectory))
        {
            return Array.Empty<ProfileSummary>();
        }

        return Directory
            .EnumerateFiles(paths.ProfileDirectory, "*.toml", SearchOption.TopDirectoryOnly)
            .OrderBy(Path.GetFileName, StringComparer.OrdinalIgnoreCase)
            .Select(path => LoadProfile(path, paths.DefaultProfileFileName))
            .ToList();
    }

    public ProfileSummary LoadProfile(AppPaths paths, string path)
    {
        return LoadProfile(path, paths.DefaultProfileFileName);
    }

    public string CloneProfile(ProfileSummary profile)
    {
        var paths = Paths;
        if (!File.Exists(profile.FullPath))
        {
            throw new FileNotFoundException("Profile file not found.", profile.FullPath);
        }

        Directory.CreateDirectory(paths.ProfileDirectory);
        var candidateFileName = FindFreeCloneFileName(paths.ProfileDirectory, profile.FileName);
        var newPath = Path.Combine(paths.ProfileDirectory, candidateFileName);
        File.Copy(profile.FullPath, newPath);

        var newName = Path.GetFileNameWithoutExtension(candidateFileName);
        var document = TomlProfileDocument.Load(newPath);
        document.SetString("trainer", "name", newName);
        document.Save(newPath);

        var validation = _validator.Validate(paths, newPath);
        if (!validation.IsSuccess)
        {
            try
            {
                File.Delete(newPath);
            }
            catch
            {
            }

            throw new InvalidOperationException($"Cloned profile failed validation: {validation.Message}");
        }

        return newPath;
    }

    public AppPaths SetDefaultProfile(ProfileSummary profile)
    {
        return _directoryService.SetDefaultProfileFileName(profile.FileName);
    }

    public AppPaths ChangeProfileDirectory(string path)
    {
        return _directoryService.SetProfileDirectory(path);
    }

    public ProfileSaveResult SaveProfileValue(string profilePath, string section, string key, string rawValue)
    {
        return SaveProfileValues(profilePath, new[] { new ProfileValueUpdate(section, key, rawValue) });
    }

    public ProfileSaveResult SaveProfileValues(string profilePath, IReadOnlyList<ProfileValueUpdate> updates)
    {
        var paths = Paths;
        var originalText = File.ReadAllText(profilePath);
        var document = TomlProfileDocument.LoadText(originalText);
        var changed = false;
        foreach (var update in updates)
        {
            changed |= document.SetRaw(update.Section, update.Key, update.RawValue);
        }

        if (!changed)
        {
            return ProfileSaveResult.Success(
                false,
                "Profile value is already current.",
                ProfileValidationResult.Success("Validation not needed; no profile change."));
        }

        var updatedText = document.ToText();
        var tempPath = Path.Combine(
            Path.GetTempPath(),
            $"gx12-wpf-save-{Guid.NewGuid():N}.toml");

        try
        {
            File.WriteAllText(tempPath, updatedText, new UTF8Encoding(false));
            var validation = _validator.Validate(paths, tempPath);
            if (!validation.IsSuccess)
            {
                return ProfileSaveResult.Failure(validation.Message, validation);
            }

            File.WriteAllText(profilePath, updatedText, new UTF8Encoding(false));
            return ProfileSaveResult.Success(true, "Profile saved.", validation);
        }
        finally
        {
            try
            {
                File.Delete(tempPath);
            }
            catch
            {
            }
        }
    }

    private static string FindFreeCloneFileName(string directory, string sourceFileName)
    {
        var baseName = Path.GetFileNameWithoutExtension(sourceFileName);
        for (var index = 2; index <= 99; index++)
        {
            var candidate = $"{baseName}-v{index}.toml";
            if (!File.Exists(Path.Combine(directory, candidate)))
            {
                return candidate;
            }
        }

        throw new InvalidOperationException("Could not find a free copy filename.");
    }

    private static string BuildRightTuningSummary(
        string controlMode,
        string outputCurve,
        bool outputShaping,
        int outputNodeCount,
        bool returnShaping,
        int returnNodeCount)
    {
        if (controlMode.Equals("drone_mouse_aim", StringComparison.OrdinalIgnoreCase))
        {
            return "Reticle aim owns output";
        }

        var output = outputCurve.Equals("nodes", StringComparison.OrdinalIgnoreCase) || outputShaping
            ? $"Output nodes {outputNodeCount}"
            : $"Output {outputCurve}";
        var returnText = returnShaping
            ? $"return nodes {returnNodeCount}"
            : "return linear";
        return $"{output} / {returnText}";
    }

    private static string BuildLeftYawTuningSummary(
        bool mouseLeftEnabled,
        bool yawShaping,
        string outputCurve,
        bool outputShaping,
        int outputNodeCount,
        bool returnShaping,
        int returnNodeCount)
    {
        if (!mouseLeftEnabled)
        {
            return "Second mouse off";
        }

        if (!yawShaping)
        {
            return "Yaw shaping off";
        }

        var output = outputCurve.Equals("nodes", StringComparison.OrdinalIgnoreCase) || outputShaping
            ? $"yaw nodes {outputNodeCount}"
            : $"yaw {outputCurve}";
        var returnText = returnShaping
            ? $"return nodes {returnNodeCount}"
            : "return linear";
        return $"{output} / {returnText}";
    }

    private static string BuildMouseAimTuningSummary(TomlProfileDocument document, string controlMode)
    {
        if (!controlMode.Equals("drone_mouse_aim", StringComparison.OrdinalIgnoreCase))
        {
            return "Inactive";
        }

        var reticle = document.GetString("mouse_aim", "reticle_limit", "512");
        var smoothing = document.GetString("mouse_aim", "output_smoothing", "0.10");
        var slew = document.GetString("mouse_aim", "slew_rate", "9000");
        return $"Reticle {reticle} / smooth {smoothing} / slew {slew}";
    }

    private static ProfileSummary LoadProfile(string path, string defaultFileName)
    {
        var fileInfo = new FileInfo(path);
        var fileName = fileInfo.Name;
        var isDefault = fileName.Equals(defaultFileName, StringComparison.OrdinalIgnoreCase);

        try
        {
            var document = TomlProfileDocument.Load(path);
            var resolutionMode = document.GetString("trainer", "resolution_mode", "legacy");
            var controlMode = document.GetString("control", "mode", "direct_mouse");
            var mouseRightEnabled = document.GetBool("mouse_right_stick", "enabled", true);
            var keyboardEnabled = document.GetBool("keyboard_left_stick", "enabled", false);
            var keyboardInputSource = document.GetString("keyboard_left_stick", "input_source", "gameinput");
            var mouseLeftEnabled = document.GetBool("mouse_left_stick", "enabled", false);
            var outputShaping = document.GetBool("mapper", "output_shaping_enabled", false);
            var returnShaping = document.GetBool("mapper", "return_shaping_enabled", false);
            var outputCurve = document.GetString("mapper", "output_curve", outputShaping ? "nodes" : "expo");
            var outputNodeCount = StickShapeCurve.ParseNodes(document.GetRaw("mapper", "output_shape_nodes", "[]")).Count;
            var returnNodeCount = StickShapeCurve.ParseNodes(document.GetRaw("mapper", "return_shape_nodes", "[]")).Count;
            var mouseLeftYawShaping = document.GetBool("mouse_left_stick", "yaw_shaping_enabled",
                document.GetBool("mouse_left_stick", "yaw_mapper_shaping_enabled", false));
            var mouseLeftYawOutputShaping = document.GetBool("mouse_left_stick", "yaw_output_shaping_enabled", false);
            var mouseLeftYawReturnShaping = document.GetBool("mouse_left_stick", "yaw_return_shaping_enabled", false);
            var mouseLeftYawOutputCurve = document.GetString(
                "mouse_left_stick",
                "yaw_output_curve",
                mouseLeftYawOutputShaping ? "nodes" : "expo");
            var mouseLeftYawOutputNodeCount = StickShapeCurve.ParseNodes(document.GetRaw("mouse_left_stick", "yaw_output_shape_nodes", "[]")).Count;
            var mouseLeftYawReturnNodeCount = StickShapeCurve.ParseNodes(document.GetRaw("mouse_left_stick", "yaw_return_shape_nodes", "[]")).Count;
            var badges = BuildBadges(
                isDefault,
                controlMode,
                mouseRightEnabled,
                keyboardEnabled,
                keyboardInputSource,
                mouseLeftEnabled,
                outputShaping,
                returnShaping,
                mouseLeftYawShaping,
                resolutionMode,
                false);

            return new ProfileSummary
            {
                Name = document.GetString("trainer", "name", Path.GetFileNameWithoutExtension(fileName)),
                FileName = fileName,
                FullPath = fileInfo.FullName,
                IsDefault = isDefault,
                FrameRateHz = document.GetString("trainer", "frame_rate_hz", "1000"),
                StopKey = document.GetString("safety", "stop_key", "Esc"),
                FreezeKey = document.GetString("safety", "freeze_key", "F2"),
                ControlMode = controlMode,
                RollGain = document.GetString("mapper", "roll_gain", ""),
                PitchGain = document.GetString("mapper", "pitch_gain", ""),
                MaxOutput = document.GetString("mapper", "max_output", ""),
                RightTuningSummary = BuildRightTuningSummary(
                    controlMode,
                    outputCurve,
                    outputShaping,
                    outputNodeCount,
                    returnShaping,
                    returnNodeCount),
                LeftYawTuningSummary = BuildLeftYawTuningSummary(
                    mouseLeftEnabled,
                    mouseLeftYawShaping,
                    mouseLeftYawOutputCurve,
                    mouseLeftYawOutputShaping,
                    mouseLeftYawOutputNodeCount,
                    mouseLeftYawReturnShaping,
                    mouseLeftYawReturnNodeCount),
                MouseAimTuningSummary = BuildMouseAimTuningSummary(document, controlMode),
                RightDevice = document.GetString("mouse_devices", "right", "auto"),
                LeftDevice = document.GetString("mouse_devices", "left", ""),
                KeyboardInputSource = keyboardInputSource,
                MouseRightEnabled = mouseRightEnabled,
                KeyboardEnabled = keyboardEnabled,
                MouseLeftEnabled = mouseLeftEnabled,
                OutputShapingEnabled = outputShaping,
                ReturnShapingEnabled = returnShaping,
                MouseLeftYawShapingEnabled = mouseLeftYawShaping,
                LastWriteTime = fileInfo.LastWriteTime,
                Badges = badges
            };
        }
        catch (Exception exception)
        {
            return new ProfileSummary
            {
                Name = Path.GetFileNameWithoutExtension(fileName),
                FileName = fileName,
                FullPath = fileInfo.FullName,
                IsDefault = isDefault,
                FrameRateHz = "",
                StopKey = "",
                FreezeKey = "",
                ControlMode = "",
                RollGain = "",
                PitchGain = "",
                MaxOutput = "",
                RightTuningSummary = "",
                LeftYawTuningSummary = "",
                MouseAimTuningSummary = "",
                RightDevice = "",
                LeftDevice = "",
                KeyboardInputSource = "",
                LastWriteTime = fileInfo.LastWriteTime,
                Badges = BuildBadges(isDefault, "", false, false, "", false, false, false, false, "", true),
                LoadError = exception.Message
            };
        }
    }

    private static IReadOnlyList<ProfileBadge> BuildBadges(
        bool isDefault,
        string controlMode,
        bool mouseRightEnabled,
        bool keyboardEnabled,
        string keyboardInputSource,
        bool mouseLeftEnabled,
        bool outputShaping,
        bool returnShaping,
        bool mouseLeftYawShaping,
        string resolutionMode,
        bool invalid)
    {
        var badges = new List<ProfileBadge>();
        if (invalid)
        {
            badges.Add(new ProfileBadge("Invalid", "Danger"));
        }

        if (isDefault)
        {
            badges.Add(new ProfileBadge("Default", "Default"));
        }

        if (controlMode.Equals("drone_mouse_aim", StringComparison.OrdinalIgnoreCase))
        {
            badges.Add(new ProfileBadge("Reticle aim", "Info"));
        }
        else
        {
            badges.Add(new ProfileBadge("Direct mouse", "Info"));
        }

        if (mouseRightEnabled)
        {
            badges.Add(new ProfileBadge("Right mouse", "Neutral"));
        }

        if (keyboardEnabled)
        {
            badges.Add(new ProfileBadge("Keyboard left", "Neutral"));
        }

        if (keyboardInputSource.Equals("wooting_analog", StringComparison.OrdinalIgnoreCase) ||
            keyboardInputSource.Equals("auto", StringComparison.OrdinalIgnoreCase))
        {
            badges.Add(new ProfileBadge("Wooting", "Warn"));
        }

        if (mouseLeftEnabled)
        {
            badges.Add(new ProfileBadge("Second mouse", "Neutral"));
        }

        if (resolutionMode.Equals("gx12_2x", StringComparison.OrdinalIgnoreCase))
        {
            badges.Add(new ProfileBadge("2x resolution", "Warn"));
        }

        if (outputShaping || returnShaping || mouseLeftYawShaping)
        {
            badges.Add(new ProfileBadge("Shaping", "Accent"));
        }

        return badges;
    }
}
