using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;
using Gx12.Launcher.Wpf.Models;

namespace Gx12.Launcher.Wpf.Services;

public sealed class UiSettingsService
{
    public const int DefaultAboveBarSpriteFixedReturnDelaySeconds = 60;
    public const int MinAboveBarSpriteFixedReturnDelaySeconds = 1;
    public const int MaxAboveBarSpriteFixedReturnDelaySeconds = 3600;
    public const int MaxPlaybackBindingSlots = 12;

    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        WriteIndented = true
    };

    private static readonly HashSet<string> SupportedImageExtensions = new(
        new[] { ".png", ".jpg", ".jpeg", ".bmp" },
        StringComparer.OrdinalIgnoreCase);

    public string GetUiDirectory(AppPaths paths)
    {
        return Path.Combine(paths.RepoRoot, ".gx12-ui");
    }

    public string GetSettingsPath(AppPaths paths)
    {
        return Path.Combine(GetUiDirectory(paths), "ui-settings.json");
    }

    public string GetTooltipImageDirectory(AppPaths paths)
    {
        return Path.Combine(GetUiDirectory(paths), "setting-tooltip-images");
    }

    public string GetTooltipSpriteDirectory(AppPaths paths)
    {
        return Path.Combine(GetUiDirectory(paths), "tooltip-sprites");
    }

    public string GetUiSpriteDirectory(AppPaths paths)
    {
        return Path.Combine(GetUiDirectory(paths), "ui-sprites");
    }

    public string GetAboveBarSpritePath(AppPaths paths)
    {
        return Path.Combine(GetUiSpriteDirectory(paths), "above.png");
    }

    public UiSettings Load(AppPaths paths)
    {
        var settingsPath = GetSettingsPath(paths);
        if (!File.Exists(settingsPath))
        {
            return new UiSettings
            {
                TooltipImages = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase)
            };
        }

        try
        {
            var settings = JsonSerializer.Deserialize<UiSettings>(File.ReadAllText(settingsPath), JsonOptions) ?? new UiSettings();
            return Normalize(settings);
        }
        catch
        {
            return new UiSettings
            {
                TooltipImages = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase)
            };
        }
    }

    public void Save(AppPaths paths, UiSettings settings)
    {
        var normalized = Normalize(settings);
        Directory.CreateDirectory(GetUiDirectory(paths));
        File.WriteAllText(
            GetSettingsPath(paths),
            JsonSerializer.Serialize(normalized, JsonOptions) + Environment.NewLine,
            System.Text.Encoding.UTF8);
    }

    public TooltipImageInfo? GetTooltipImage(AppPaths paths, UiSettings settings, string settingId)
    {
        var normalized = Normalize(settings);
        if (!normalized.TooltipImages.TryGetValue(settingId, out var relativePath) ||
            string.IsNullOrWhiteSpace(relativePath))
        {
            return null;
        }

        var fullPath = Path.IsPathRooted(relativePath)
            ? Path.GetFullPath(relativePath)
            : Path.GetFullPath(Path.Combine(GetUiDirectory(paths), relativePath.Replace('/', Path.DirectorySeparatorChar)));

        return new TooltipImageInfo(relativePath, fullPath, File.Exists(fullPath));
    }

    public TooltipImageInfo ImportTooltipImage(AppPaths paths, string settingId, string sourcePath)
    {
        if (!File.Exists(sourcePath))
        {
            throw new FileNotFoundException("Tooltip image file not found.", sourcePath);
        }

        var extension = Path.GetExtension(sourcePath);
        if (!SupportedImageExtensions.Contains(extension))
        {
            throw new ArgumentException("Tooltip images must be PNG, JPG, JPEG, or BMP files.", nameof(sourcePath));
        }

        var targetDirectory = GetTooltipImageDirectory(paths);
        Directory.CreateDirectory(targetDirectory);
        var targetPath = Path.Combine(targetDirectory, BuildImageFileName(settingId, extension));
        var sourceFullPath = Path.GetFullPath(sourcePath);
        var targetFullPath = Path.GetFullPath(targetPath);
        if (!sourceFullPath.Equals(targetFullPath, StringComparison.OrdinalIgnoreCase))
        {
            File.Copy(sourceFullPath, targetFullPath, overwrite: true);
        }

        var relativePath = Path.GetRelativePath(GetUiDirectory(paths), targetFullPath)
            .Replace(Path.DirectorySeparatorChar, '/');
        var settings = Load(paths);
        settings.TooltipImages[settingId] = relativePath;
        Save(paths, settings);

        return new TooltipImageInfo(relativePath, targetFullPath, Exists: true);
    }

    public void ClearTooltipImage(AppPaths paths, string settingId)
    {
        var settings = Load(paths);
        if (settings.TooltipImages.Remove(settingId))
        {
            Save(paths, settings);
        }
    }

    public static int ClampAboveBarSpriteFixedReturnDelaySeconds(int seconds)
    {
        return Math.Clamp(
            seconds,
            MinAboveBarSpriteFixedReturnDelaySeconds,
            MaxAboveBarSpriteFixedReturnDelaySeconds);
    }

    private static UiSettings Normalize(UiSettings settings)
    {
        var recordingPath = (settings.RecordingPath ?? string.Empty).Trim();

        var normalized = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        foreach (var pair in settings.TooltipImages ?? new Dictionary<string, string>())
        {
            if (string.IsNullOrWhiteSpace(pair.Key) || string.IsNullOrWhiteSpace(pair.Value))
            {
                continue;
            }

            normalized[pair.Key.Trim()] = pair.Value.Trim();
        }

        var playbackBindings = (settings.PlaybackBindings ?? new List<PlaybackBindingSettings>())
            .Where(binding => binding.Enabled)
            .Where(binding =>
                !string.IsNullOrWhiteSpace(binding.RecordingPath) ||
                !string.IsNullOrWhiteSpace(binding.Trigger) ||
                !string.IsNullOrWhiteSpace(binding.ChannelMask))
            .Take(MaxPlaybackBindingSlots)
            .Select(binding => new PlaybackBindingSettings
            {
                Enabled = true,
                RecordingPath = (binding.RecordingPath ?? string.Empty).Trim(),
                Trigger = string.IsNullOrWhiteSpace(binding.Trigger) ? "F5" : binding.Trigger.Trim(),
                ChannelMask = string.IsNullOrWhiteSpace(binding.ChannelMask) ? "ail,ele" : binding.ChannelMask.Trim(),
                BlockLiveInput = binding.BlockLiveInput
            })
            .ToList();

        return new UiSettings
        {
            RecordingPath = recordingPath,
            RecordingOverwrite = settings.RecordingOverwrite,
            PlaybackAileron = settings.PlaybackAileron,
            PlaybackElevator = settings.PlaybackElevator,
            PlaybackThrottle = settings.PlaybackThrottle,
            PlaybackRudder = settings.PlaybackRudder,
            PlaybackRadioRightGimbal = settings.PlaybackRadioRightGimbal && !settings.PlaybackRecordedTrainerRight,
            PlaybackRecordedTrainerRight = settings.PlaybackRecordedTrainerRight,
            PlaybackRadioLeftGimbal = settings.PlaybackRadioLeftGimbal && !settings.PlaybackRecordedTrainerLeft,
            PlaybackRecordedTrainerLeft = settings.PlaybackRecordedTrainerLeft,
            PlaybackBlockLiveInput = settings.PlaybackBlockLiveInput,
            TooltipImages = normalized,
            PlaybackBindings = playbackBindings,
            AboveBarSpriteRandomReturnDelay = settings.AboveBarSpriteRandomReturnDelay,
            AboveBarSpriteFixedReturnDelaySeconds = ClampAboveBarSpriteFixedReturnDelaySeconds(
                settings.AboveBarSpriteFixedReturnDelaySeconds)
        };
    }

    private static string BuildImageFileName(string settingId, string extension)
    {
        var safeChars = settingId
            .Select(character =>
                char.IsLetterOrDigit(character) || character is '-' or '_' or '.'
                    ? character
                    : '-')
            .ToArray();
        var safeName = new string(safeChars).Trim('.', '-');
        return string.IsNullOrWhiteSpace(safeName)
            ? $"setting{extension.ToLowerInvariant()}"
            : $"{safeName}{extension.ToLowerInvariant()}";
    }
}
