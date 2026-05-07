using System;
using System.Collections.Generic;
using System.Linq;
using Gx12.Launcher.Wpf.Models;

namespace Gx12.Launcher.Wpf.Services;

public static class OptionTooltipCatalog
{
    private static readonly IReadOnlyDictionary<string, ProfileSettingDefinition> DefinitionsById =
        ProfileSchema.Definitions.ToDictionary(
            definition => definition.Id,
            StringComparer.OrdinalIgnoreCase);

    private static readonly IReadOnlyDictionary<string, string> BindingSettings =
        new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase)
        {
            ["ProfileName"] = "trainer.name",
            ["FrameRateHz"] = "trainer.frame_rate_hz",
            ["HighResolution2xEnabled"] = "trainer.resolution_mode",
            ["ControlMode"] = "control.mode",
            ["MouseAimEnabled"] = "control.mode",
            ["StopKey"] = "safety.stop_key",
            ["FreezeKey"] = "safety.freeze_key",
            ["MouseRightEnabled"] = "mouse_right_stick.enabled",

            ["RollGain"] = "mapper.roll_gain",
            ["PitchGain"] = "mapper.pitch_gain",
            ["MaxOutput"] = "mapper.max_output",
            ["Deadband"] = "mapper.deadband",
            ["Expo"] = "mapper.expo",
            ["OutputCurve"] = "mapper.output_curve",
            ["ActualCenter"] = "mapper.actual_center",
            ["ActualMax"] = "mapper.actual_max",
            ["ActualExpo"] = "mapper.actual_expo",
            ["OutputShapeNodesText"] = "mapper.output_shape_nodes",
            ["ReturnShapeNodesText"] = "mapper.return_shape_nodes",
            ["ReturnEnabled"] = "mapper.return_enabled",
            ["ReturnRate"] = "mapper.return_rate",
            ["ReturnIdle"] = "mapper.return_idle_ms",
            ["ConstantReturnEnabled"] = "mapper.constant_return_enabled",
            ["ConstantReturnRate"] = "mapper.constant_return_rate",
            ["ElasticReturnEnabled"] = "mapper.elastic_return_enabled",
            ["ElasticReturnMode"] = "mapper.elastic_return_mode",
            ["ElasticReturnCoefficient"] = "mapper.elastic_return_coefficient",
            ["ElasticReturnCurve"] = "mapper.elastic_return_curve",
            ["ReturnShapingEnabled"] = "mapper.return_shaping_enabled",
            ["InputFilter"] = "mapper.input_filter",
            ["Smoothing"] = "mapper.smoothing",
            ["OneEuroMinCutoffHz"] = "mapper.one_euro_min_cutoff_hz",
            ["OneEuroBeta"] = "mapper.one_euro_beta",
            ["OneEuroDcutoffHz"] = "mapper.one_euro_dcutoff_hz",
            ["DespikeEnabled"] = "mapper.despike_enabled",
            ["DespikeCountEnabled"] = "mapper.despike_count_enabled",
            ["DespikeWindow"] = "mapper.despike_window",
            ["DespikeThresholdSigma"] = "mapper.despike_threshold_sigma",
            ["PositionModel"] = "mapper.position_model",
            ["GimbalFrequencyHz"] = "mapper.gimbal_frequency_hz",
            ["GimbalDampingRatio"] = "mapper.gimbal_damping_ratio",
            ["GimbalInputImpulse"] = "mapper.gimbal_input_impulse",
            ["GimbalStaticFriction"] = "mapper.gimbal_static_friction",
            ["GimbalDynamicFriction"] = "mapper.gimbal_dynamic_friction",
            ["GimbalEdgeBumper"] = "mapper.gimbal_edge_bumper",
            ["GimbalAntiwindupEnabled"] = "mapper.gimbal_antiwindup_enabled",
            ["GimbalAntiwindupStart"] = "mapper.gimbal_antiwindup_start",
            ["GimbalAntiwindupMinGain"] = "mapper.gimbal_antiwindup_min_gain",
            ["InputGainMode"] = "mapper.input_gain_mode",
            ["AdaptiveSlowGain"] = "mapper.adaptive_slow_gain",
            ["AdaptiveFastGain"] = "mapper.adaptive_fast_gain",
            ["AdaptiveSpeedLow"] = "mapper.adaptive_speed_low",
            ["AdaptiveSpeedHigh"] = "mapper.adaptive_speed_high",
            ["AdaptiveCurve"] = "mapper.adaptive_curve",
            ["AdaptiveTrackerMs"] = "mapper.adaptive_tracker_ms",
            ["GateShape"] = "mapper.gate_shape",
            ["DiagonalScale"] = "mapper.diagonal_scale",
            ["InvertRoll"] = "mapper.invert_roll",
            ["InvertPitch"] = "mapper.invert_pitch",
            ["SwapAxes"] = "mapper.swap_axes",

            ["KeyboardInputSource"] = "keyboard_left_stick.input_source",
            ["KeyboardRequireAnalog"] = "keyboard_left_stick.require_analog",
            ["MouseLeftRequireDevice"] = "mouse_left_stick.require_device",
            ["MouseLeftThrottleRate"] = "mouse_left_stick.throttle_rate",
            ["MouseLeftYawGain"] = "mouse_left_stick.yaw_gain",
            ["MouseLeftYawPulse"] = "mouse_left_stick.yaw_pulse",
            ["MouseLeftYawDeadband"] = "mouse_left_stick.yaw_deadband",
            ["MouseLeftInvertThrottle"] = "mouse_left_stick.invert_throttle",
            ["MouseLeftInvertYaw"] = "mouse_left_stick.invert_yaw",
            ["MouseLeftSwapAxes"] = "mouse_left_stick.swap_axes",
            ["MouseLeftYawShapingEnabled"] = "mouse_left_stick.yaw_shaping_enabled",
            ["MouseLeftYawOutputCurve"] = "mouse_left_stick.yaw_output_curve",
            ["MouseLeftYawOutputShapingEnabled"] = "mouse_left_stick.yaw_output_shaping_enabled",
            ["MouseLeftYawReturnShapingEnabled"] = "mouse_left_stick.yaw_return_shaping_enabled",
            ["MouseLeftYawOutputShapeNodesText"] = "mouse_left_stick.yaw_output_shape_nodes",
            ["MouseLeftYawReturnShapeNodesText"] = "mouse_left_stick.yaw_return_shape_nodes",

            ["AimSensitivityX"] = "mouse_aim.sensitivity_x",
            ["AimSensitivityY"] = "mouse_aim.sensitivity_y",
            ["AimReticleLimit"] = "mouse_aim.reticle_limit",
            ["AimReticleDeadband"] = "mouse_aim.reticle_deadband",
            ["AimReturnRate"] = "mouse_aim.reticle_return_rate",
            ["AimOutputSmoothing"] = "mouse_aim.output_smoothing",
            ["AimRollGain"] = "mouse_aim.roll_gain",
            ["AimYawGain"] = "mouse_aim.yaw_gain",
            ["AimPitchGain"] = "mouse_aim.pitch_gain",
            ["AimRollMax"] = "mouse_aim.roll_max",
            ["AimYawMax"] = "mouse_aim.yaw_max",
            ["AimPitchMax"] = "mouse_aim.pitch_max",
            ["AimSlewRate"] = "mouse_aim.slew_rate",
            ["AimInvertX"] = "mouse_aim.invert_x",
            ["AimInvertY"] = "mouse_aim.invert_y"
        };

    private static readonly IReadOnlyDictionary<string, OptionTooltipInfo> BindingTooltips =
        new Dictionary<string, OptionTooltipInfo>(StringComparer.OrdinalIgnoreCase)
        {
            ["SearchText"] = new(
                "Profile search",
                "Filters the profile rail by profile name, file name, mode, input source, and badges. It does not change any profile file."),
            ["LeftStickSource"] = new(
                "Left stick source",
                "Chooses the only PC-side left-stick source for direct-mouse profiles.",
                "Off keeps throttle low and yaw centered. Keyboard / Wooting enables the keyboard source. Second mouse enables the bound left mouse source. The launcher saves the underlying source booleans atomically so the two left-stick sources do not overlap.",
                "Input-routing setting. Recheck before live use."),
            ["DeviceScanSeconds"] = new(
                "GameInput scan duration",
                "How long the mouse-device scanner listens for movement.",
                "Use a short scan for routine checks. Use a longer scan when identifying physical mice one at a time. Valid range: 1..30 seconds."),
            ["MouseLeftDryRunSeconds"] = new(
                "Second mouse dry-run duration",
                "How long the second-mouse diagnostic runs without starting a live trainer session.",
                "Useful after changing left mouse token, yaw gain, deadband, or axis inversion. Valid range: 1..60 seconds."),
            ["CatalogSearchText"] = new(
                "Catalog search",
                "Filters the settings catalog by label, TOML path, group, page, tier, current value, default value, and help text."),
            ["ShowBasicSettings"] = new(
                "Show basic settings",
                "Includes the normal flight and source settings in the catalog."),
            ["ShowAdvancedSettings"] = new(
                "Show advanced settings",
                "Includes deeper tuning and compatibility settings in the catalog."),
            ["ShowExperimentalSettings"] = new(
                "Show tuning settings",
                "Includes shaping, filtering, dynamic-gimbal, and adaptive controls for deeper profile tuning.",
                "",
                "Verify major tuning changes in VelociDrone before real flight."),
            ["ShowChangedSettingsOnly"] = new(
                "Changed only",
                "Shows only settings whose current value differs from the schema default."),
            ["ShowInvalidSettingsOnly"] = new(
                "Invalid only",
                "Shows only settings whose current raw TOML value does not match the expected kind or allowed values."),
            ["CatalogEditValue"] = new(
                "Catalog edit value",
                "Edits the selected catalog setting without hand-editing the TOML text.",
                "String and enum values are quoted for TOML automatically. Number, boolean, and array values are saved as raw TOML."),
        };

    private static readonly IReadOnlyDictionary<string, OptionTooltipInfo> CommandTooltips =
        new Dictionary<string, OptionTooltipInfo>(StringComparer.OrdinalIgnoreCase)
        {
            ["ClearSearchCommand"] = new("Clear search", "Clears the current profile search text."),
            ["CloneProfileCommand"] = new(
                "Clone profile",
                "Creates a line-preserving copy of the selected profile and gives it a new file name."),
            ["RefreshCommand"] = new(
                "Refresh profiles",
                "Reloads the profile folder, default marker, profile summaries, and validation state from disk."),
            ["ChangeFolderCommand"] = new(
                "Change profile folder",
                "Chooses the folder used for loading and saving GX12 profiles."),
            ["SetDefaultCommand"] = new(
                "Set default profile",
                "Marks the selected profile as the launcher's default profile."),
            ["CopyPathCommand"] = new(
                "Copy profile path",
                "Copies the selected profile's full path to the clipboard."),
            ["CopyTrainerCommandLineCommand"] = new(
                "Copy trainer command",
                "Copies the exact composite-trainer command line for the selected profile."),
            ["CopyConsoleScriptPathCommand"] = new(
                "Copy console script path",
                "Copies the generated console script path used by the WPF launcher."),
            ["StartCompositeTrainerCommand"] = new(
                "Start composite trainer",
                "Starts gx12mouse through the managed WPF composite-trainer path for the selected profile.",
                "",
                "Real flight has been tested on the current path; recheck throttle cut and props-off behavior after package or tune changes."),
            ["StopRunCommand"] = new(
                "Stop active run",
                "Signals the shared launcher stop event so the managed trainer run can exit cleanly."),
            ["RunMouseDevicesCommand"] = new(
                "Run mouse enumeration",
                "Runs gx12mouse --mouse-devices-gameinput for the chosen duration and parses the detected devices."),
            ["RunShowProfileCommand"] = new(
                "Run profile validation",
                "Runs gx12mouse --show-profile on the selected profile and shows the parsed runtime view."),
            ["RunGimbalPreviewCommand"] = new(
                "Run gimbal preview",
                "Runs gx12mouse --gimbal-preview for the selected profile so shaping and return behavior can be inspected."),
            ["RunMouseLeftDryRunCommand"] = new(
                "Run second-mouse dry run",
                "Runs the second-mouse diagnostic without starting a live trainer session."),
            ["StopDiagnosticCommand"] = new(
                "Stop diagnostic",
                "Stops the currently running diagnostic command."),
            ["CopyCommandLineCommand"] = new(
                "Copy command line",
                "Copies the command line shown on this row."),
            ["CopyDiagnosticOutputCommand"] = new(
                "Copy diagnostic output",
                "Copies the current diagnostic command and captured output."),
            ["CopyDiagnosticLogPathCommand"] = new(
                "Copy diagnostic log path",
                "Copies the path of the most recent persisted diagnostic log."),
            ["JumpToCatalogRowCommand"] = new(
                "Jump to setting",
                "Selects this row in the catalog detail panel so it can be edited or assigned a tooltip image."),
            ["ApplyCatalogValueCommand"] = new(
                "Apply catalog value",
                "Writes the edited value for the selected catalog setting into the active profile."),
            ["ResetCatalogValueCommand"] = new(
                "Reset catalog value",
                "Writes the schema default value for the selected catalog setting into the active profile."),
            ["ChooseTooltipImageCommand"] = new(
                "Attach setting tooltip image",
                "Attaches an image to the selected catalog setting's tooltip metadata under .gx12-ui."),
            ["ClearTooltipImageCommand"] = new(
                "Clear tooltip image",
                "Removes the image mapping for the selected catalog setting."),
            ["CopyTooltipImagePathCommand"] = new(
                "Copy setting tooltip image path",
                "Copies the selected setting's tooltip image path."),
            ["CopyTooltipSpriteDirectoryCommand"] = new(
                "Copy random sprite folder",
                "Creates and copies the folder where PNG sprites are dropped for random tooltip art."),
            ["RefreshTooltipSpritesCommand"] = new(
                "Refresh random sprites",
                "Updates the App Settings sprite count. Tooltips also rescan the folder whenever they open."),
            ["CopyWpfLauncherPathCommand"] = new(
                "Copy release exe path",
                "Copies the packaged Asukaflight.exe path for GitHub releases."),
            ["CopyPublishCommandCommand"] = new(
                "Copy package command",
                "Copies the GitHub release packaging recipe from the release panel."),
            ["CopyReleaseSummaryCommand"] = new(
                "Copy release summary",
                "Copies the release readiness summary and active checks.")
        };

    private static readonly IReadOnlyDictionary<string, string> SettingHelpOverrides =
        new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase)
        {
            ["trainer.frame_rate_hz"] = "Mapper update rate for trainer output. 1000 Hz matches the validated composite firmware path; lower values are mainly for diagnostics.",
            ["trainer.resolution_mode"] = "Enables the GX12 2x trainer-resolution transport for active mouse trainer axes: right-stick mouse and second-mouse left-stick throttle/yaw. Physical radio sticks stay on the radio-native path.",
            ["control.mode"] = "Direct mouse maps the mouse into the virtual right stick. Reticle aim switches to the open-loop aim controller for sim and bench testing.",
            ["safety.stop_key"] = "Global key that stops the active managed run and triggers cleanup.",
            ["safety.freeze_key"] = "Global key that toggles mouse freeze during an active run.",
            ["mouse_right_stick.enabled"] = "Enables the GameInput mouse source for right-stick roll and pitch.",
            ["mapper.roll_gain"] = "Mouse X sensitivity for right-stick roll. Higher values move the virtual stick farther for the same mouse movement.",
            ["mapper.pitch_gain"] = "Mouse Y sensitivity for right-stick pitch. Higher values move the virtual stick farther for the same mouse movement.",
            ["mapper.max_output"] = "Maximum right-stick trainer output in profile units. 512 is full stick; 2x mode doubles transport precision internally.",
            ["mapper.deadband"] = "Small right-stick outputs below this trainer-unit value are suppressed to avoid drift or jitter near center.",
            ["mapper.expo"] = "Response curve for roll and pitch. 0 is linear; higher values soften small movements while keeping edge authority.",
            ["mapper.output_curve"] = "Selects the right-stick output curve: scalar expo, free-form shaping nodes, or Actual Rates-style center/max/expo controls.",
            ["mapper.input_filter"] = "Pre-integrator input filtering. Off is immediate; smoothing preserves the legacy low-pass; One Euro reduces jitter with less fast-motion lag.",
            ["mapper.smoothing"] = "Legacy output smoothing for right-stick roll and pitch. Use input_filter = smoothing to enable it.",
            ["mapper.despike_enabled"] = "Hampel filter that replaces isolated mouse-delta spikes with the local median before shaping.",
            ["mapper.despike_count_enabled"] = "Reports how many Hampel mouse samples were replaced while the trainer is running.",
            ["mapper.position_model"] = "Integrator keeps the current virtual-position mapper. Dynamic gimbal treats mouse input as force on a damped virtual stick.",
            ["mapper.input_gain_mode"] = "Flat keeps current sensitivity. Adaptive changes gain with recent mouse speed for fine slow corrections and stronger fast sweeps.",
            ["mapper.gate_shape"] = "2D roll/pitch gate. Axis preserves independent-axis behavior; circle caps radial magnitude; octagon is a middle ground; square preserves more diagonal authority.",
            ["mapper.return_enabled"] = "When enabled, the virtual right stick recenters after mouse movement stops.",
            ["mapper.constant_return_enabled"] = "When enabled, the virtual right stick is urged toward center on every mapper tick.",
            ["mapper.elastic_return_enabled"] = "When enabled, return strength scales with current stick deflection.",
            ["mapper.return_shaping_enabled"] = "Enables free-form elastic return-rate shaping nodes.",
            ["mapper.output_shape_nodes"] = "Free-form right-stick output shaping curve. Empty list means linear.",
            ["mapper.return_shape_nodes"] = "Free-form elastic return-rate shaping curve. Empty list means linear.",
            ["mouse_left_stick.enabled"] = "Uses a bound second mouse for left-stick throttle and yaw.",
            ["mouse_left_stick.require_device"] = "Requires an explicit second-mouse device instead of silently falling back when the token is missing.",
            ["mouse_left_stick.throttle_rate"] = "Second-mouse throttle integration speed. Left throttle still stays on the low-throttle-safe path.",
            ["mouse_left_stick.yaw_gain"] = "Second-mouse yaw sensitivity before yaw shaping and limits.",
            ["mouse_left_stick.yaw_pulse"] = "Maximum second-mouse yaw output. 512 is full left-stick yaw.",
            ["mouse_left_stick.yaw_deadband"] = "Small second-mouse yaw values below this trainer-unit threshold are suppressed near center.",
            ["mouse_left_stick.yaw_shaping_enabled"] = "Enables the dedicated left-yaw shaping path for second mouse yaw.",
            ["mouse_left_stick.yaw_output_curve"] = "Selects left-yaw output curve mode: scalar expo, free-form nodes, or Actual Rates-style controls.",
            ["mouse_left_stick.yaw_output_shape_nodes"] = "Free-form second-mouse yaw output shaping curve. Empty list means linear.",
            ["mouse_left_stick.yaw_return_shape_nodes"] = "Free-form second-mouse yaw return shaping curve. Empty list means linear.",
            ["keyboard_left_stick.input_source"] = "gameinput is digital keyboard input. wooting_analog reads analog key depth. auto falls back to digital if analog is unavailable.",
            ["keyboard_left_stick.require_analog"] = "Stops safely instead of falling back to digital keys if Wooting analog input fails.",
            ["mouse_aim.sensitivity_x"] = "Horizontal mouse sensitivity for moving the virtual aim reticle.",
            ["mouse_aim.sensitivity_y"] = "Vertical mouse sensitivity for moving the virtual aim reticle.",
            ["mouse_aim.reticle_limit"] = "Maximum virtual reticle offset before aim input saturates.",
            ["mouse_aim.reticle_deadband"] = "Small virtual reticle offsets below this value produce no aim correction.",
            ["mouse_aim.reticle_return_rate"] = "How quickly the virtual reticle drifts back toward center when mouse input stops.",
            ["mouse_aim.output_smoothing"] = "Filtering applied to reticle-aim outputs. 0 is immediate; higher values smooth command changes.",
            ["mouse_aim.roll_gain"] = "How strongly horizontal aim error commands right-stick roll.",
            ["mouse_aim.yaw_gain"] = "How strongly horizontal aim error commands left-stick yaw.",
            ["mouse_aim.pitch_gain"] = "How strongly vertical aim error commands right-stick pitch.",
            ["mouse_aim.roll_max"] = "Maximum roll output reticle aim can command. 512 is full right-stick roll.",
            ["mouse_aim.yaw_max"] = "Maximum yaw output reticle aim can command. 512 is full left-stick yaw.",
            ["mouse_aim.pitch_max"] = "Maximum pitch output reticle aim can command. 512 is full right-stick pitch.",
            ["mouse_aim.slew_rate"] = "Maximum per-second change allowed on reticle-aim outputs.",
            ["mouse_aim.invert_x"] = "Reverses horizontal reticle aim input before it reaches the reticle controller.",
            ["mouse_aim.invert_y"] = "Reverses vertical reticle aim input before it reaches the reticle controller."
        };

    public static bool TryGetTooltipForBindingPath(string? bindingPath, out OptionTooltipInfo tooltip)
    {
        var key = NormalizeBindingPath(bindingPath);
        if (string.IsNullOrWhiteSpace(key))
        {
            tooltip = default!;
            return false;
        }

        if (BindingSettings.TryGetValue(key, out var settingId) &&
            TryGetTooltipForSetting(settingId, out tooltip))
        {
            return true;
        }

        return BindingTooltips.TryGetValue(key, out tooltip!);
    }

    public static bool TryGetTooltipForCommandPath(string? commandPath, out OptionTooltipInfo tooltip)
    {
        var key = NormalizeBindingPath(commandPath);
        if (string.IsNullOrWhiteSpace(key))
        {
            tooltip = default!;
            return false;
        }

        return CommandTooltips.TryGetValue(key, out tooltip!);
    }

    public static bool TryGetTooltipForSetting(string settingId, out OptionTooltipInfo tooltip)
    {
        if (!DefinitionsById.TryGetValue(settingId, out var definition))
        {
            tooltip = default!;
            return false;
        }

        tooltip = CreateSettingTooltip(definition);
        return true;
    }

    public static string NormalizeBindingPath(string? bindingPath)
    {
        if (string.IsNullOrWhiteSpace(bindingPath))
        {
            return "";
        }

        var value = bindingPath.Trim();
        while (value.StartsWith("DataContext.", StringComparison.OrdinalIgnoreCase))
        {
            value = value["DataContext.".Length..];
        }

        while (value.StartsWith("Editor.", StringComparison.OrdinalIgnoreCase))
        {
            value = value["Editor.".Length..];
        }

        var dotIndex = value.LastIndexOf('.');
        return dotIndex >= 0 ? value[(dotIndex + 1)..] : value;
    }

    private static OptionTooltipInfo CreateSettingTooltip(ProfileSettingDefinition definition)
    {
        var body = SettingHelpOverrides.TryGetValue(definition.Id, out var helpOverride)
            ? helpOverride
            : definition.Help;
        var detail = BuildDetail(definition);
        var footer = $"TOML: {definition.TomlPath}. Default: {FormatDefaultValue(definition)}.";
        return new OptionTooltipInfo(definition.Label, body, detail, definition.RiskText, footer);
    }

    private static string BuildDetail(ProfileSettingDefinition definition)
    {
        var parts = new List<string>();
        if (!string.IsNullOrWhiteSpace(definition.DetailText))
        {
            parts.Add(definition.DetailText);
        }

        if (definition.Id.EndsWith("_shape_nodes", StringComparison.OrdinalIgnoreCase))
        {
            parts.Add("Editor controls: left-click adds/selects a node, drag moves it, mouse wheel changes width, right-click or Delete removes it.");
        }

        if (definition.Tier == SettingTier.Experimental)
        {
            parts.Add("Treat changes as sim-first tuning until validated.");
        }

        return string.Join(" ", parts);
    }

    private static string FormatDefaultValue(ProfileSettingDefinition definition)
    {
        var value = definition.DefaultRawValue.Trim();
        return definition.Kind is SettingKind.String or SettingKind.Enum
            ? value.Trim('"')
            : value;
    }
}
