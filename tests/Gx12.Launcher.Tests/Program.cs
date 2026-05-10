using Gx12.Launcher.Wpf.Services;
using Gx12.Launcher.Wpf.Models;
using Gx12.Launcher.Wpf.ViewModels;
using Gx12.Launcher.Wpf.Controls;
using Gx12.Launcher.Wpf;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Xml;
using System.Xml.Linq;
using System.Text.RegularExpressions;
using System.Reflection;

var tests = new (string Name, Action Body)[]
{
    ("current profiles round-trip without text changes", CurrentProfilesRoundTrip),
    ("line-preserving scalar edit changes only the value", LinePreservingScalarEdit),
    ("line-preserving array edit keeps comments", LinePreservingArrayEdit),
    ("clone/default/folder match launcher marker behavior", CloneDefaultAndFolderMarkers),
    ("invalid profile save does not touch the source file", InvalidSaveDoesNotWrite),
    ("batch save switches left-stick sources atomically", BatchSaveSwitchesLeftStickSources),
    ("settings schema covers V3 saves and profile fixtures", SettingsSchemaCoversV3AndProfiles),
    ("settings catalog rows search labels and TOML keys", SettingsCatalogRowsSearchLabelsAndKeys),
    ("profile editor applies catalog edits", ProfileEditorAppliesCatalogEdits),
    ("profile editor toggles 2x resolution mode", ProfileEditorToggles2xResolutionMode),
    ("profile badges do not mark tuning as experimental", ProfileBadgesDoNotMarkTuningAsExperimental),
    ("profile editor option visibility follows selected modes", ProfileEditorOptionVisibilityFollowsSelectedModes),
    ("stick shape nodes parse clamp and format", StickShapeNodesParseClampAndFormat),
    ("stick shape curve evaluates node blend", StickShapeCurveEvaluatesNodeBlend),
    ("gameinput mouse device scan parses summary output", GameInputMouseDeviceScanParsesSummaryOutput),
    ("gameinput assignment check flags profile token state", GameInputAssignmentCheckFlagsProfileTokenState),
    ("diagnostic commands format gx12mouse arguments", DiagnosticCommandsFormatGx12MouseArguments),
    ("diagnostic runner reports missing executable", DiagnosticRunnerReportsMissingExecutable),
    ("runtime service formats trainer command and rejects missing inputs", RuntimeServiceFormatsTrainerCommandAndRejectsMissingInputs),
    ("runtime service formats recording and playback commands", RuntimeServiceFormatsRecordingAndPlaybackCommands),
    ("native playback bank bind can stop active playback", NativePlaybackBankBindCanStopActivePlayback),
    ("runtime service writes console script with delayed result reporting", RuntimeServiceWritesConsoleScriptWithDelayedResultReporting),
    ("diagnostic log store persists recent history", DiagnosticLogStorePersistsRecentHistory),
    ("ui settings service persists tooltip image mappings", UiSettingsServicePersistsTooltipImageMappings),
    ("ui settings service exposes random tooltip sprite folder", UiSettingsServiceExposesRandomTooltipSpriteFolder),
    ("ui settings service exposes fixed ui sprite path", UiSettingsServiceExposesFixedUiSpritePath),
    ("ui settings service persists above-bar sprite return settings", UiSettingsServicePersistsAboveBarSpriteReturnSettings),
    ("tooltip sprite service enumerates png drop folder", TooltipSpriteServiceEnumeratesPngDropFolder),
    ("catalog row handles missing tooltip image mappings", CatalogRowHandlesMissingTooltipImageMappings),
    ("main view model keeps editor during transient selected-profile clears", MainViewModelKeepsEditorDuringTransientSelectedProfileClears),
    ("main view model formats radio gimbal playback mask", MainViewModelFormatsRadioGimbalPlaybackMask),
    ("main view model falls back to HID left playback without PC left source", MainViewModelFallsBackToHidLeftPlaybackWithoutPcLeftSource),
    ("main view model persists recording playback channel toggles", MainViewModelPersistsRecordingPlaybackChannelToggles),
    ("main view model picks recording and playback files", MainViewModelPicksRecordingAndPlaybackFiles),
    ("release info exposes launcher and publish recipe", ReleaseInfoExposesLauncherAndPublishRecipe),
    ("release publish script omits public symbols", ReleasePublishScriptOmitsPublicSymbols),
    ("distribution publish script packages firmware and release bundle", DistributionPublishScriptPackagesFirmwareAndReleaseBundle),
    ("profile directory service recognizes release package roots", ProfileDirectoryServiceRecognizesReleasePackageRoots),
    ("profile directory service recognizes clean source roots", ProfileDirectoryServiceRecognizesCleanSourceRoots),
    ("main window option controls resolve tooltips", MainWindowOptionControlsResolveTooltips),
    ("main window hides release readiness user surface", MainWindowHidesReleaseReadinessUserSurface),
    ("main window files tab exposes above-bar sprite return controls", MainWindowFilesTabExposesAboveBarSpriteReturnControls),
    ("hotkey capture formats keys and mouse buttons", HotkeyCaptureFormatsKeysAndMouseButtons),
    ("main window exposes recording controls", MainWindowExposesRecordingControls),
    ("workspace tabs keep hover scoped to headers and allow overflow", WorkspaceTabsKeepHoverScopedToHeadersAndAllowOverflow),
    ("workspace tabs render optional above-bar sprite", WorkspaceTabsRenderOptionalAboveBarSprite),
    ("above-bar sprite click handler does not throw", AboveBarSpriteClickHandlerDoesNotThrow),
    ("main window above-bar sprite click does not throw", MainWindowAboveBarSpriteClickDoesNotThrow),
    ("main window grid placements fit declared tracks", MainWindowGridPlacementsFitDeclaredTracks)
};

var failures = 0;
foreach (var test in tests)
{
    try
    {
        test.Body();
        Console.WriteLine($"PASS {test.Name}");
    }
    catch (Exception exception)
    {
        failures++;
        Console.WriteLine($"FAIL {test.Name}");
        Console.WriteLine(exception);
    }
}

return failures == 0 ? 0 : 1;

static void CurrentProfilesRoundTrip()
{
    var repoRoot = FindRepoRoot();
    var profileDirectory = Path.Combine(repoRoot, "profiles");
    var files = Directory.EnumerateFiles(profileDirectory, "*.toml", SearchOption.TopDirectoryOnly).ToList();
    AssertTrue(files.Count > 0, "Expected at least one profile fixture.");

    using var temp = new TemporaryDirectory();
    foreach (var profilePath in files)
    {
        var originalBytes = File.ReadAllBytes(profilePath);
        var document = TomlProfileDocument.Load(profilePath);
        var savedPath = Path.Combine(temp.Path, Path.GetFileName(profilePath));
        document.Save(savedPath);
        var savedBytes = File.ReadAllBytes(savedPath);
        AssertSequenceEqual(originalBytes, savedBytes, Path.GetFileName(profilePath));
    }
}

static void LinePreservingScalarEdit()
{
    const string input =
        "[trainer]\r\n" +
        "name = \"alpha\" # keep\r\n" +
        "frame_rate_hz = 1000\r\n" +
        "\r\n" +
        "[mapper]\r\n" +
        "roll_gain = 0.30 # roll\r\n" +
        "pitch_gain = 0.30\r\n";
    const string expected =
        "[trainer]\r\n" +
        "name = \"alpha\" # keep\r\n" +
        "frame_rate_hz = 1000\r\n" +
        "\r\n" +
        "[mapper]\r\n" +
        "roll_gain = 0.42 # roll\r\n" +
        "pitch_gain = 0.30\r\n";

    var document = TomlProfileDocument.LoadText(input);
    var changed = document.SetRaw("mapper", "roll_gain", "0.42");
    AssertTrue(changed, "Expected the profile document to change.");
    AssertEqual(expected, document.ToText(), "Edited TOML text did not match.");
    AssertEqual(1, CountChangedLines(input, document.ToText()), "Only one line should change.");
}

static void LinePreservingArrayEdit()
{
    const string input =
        "[mapper]\r\n" +
        "output_shape_nodes = [] # keep shape note\r\n" +
        "return_shape_nodes = []\r\n";
    const string expected =
        "[mapper]\r\n" +
        "output_shape_nodes = [[0.25,0.2,0.1]] # keep shape note\r\n" +
        "return_shape_nodes = []\r\n";

    var document = TomlProfileDocument.LoadText(input);
    document.SetRaw("mapper", "output_shape_nodes", "[[0.25,0.2,0.1]]");
    AssertEqual(expected, document.ToText(), "Array value edit should preserve spacing and comment.");
}

static void CloneDefaultAndFolderMarkers()
{
    var sourceFixture = Path.Combine(FindRepoRoot(), "profiles", "whoop-linear.toml");
    AssertTrue(File.Exists(sourceFixture), "Missing whoop-linear.toml fixture.");

    using var temp = CreateTempRepoWithProfiles();
    var profilesDir = Path.Combine(temp.Path, "profiles");
    File.Copy(sourceFixture, Path.Combine(profilesDir, "whoop-linear.toml"));
    File.WriteAllText(Path.Combine(temp.Path, ".gx12-default-profile"), "whoop-linear.toml\r\n");

    var validator = new DelegateProfileValidator((_, profilePath) =>
        File.Exists(profilePath)
            ? ProfileValidationResult.Success()
            : ProfileValidationResult.Failure("missing clone"));
    var service = new ProfileDirectoryService(temp.Path);
    var repository = new ProfileRepository(service, validator);
    var profile = repository.LoadProfiles(repository.Paths).Single();

    var clonePath = repository.CloneProfile(profile);
    AssertEqual("whoop-linear-v2.toml", Path.GetFileName(clonePath), "Unexpected clone file name.");
    AssertEqual(1, validator.CallCount, "Clone should validate the new profile once.");

    var cloneDocument = TomlProfileDocument.Load(clonePath);
    AssertEqual("whoop-linear-v2", cloneDocument.GetString("trainer", "name"), "Clone should get a new trainer name.");

    service.SetDefaultProfileFileName(Path.Combine("ignored", "custom.toml"));
    var defaultMarker = File.ReadAllText(Path.Combine(temp.Path, ".gx12-default-profile")).Trim();
    AssertEqual("custom.toml", defaultMarker, "Default marker should store only the filename.");

    var changedPaths = service.SetProfileDirectory("alt-profiles");
    AssertTrue(Directory.Exists(changedPaths.ProfileDirectory), "Profile directory should be created.");
    var profileDirMarker = File.ReadAllText(Path.Combine(temp.Path, ".gx12-profile-dir")).Trim();
    AssertEqual(changedPaths.ProfileDirectory, profileDirMarker, "Profile directory marker should store the resolved path.");
}

static void InvalidSaveDoesNotWrite()
{
    using var temp = CreateTempRepoWithProfiles();
    var profilePath = Path.Combine(temp.Path, "profiles", "sample.toml");
    const string original =
        "[trainer]\r\n" +
        "name = \"sample\"\r\n" +
        "frame_rate_hz = 1000\r\n";
    File.WriteAllText(profilePath, original);

    var validator = new DelegateProfileValidator((_, path) =>
        File.ReadAllText(path).Contains("frame_rate_hz = -1", StringComparison.Ordinal)
            ? ProfileValidationResult.Failure("frame rate rejected")
            : ProfileValidationResult.Success());
    var repository = new ProfileRepository(new ProfileDirectoryService(temp.Path), validator);

    var failed = repository.SaveProfileValue(profilePath, "trainer", "frame_rate_hz", "-1");
    AssertTrue(!failed.IsSuccess, "Invalid save should fail.");
    AssertEqual(original, File.ReadAllText(profilePath), "Invalid save changed the source profile.");

    var saved = repository.SaveProfileValue(profilePath, "trainer", "frame_rate_hz", "2000");
    AssertTrue(saved.IsSuccess && saved.Changed, "Valid save should succeed and report a change.");
    AssertTrue(File.ReadAllText(profilePath).Contains("frame_rate_hz = 2000", StringComparison.Ordinal),
        "Valid save did not update the intended value.");
}

static void BatchSaveSwitchesLeftStickSources()
{
    using var temp = CreateTempRepoWithProfiles();
    var profilePath = Path.Combine(temp.Path, "profiles", "sample.toml");
    const string original =
        "[trainer]\r\n" +
        "name = \"sample\"\r\n" +
        "frame_rate_hz = 1000\r\n" +
        "\r\n" +
        "[control]\r\n" +
        "mode = \"direct_mouse\"\r\n" +
        "\r\n" +
        "[keyboard_left_stick]\r\n" +
        "enabled = true\r\n" +
        "\r\n" +
        "[mouse_left_stick]\r\n" +
        "enabled = false\r\n" +
        "\r\n" +
        "[right_mouse_left_stick]\r\n" +
        "enabled = false\r\n";
    File.WriteAllText(profilePath, original);

    var validator = new DelegateProfileValidator((_, path) =>
    {
        var document = TomlProfileDocument.Load(path);
        var enabledCount =
            (document.GetBool("keyboard_left_stick", "enabled") ? 1 : 0) +
            (document.GetBool("mouse_left_stick", "enabled") ? 1 : 0) +
            (document.GetBool("right_mouse_left_stick", "enabled") ? 1 : 0);
        return enabledCount > 1
            ? ProfileValidationResult.Failure("left-stick sources conflict")
            : ProfileValidationResult.Success();
    });
    var repository = new ProfileRepository(new ProfileDirectoryService(temp.Path), validator);

    var failedSingle = repository.SaveProfileValue(profilePath, "mouse_left_stick", "enabled", "true");
    AssertTrue(!failedSingle.IsSuccess, "Single-field switch should expose the transient conflict.");
    AssertEqual(original, File.ReadAllText(profilePath), "Failed single-field switch changed the profile.");

    var savedBatch = repository.SaveProfileValues(profilePath, new[]
    {
        new ProfileValueUpdate("keyboard_left_stick", "enabled", "false"),
        new ProfileValueUpdate("mouse_left_stick", "enabled", "true")
    });
    AssertTrue(savedBatch.IsSuccess && savedBatch.Changed, "Batch source switch should save.");

    var saved = TomlProfileDocument.Load(profilePath);
    AssertTrue(!saved.GetBool("keyboard_left_stick", "enabled"), "Keyboard source should be disabled.");
    AssertTrue(saved.GetBool("mouse_left_stick", "enabled"), "Second mouse source should be enabled.");
    AssertTrue(!saved.GetBool("right_mouse_left_stick", "enabled"), "Right-mouse button/scroll source should be disabled.");

    var savedRightMouse = repository.SaveProfileValues(profilePath, new[]
    {
        new ProfileValueUpdate("mouse_left_stick", "enabled", "false"),
        new ProfileValueUpdate("right_mouse_left_stick", "enabled", "true")
    });
    AssertTrue(savedRightMouse.IsSuccess && savedRightMouse.Changed, "Batch switch to right mouse buttons/scroll should save.");

    saved = TomlProfileDocument.Load(profilePath);
    AssertTrue(!saved.GetBool("keyboard_left_stick", "enabled"), "Keyboard source should stay disabled.");
    AssertTrue(!saved.GetBool("mouse_left_stick", "enabled"), "Second mouse source should be disabled.");
    AssertTrue(saved.GetBool("right_mouse_left_stick", "enabled"), "Right-mouse button/scroll source should be enabled.");
}

static void SettingsSchemaCoversV3AndProfiles()
{
    var repoRoot = FindRepoRoot();
    var schemaPaths = ProfileSchema.Definitions
        .Select(definition => definition.TomlPath)
        .ToHashSet(StringComparer.OrdinalIgnoreCase);

    AssertEqual(
        ProfileSchema.Definitions.Count,
        schemaPaths.Count,
        "Settings schema should not contain duplicate TOML paths.");

    var v3Source = File.ReadAllText(Path.Combine(repoRoot, "tools", "gx12-launcher-v3.ps1"));
    var v3Paths = Regex
        .Matches(
            v3Source,
            @"Set-Toml(?:Array)?Value\s+-Lines\s+\$lines\s+-Section\s+'(?<section>[^']+)'\s+-Key\s+'(?<key>[^']+)'",
            RegexOptions.CultureInvariant)
        .Select(match => $"{match.Groups["section"].Value}.{match.Groups["key"].Value}")
        .Distinct(StringComparer.OrdinalIgnoreCase)
        .OrderBy(path => path, StringComparer.OrdinalIgnoreCase)
        .ToList();

    AssertTrue(v3Paths.Count > 0, "Expected to discover V3 profile save fields.");
    var missingV3Paths = v3Paths
        .Where(path => !schemaPaths.Contains(path))
        .ToList();
    AssertTrue(
        missingV3Paths.Count == 0,
        $"Settings schema is missing V3 field(s): {string.Join(", ", missingV3Paths)}");

    var fixturePaths = Directory
        .EnumerateFiles(Path.Combine(repoRoot, "profiles"), "*.toml", SearchOption.TopDirectoryOnly)
        .SelectMany(path => TomlProfileDocument.Load(path).GetValuePaths())
        .Distinct(StringComparer.OrdinalIgnoreCase)
        .OrderBy(path => path, StringComparer.OrdinalIgnoreCase)
        .ToList();
    var missingFixturePaths = fixturePaths
        .Where(path => !schemaPaths.Contains(path))
        .ToList();
    AssertTrue(
        missingFixturePaths.Count == 0,
        $"Settings schema is missing profile fixture field(s): {string.Join(", ", missingFixturePaths)}");
}

static void SettingsCatalogRowsSearchLabelsAndKeys()
{
    var document = TomlProfileDocument.LoadText(
        "[mapper]\r\n" +
        "roll_gain = 0.42\r\n" +
        "input_filter = \"one_euro\"\r\n");
    var rollDefinition = ProfileSchema.Definitions.Single(definition =>
        definition.TomlPath.Equals("mapper.roll_gain", StringComparison.OrdinalIgnoreCase));
    var rollRow = new SettingCatalogRow(rollDefinition, document);

    AssertEqual("0.42", rollRow.DisplayValue, "Catalog row should expose the current raw value.");
    AssertTrue(rollRow.IsChangedFromDefault, "Changed-from-default should be detected for roll_gain.");
    AssertTrue(rollRow.MatchesSearch("roll gain"), "Friendly label search should match.");
    AssertTrue(rollRow.MatchesSearch("mapper.roll_gain"), "TOML path search should match.");

    var filterDefinition = ProfileSchema.Definitions.Single(definition =>
        definition.TomlPath.Equals("mapper.input_filter", StringComparison.OrdinalIgnoreCase));
    var filterRow = new SettingCatalogRow(filterDefinition, document);
    AssertTrue(!filterRow.IsInvalid, "Allowed enum value should not be invalid.");

    var invalidDocument = TomlProfileDocument.LoadText("[mapper]\r\ninput_filter = \"mystery\"\r\n");
    var invalidFilterRow = new SettingCatalogRow(filterDefinition, invalidDocument);
    AssertTrue(invalidFilterRow.IsInvalid, "Unknown enum value should be marked invalid.");
}

static void ProfileEditorAppliesCatalogEdits()
{
    using var temp = CreateTempRepoWithProfiles();
    var profilePath = Path.Combine(temp.Path, "profiles", "sample.toml");
    File.WriteAllText(
        profilePath,
        "[trainer]\r\n" +
        "name = \"sample\"\r\n" +
        "frame_rate_hz = 1000\r\n" +
        "\r\n" +
        "[mapper]\r\n" +
        "roll_gain = 0.30\r\n" +
        "input_filter = \"off\"\r\n");

    var repository = new ProfileRepository(
        new ProfileDirectoryService(temp.Path),
        new DelegateProfileValidator((_, _) => ProfileValidationResult.Success("ok")));
    var profile = repository.LoadProfiles(repository.Paths).Single();
    var editor = new ProfileEditorViewModel(
        repository,
        profile,
        new UiSettingsService(),
        new NullTooltipImagePicker());

    editor.SelectedCatalogRow = editor.CatalogRows.Single(row =>
        row.TomlPath.Equals("mapper.input_filter", StringComparison.OrdinalIgnoreCase));
    editor.CatalogEditValue = "one_euro";
    editor.ApplyCatalogValueCommand.Execute(null);

    var saved = TomlProfileDocument.Load(profilePath);
    AssertEqual("one_euro", saved.GetString("mapper", "input_filter", ""), "Catalog enum edit should save quoted TOML.");

    editor.SelectedCatalogRow = editor.CatalogRows.Single(row =>
        row.TomlPath.Equals("mapper.roll_gain", StringComparison.OrdinalIgnoreCase));
    editor.CatalogEditValue = "0.42";
    editor.ApplyCatalogValueCommand.Execute(null);

    saved = TomlProfileDocument.Load(profilePath);
    AssertEqual("0.42", saved.GetRaw("mapper", "roll_gain", ""), "Catalog number edit should save raw TOML.");

    editor.ResetCatalogValueCommand.Execute(null);
    saved = TomlProfileDocument.Load(profilePath);
    AssertEqual("50.0", saved.GetRaw("mapper", "roll_gain", ""), "Catalog default reset should write schema default.");
}

static void ProfileEditorToggles2xResolutionMode()
{
    using var temp = CreateTempRepoWithProfiles();
    var profilePath = Path.Combine(temp.Path, "profiles", "sample.toml");
    File.WriteAllText(
        profilePath,
        "[trainer]\r\n" +
        "name = \"sample\"\r\n" +
        "frame_rate_hz = 1000\r\n" +
        "\r\n" +
        "[control]\r\n" +
        "mode = \"direct_mouse\"\r\n" +
        "\r\n" +
        "[mouse_right_stick]\r\n" +
        "enabled = true\r\n" +
        "\r\n" +
        "[mapper]\r\n" +
        "max_output = 512\r\n");

    var repository = new ProfileRepository(
        new ProfileDirectoryService(temp.Path),
        new DelegateProfileValidator((_, _) => ProfileValidationResult.Success("ok")));
    var profile = repository.LoadProfiles(repository.Paths).Single();
    var editor = new ProfileEditorViewModel(
        repository,
        profile,
        new UiSettingsService(),
        new NullTooltipImagePicker());

    AssertTrue(!editor.HighResolution2xEnabled, "Profiles should default to legacy resolution mode.");

    editor.HighResolution2xEnabled = true;
    var saved = TomlProfileDocument.Load(profilePath);
    AssertEqual("gx12_2x", saved.GetString("trainer", "resolution_mode", ""), "2x toggle should save trainer.resolution_mode.");
    AssertEqual("gx12_2x", editor.ResolutionMode, "2x toggle should update the editor mode.");

    var summary = repository.LoadProfiles(repository.Paths).Single();
    AssertTrue(
        summary.Badges.Any(badge => badge.Label.Equals("2x resolution", StringComparison.OrdinalIgnoreCase)),
        "2x profiles should be visible in the profile rail badges.");

    editor.HighResolution2xEnabled = false;
    saved = TomlProfileDocument.Load(profilePath);
    AssertEqual("legacy", saved.GetString("trainer", "resolution_mode", ""), "2x toggle should restore legacy mode.");
}

static void ProfileBadgesDoNotMarkTuningAsExperimental()
{
    using var temp = CreateTempRepoWithProfiles();
    var profilePath = Path.Combine(temp.Path, "profiles", "sample.toml");
    File.WriteAllText(
        profilePath,
        "[trainer]\r\n" +
        "name = \"sample\"\r\n" +
        "\r\n" +
        "[control]\r\n" +
        "mode = \"direct_mouse\"\r\n" +
        "\r\n" +
        "[mouse_right_stick]\r\n" +
        "enabled = true\r\n" +
        "\r\n" +
        "[mapper]\r\n" +
        "output_shaping_enabled = true\r\n" +
        "gimbal_antiwindup_enabled = true\r\n");

    var repository = new ProfileRepository(
        new ProfileDirectoryService(temp.Path),
        new DelegateProfileValidator((_, _) => ProfileValidationResult.Success("ok")));
    var summary = repository.LoadProfiles(repository.Paths).Single();

    AssertTrue(
        summary.Badges.Any(badge => badge.Label.Equals("Shaping", StringComparison.OrdinalIgnoreCase)),
        "Profiles using shaping controls should still expose the shaping badge.");
    AssertTrue(
        summary.Badges.All(badge => !badge.Label.Equals("Experimental", StringComparison.OrdinalIgnoreCase)),
        "Profile rail badges should not label working tuning features as experimental.");
}

static void ProfileEditorOptionVisibilityFollowsSelectedModes()
{
    using var temp = CreateTempRepoWithProfiles();
    var profilePath = Path.Combine(temp.Path, "profiles", "sample.toml");
    File.WriteAllText(
        profilePath,
        "[trainer]\r\n" +
        "name = \"sample\"\r\n" +
        "\r\n" +
        "[control]\r\n" +
        "mode = \"direct_mouse\"\r\n" +
        "\r\n" +
        "[mouse_right_stick]\r\n" +
        "enabled = true\r\n" +
        "\r\n" +
        "[mapper]\r\n" +
        "output_curve = \"expo\"\r\n" +
        "input_filter = \"off\"\r\n" +
        "position_model = \"integrator\"\r\n" +
        "input_gain_mode = \"flat\"\r\n" +
        "gate_shape = \"axis\"\r\n");

    var repository = new ProfileRepository(
        new ProfileDirectoryService(temp.Path),
        new DelegateProfileValidator((_, _) => ProfileValidationResult.Success("ok")));
    var profile = repository.LoadProfiles(repository.Paths).Single();
    var editor = new ProfileEditorViewModel(
        repository,
        profile,
        new UiSettingsService(),
        new NullTooltipImagePicker());

    AssertTrue(editor.IsRightStickBasicActive, "Direct mouse with right stick enabled should expose right-stick basics.");
    AssertTrue(!editor.MouseAimEnabled, "Reticle-aim toggle should reflect direct-mouse mode.");
    AssertTrue(editor.IsOutputCurveExpoSelected, "Expo parameters should be visible when Expo is selected.");
    AssertTrue(!editor.IsOutputCurveActualSelected, "Actual-rate parameters should be hidden unless Actual is selected.");
    AssertTrue(!editor.IsIdleReturnActive, "Idle return parameters should be hidden until idle return is enabled.");

    editor.ReturnEnabled = true;
    editor.ConstantReturnEnabled = true;
    editor.ElasticReturnEnabled = true;
    AssertTrue(editor.IsIdleReturnActive, "Idle return parameters should appear when idle return is enabled.");
    AssertTrue(editor.IsConstantReturnActive, "Constant return parameters should appear when constant return is enabled.");
    AssertTrue(editor.IsElasticReturnActive, "Elastic return parameters should appear when elastic return is enabled.");

    editor.OutputCurve = "actual";
    AssertTrue(editor.IsOutputCurveActualSelected, "Actual-rate parameters should appear when Actual rates is selected.");
    AssertTrue(!editor.IsOutputCurveExpoSelected, "Expo parameters should hide when Actual rates is selected.");
    editor.OutputCurve = "nodes";
    AssertTrue(editor.IsOutputShapeActive, "Output graph should be active only for shape nodes.");

    editor.InputFilter = "smoothing";
    AssertTrue(editor.IsInputFilterSmoothingSelected, "Smoothing parameter should follow the smoothing filter.");
    AssertTrue(!editor.IsInputFilterOneEuroSelected, "One Euro parameters should hide for smoothing.");
    editor.InputFilter = "one_euro";
    AssertTrue(editor.IsInputFilterOneEuroSelected, "One Euro parameters should follow the One Euro filter.");
    editor.DespikeEnabled = true;
    AssertTrue(editor.IsDespikeActive, "Despike parameters should follow the despike checkbox.");

    editor.PositionModel = "dynamic_gimbal";
    AssertTrue(editor.IsDynamicGimbalSelected, "Dynamic gimbal parameters should follow the position model.");
    editor.GimbalAntiwindupEnabled = false;
    AssertTrue(!editor.IsGimbalAntiwindupParameterActive, "Anti-windup parameters should hide when anti-windup is off.");
    editor.GimbalAntiwindupEnabled = true;
    AssertTrue(editor.IsGimbalAntiwindupParameterActive, "Anti-windup parameters should appear when anti-windup is on.");
    editor.InputGainMode = "adaptive";
    AssertTrue(editor.IsAdaptiveGainSelected, "Adaptive gain panels should follow adaptive gain mode.");
    editor.GateShape = "circle";
    AssertTrue(editor.IsRadialGateSelected, "Diagonal scale should appear only for radial gate shapes.");

    editor.LeftStickSource = ProfileEditorViewModel.LeftSourceMouse;
    editor.MouseLeftYawShapingEnabled = true;
    editor.MouseLeftYawOutputCurve = "nodes";
    editor.MouseLeftYawReturnShapingEnabled = true;
    AssertTrue(editor.IsMouseLeftYawOutputShapeActive, "Left-yaw output graph should require yaw shaping and nodes.");
    AssertTrue(editor.IsMouseLeftYawReturnShapeActive, "Left-yaw return graph should require yaw shaping and return nodes.");

    editor.LeftStickSource = ProfileEditorViewModel.LeftSourceRightMouseButtonsScroll;
    AssertTrue(editor.IsRightMouseLeftSelected, "Right-mouse button/scroll parameters should show for that source.");
    AssertTrue(!editor.IsMouseLeftSelected, "Second-mouse parameters should hide for the right-mouse button/scroll source.");

    editor.ControlMode = "drone_mouse_aim";
    AssertTrue(editor.MouseAimEnabled, "Reticle-aim toggle should reflect reticle-aim mode.");
    AssertTrue(!editor.IsRightStickBasicActive, "Direct right-stick parameters should hide in reticle-aim mode.");
    AssertTrue(!editor.IsMouseLeftSelected, "Second-mouse parameters should hide in reticle-aim mode.");
    AssertTrue(!editor.IsRightMouseLeftSelected, "Right-mouse button/scroll parameters should hide in reticle-aim mode.");

    editor.MouseAimEnabled = false;
    AssertEqual("direct_mouse", editor.ControlMode, "Reticle-aim toggle should switch the profile back to direct mouse.");
    AssertTrue(!editor.MouseAimEnabled, "Reticle-aim toggle should clear when direct mouse is selected.");

    editor.MouseAimEnabled = true;
    AssertEqual("drone_mouse_aim", editor.ControlMode, "Reticle-aim toggle should enable the reticle-aim control mode.");
    var saved = TomlProfileDocument.Load(profilePath);
    AssertEqual("drone_mouse_aim", saved.GetString("control", "mode", ""), "Reticle-aim toggle should save control.mode.");
    AssertTrue(!saved.GetBool("mouse_left_stick", "enabled", true), "Reticle-aim toggle should disable the second-mouse left-stick source.");
    AssertTrue(!saved.GetBool("right_mouse_left_stick", "enabled", true), "Reticle-aim toggle should disable the right-mouse button/scroll left-stick source.");
}

static void StickShapeNodesParseClampAndFormat()
{
    var nodes = StickShapeCurve.ParseNodes("[[0.2500,0.2000,0.010], [1.5,2.0,2.0]]");

    AssertEqual(2, nodes.Count, "Expected two parsed shape nodes.");
    AssertNear(0.25, nodes[0].X, "First node X should parse.");
    AssertNear(0.20, nodes[0].Y, "First node Y should parse.");
    AssertNear(0.05, nodes[0].Width, "Width should clamp to the launcher editing floor.");
    AssertNear(1.0, nodes[1].X, "Node X should clamp high values.");
    AssertNear(1.0, nodes[1].Y, "Node Y should clamp high values.");
    AssertNear(1.0, nodes[1].Width, "Width should clamp high values.");
    AssertEqual("[[0.25,0.2,0.05], [1,1,1]]", StickShapeCurve.FormatNodes(nodes), "Formatted shape nodes should match V3 style.");
}

static void StickShapeCurveEvaluatesNodeBlend()
{
    var nodes = StickShapeCurve.ParseNodes("[[0.5,1,0.5]]");

    AssertNear(0.40, StickShapeCurve.Evaluate(0.40, Array.Empty<StickShapeNode>()), "Empty shape should be linear.");
    AssertNear(1.00, StickShapeCurve.Evaluate(0.50, nodes), "Curve should hit the node target at its center.");
    AssertNear(0.625, StickShapeCurve.Evaluate(0.25, nodes), "Curve should cosine-blend between linear and target.");
    AssertNear(1.00, StickShapeCurve.Evaluate(1.00, nodes), "Outside the node band should return to linear.");
}

static void GameInputMouseDeviceScanParsesSummaryOutput()
{
    var devices = GameInputMouseDeviceScan.ParseDevices(SampleGameInputDeviceOutput());

    AssertEqual(2, devices.Count, "Expected two parsed GameInput mouse devices.");
    AssertEqual("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", devices[0].RootToken, "Parser should keep the root token.");
    AssertEqual("11111111111111111111111111111111", devices[0].DeviceToken, "Parser should keep the device token.");
    AssertEqual("0x046d:0xc539", devices[0].VendorProductDisplay, "VID/PID should round-trip.");
    AssertEqual("dx +120 / dy -30", devices[0].MovementSummary, "Movement summary should use final totals.");
    AssertTrue(devices[0].PnpPath.StartsWith(@"HID\VID_046D", StringComparison.Ordinal), "Parser should attach the pnp line to the prior summary device.");
}

static void GameInputAssignmentCheckFlagsProfileTokenState()
{
    var report = GameInputMouseDeviceScan.Analyze(
        "auto",
        "auto",
        mouseRightEnabled: true,
        mouseLeftEnabled: true,
        mouseLeftRequireDevice: true,
        SampleGameInputDeviceOutput());

    AssertEqual("Ok", FindStatus(report, "Right").Kind, "Right auto should be OK when two moved devices were scanned.");
    AssertEqual("Ok", FindStatus(report, "Left").Kind, "Left auto should be OK when two moved devices were scanned.");
    AssertTrue(report.Summary.Contains("2 GameInput mouse device", StringComparison.Ordinal), "Summary should include device count.");

    var missingRight = GameInputMouseDeviceScan.Analyze(
        "cccccccccccccccccccccccccccccccc",
        "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
        mouseRightEnabled: true,
        mouseLeftEnabled: true,
        mouseLeftRequireDevice: true,
        SampleGameInputDeviceOutput());

    AssertEqual("Danger", FindStatus(missingRight, "Right").Kind, "Missing explicit right token should be a problem.");
    AssertEqual("Ok", FindStatus(missingRight, "Left").Kind, "Present explicit left token should pass.");
}

static void DiagnosticCommandsFormatGx12MouseArguments()
{
    var root = Path.Combine("test-root", "Asukaflight Test Repo");
    var profilePath = Path.Combine(root, "profiles", "whoop-linear.toml");
    var exePath = Path.Combine(root, "runtime", "gx12mouse.exe");
    var paths = new AppPaths(
        root,
        Path.Combine(root, "profiles"),
        "whoop-linear.toml",
        exePath,
        Path.Combine(root, ".gx12-profile-dir"),
        Path.Combine(root, ".gx12-default-profile"));
    var service = new Gx12DiagnosticsService();

    var devices = service.BuildMouseDevicesGameInput(paths, 10);
    AssertEqual(
        $"\"{exePath}\" --mouse-devices-gameinput 10",
        devices.CommandLine,
        "GameInput device enumeration command should be copyable.");
    AssertEqual(
        "--mouse-devices-gameinput|10",
        string.Join("|", devices.Arguments),
        "GameInput device enumeration arguments should match gx12mouse.");

    var showProfile = service.BuildShowProfile(paths, profilePath);
    AssertEqual(
        $"\"{exePath}\" --show-profile \"{profilePath}\"",
        showProfile.CommandLine,
        "Show-profile command should quote profile paths.");

    var leftDryRun = service.BuildMouseLeftDryRun(paths, profilePath, 12);
    AssertEqual(
        $"--mouse-left-dry-run|{profilePath}|12",
        string.Join("|", leftDryRun.Arguments),
        "Second-mouse dry-run arguments should include profile and duration.");

    var preview = service.BuildGimbalPreview(paths, profilePath);
    AssertEqual(
        $"--gimbal-preview|{profilePath}",
        string.Join("|", preview.Arguments),
        "Gimbal preview arguments should include the selected profile.");
}

static void DiagnosticRunnerReportsMissingExecutable()
{
    using var temp = new TemporaryDirectory();
    var profilePath = Path.Combine(temp.Path, "profile.toml");
    File.WriteAllText(profilePath, "[trainer]\r\nname = \"sample\"\r\n");
    var paths = new AppPaths(
        temp.Path,
        temp.Path,
        "profile.toml",
        Path.Combine(temp.Path, "missing-gx12mouse.exe"),
        Path.Combine(temp.Path, ".gx12-profile-dir"),
        Path.Combine(temp.Path, ".gx12-default-profile"));
    var service = new Gx12DiagnosticsService();
    var command = service.BuildShowProfile(paths, profilePath);

    var result = service.RunAsync(paths, command, System.Threading.CancellationToken.None).GetAwaiter().GetResult();

    AssertTrue(!result.IsSuccess, "Missing executable should fail without launching a process.");
    AssertEqual("gx12mouse.exe is missing.", result.Message, "Missing executable message should be explicit.");
    AssertTrue(result.CommandLine.Contains("--show-profile", StringComparison.Ordinal), "Result should keep the copyable command line.");
}

static void RuntimeServiceFormatsTrainerCommandAndRejectsMissingInputs()
{
    using var temp = new TemporaryDirectory();
    var profilePath = Path.Combine(temp.Path, "profile.toml");
    File.WriteAllText(profilePath, "[trainer]\r\nname = \"sample\"\r\n");
    var paths = new AppPaths(
        temp.Path,
        temp.Path,
        "profile.toml",
        Path.Combine(temp.Path, "missing-gx12mouse.exe"),
        Path.Combine(temp.Path, ".gx12-profile-dir"),
        Path.Combine(temp.Path, ".gx12-default-profile"));
    var service = new Gx12RuntimeService();

    var commandLine = Gx12RuntimeService.BuildTrainerCommandLine(paths, profilePath);
    AssertTrue(commandLine.Contains("--trainer-profile", StringComparison.Ordinal),
        "Runtime command should use the composite trainer profile argument.");
    AssertTrue(commandLine.EndsWith(" live", StringComparison.Ordinal),
        "Runtime command should use live mode.");

    var integratedLine = Gx12RuntimeService.BuildTrainerCommandLine(
        paths,
        profilePath,
        Path.Combine(temp.Path, "logs", "sample.gx12rec.csv"),
        12,
        liveReload: true,
        recordingToggleKey: "F4",
        playbackLoop: true,
        playbackBindings: new[]
        {
            new PlaybackBindCommand(Path.Combine(temp.Path, "logs", "clip.gx12rec.csv"), "F5", "ail,ele", true)
        },
        recordingOverwrite: true,
        runtimeControlPath: Path.Combine(temp.Path, ".gx12-ui", "runtime-control.tsv"));
    AssertTrue(integratedLine.Contains("--trainer-profile", StringComparison.Ordinal),
        "Integrated runtime command should still use the normal trainer profile mode.");
    AssertTrue(integratedLine.Contains("--recording", StringComparison.Ordinal) &&
               integratedLine.Contains("--record-duration=12", StringComparison.Ordinal) &&
               integratedLine.Contains("--record-toggle=F4", StringComparison.Ordinal),
        "Integrated runtime command should arm recording inside the normal trainer process.");
    AssertTrue(integratedLine.Contains("--record-overwrite", StringComparison.Ordinal) &&
               integratedLine.Contains("--runtime-control", StringComparison.Ordinal),
        "Integrated runtime command should allow live recording/playback settings updates.");
    AssertTrue(integratedLine.Contains("--playback-loop", StringComparison.Ordinal) &&
               integratedLine.Contains("--bind-block F5 ail,ele", StringComparison.Ordinal),
        "Integrated runtime command should arm playback bank binds inside the normal trainer process.");

    var result = service.StartCompositeTrainer(paths, profilePath);
    AssertTrue(!result.IsSuccess, "Missing gx12mouse.exe should fail before start.");
    AssertTrue(result.Message.Contains("Missing executable", StringComparison.Ordinal),
        "Missing executable message should be explicit.");
    AssertEqual(commandLine, result.CommandLine, "Failed start should keep the copyable runtime command.");
}

static void RuntimeServiceFormatsRecordingAndPlaybackCommands()
{
    using var temp = new TemporaryDirectory();
    var profilePath = Path.Combine(temp.Path, "profile.toml");
    var recordingPath = Path.Combine(temp.Path, "logs", "sample.gx12rec.csv");
    var paths = new AppPaths(
        temp.Path,
        temp.Path,
        "profile.toml",
        Path.Combine(temp.Path, "gx12mouse.exe"),
        Path.Combine(temp.Path, ".gx12-profile-dir"),
        Path.Combine(temp.Path, ".gx12-default-profile"));

    var recordLine = Gx12RuntimeService.BuildTrainerRecordCommandLine(
        paths,
        profilePath,
        recordingPath,
        12,
        liveReload: true,
        recordingToggleKey: "F4",
        recordingOverwrite: true,
        runtimeControlPath: Path.Combine(temp.Path, ".gx12-ui", "runtime-control.tsv"));
    AssertTrue(recordLine.Contains("--trainer-record", StringComparison.Ordinal),
        "Recording command should use --trainer-record.");
    AssertTrue(recordLine.Contains(recordingPath, StringComparison.Ordinal),
        "Recording command should include the output recording path.");
    AssertTrue(recordLine.Contains(" 12 live ", StringComparison.Ordinal),
        "Recording command should include duration and live reload.");
    AssertTrue(recordLine.Contains("--record-toggle=F4", StringComparison.Ordinal),
        "Recording command should include the toggle key when configured.");
    AssertTrue(recordLine.Contains("--record-overwrite", StringComparison.Ordinal) &&
               recordLine.Contains("--runtime-control", StringComparison.Ordinal),
        "Recording command should include overwrite and live control options when configured.");

    var immediateRecordLine = Gx12RuntimeService.BuildTrainerRecordCommandLine(
        paths,
        profilePath,
        recordingPath,
        12,
        liveReload: false,
        recordingToggleKey: "off");
    AssertTrue(!immediateRecordLine.Contains("--record-toggle=", StringComparison.Ordinal),
        "Recording command should omit the toggle option when it is off.");

    var playbackLine = Gx12RuntimeService.BuildTrainerPlaybackCommandLine(
        paths,
        recordingPath,
        "COM6",
        true,
        "radio_ail,radio_ele,radio_thr",
        "Mouse4");
    AssertTrue(playbackLine.Contains("--trainer-playback", StringComparison.Ordinal),
        "Playback command should use --trainer-playback.");
    AssertTrue(playbackLine.Contains(" COM6 loop ", StringComparison.Ordinal),
        "Playback command should include port and loop mode.");
    AssertTrue(playbackLine.Contains("--channels=radio_ail,radio_ele,radio_thr", StringComparison.Ordinal),
        "Playback command should include the selected channel mask.");
    AssertTrue(playbackLine.Contains("--trigger=Mouse4", StringComparison.Ordinal),
        "Playback command should include the trigger when configured.");

    var immediateLine = Gx12RuntimeService.BuildTrainerPlaybackCommandLine(
        paths,
        recordingPath,
        "",
        false,
        "ail,ele",
        "immediate");
    AssertTrue(immediateLine.Contains(" auto once ", StringComparison.Ordinal),
        "Blank playback port should normalize to auto and once mode.");
    AssertTrue(!immediateLine.Contains("--trigger=", StringComparison.Ordinal),
        "Immediate trigger should not add an explicit trigger argument.");

    var bankLine = Gx12RuntimeService.BuildTrainerPlaybackBankCommandLine(
        paths,
        "auto",
        false,
        new[]
        {
            new PlaybackBindCommand(recordingPath, "F5", "ail,ele", true),
            new PlaybackBindCommand(Path.Combine(temp.Path, "logs", "second.gx12rec.csv"), "F6", "ail,ele,thr")
        });
    AssertTrue(bankLine.Contains("--trainer-playback-bank auto once", StringComparison.Ordinal),
        "Playback bank command should use the bank mode and shared port/mode.");
    AssertTrue(bankLine.Contains("--bind-block F5 ail,ele", StringComparison.Ordinal),
        "Playback bank command should include the first hotkey binding with live input blocking.");
    AssertTrue(bankLine.Contains("--bind F6 ail,ele,thr", StringComparison.Ordinal),
        "Playback bank command should include the second hotkey binding.");

    var controlText = Gx12RuntimeService.BuildRuntimeControlText(
        recordingPath,
        12,
        "F4",
        recordingOverwrite: true,
        playbackLoop: false,
        playbackBindings: new[]
        {
            new PlaybackBindCommand(recordingPath, "F5", "ail,ele", true)
        });
    AssertTrue(controlText.Contains(
            $"bind\tF5\tail,ele\t{recordingPath}\t1{Environment.NewLine}",
            StringComparison.Ordinal),
        "Runtime control file should serialize per-bind live input blocking.");

    var infoLine = Gx12RuntimeService.BuildRecordingInfoCommandLine(paths, recordingPath);
    AssertTrue(infoLine.Contains("--recording-info", StringComparison.Ordinal),
        "Recording info command should use --recording-info.");
}

static void NativePlaybackBankBindCanStopActivePlayback()
{
    var sourcePath = Path.Combine(FindRepoRoot(), "src", "main.cpp");
    var source = File.ReadAllText(sourcePath);

    AssertTrue(source.Contains("PlaybackRunShouldStop(control)", StringComparison.Ordinal),
        "Playback frame pacing should poll the shared playback stop control.");
    AssertTrue(source.Contains("playback_control.stop_trigger = &slot.spec.trigger", StringComparison.Ordinal),
        "Playback bank should arm the active bind key as a stop trigger for its own playback.");
    AssertTrue(source.Contains("playback_control.stop_trigger_was_down = true", StringComparison.Ordinal),
        "Playback bank should ignore the initial key-down edge that started playback.");
    AssertTrue(source.Contains("playback_control.stopped_by_trigger ? \"bind\" : \"no\"", StringComparison.Ordinal),
        "Playback bank should report active-bind cancellation without treating it as a whole-bank stop.");
    AssertTrue(source.Contains("playback_bank_integrated=true", StringComparison.Ordinal),
        "Trainer profile mode should load playback bank binds into the normal runtime process.");
    AssertTrue(source.Contains("ConsumePlaybackInputInjection", StringComparison.Ordinal) &&
               source.Contains("sample.mapper_tick != *last_mapper_tick", StringComparison.Ordinal),
        "Integrated playback should inject recorded raw input once per recorded mapper tick.");
    AssertTrue(source.Contains("!PlaybackChannelUsesInputInjection(slot.spec.mask, active_profile, ch)", StringComparison.Ordinal),
        "Integrated playback should not overwrite channels already handled through input injection.");
    AssertTrue(source.Contains("GameInputMouse4Mouse5Axis(last_right_mapper_buttons)", StringComparison.Ordinal),
        "Right-mouse left-stick playback should merge recorded Mouse4/Mouse5 state before mapping.");
    AssertTrue(source.Contains("trainer_flags |= PlaybackActiveFlags(slot.spec.mask, active_profile.resolution_mode)", StringComparison.Ordinal),
        "Integrated playback should merge playback channel masks into the live trainer frame flags.");
    AssertTrue(source.Contains("active_recording_options.overwrite_existing", StringComparison.Ordinal) &&
               source.Contains("recording_clip_base_path != active_recording_options.path", StringComparison.Ordinal),
        "Toggle recording should support overwrite mode and reset clip numbering when the selected file changes.");
    AssertTrue(source.Contains("LoadTrainerRuntimeControlFile", StringComparison.Ordinal) &&
               source.Contains("runtime_control_reload=ok", StringComparison.Ordinal),
        "The active trainer run should reload recording/playback file choices without a restart.");
}

static void RuntimeServiceWritesConsoleScriptWithDelayedResultReporting()
{
    using var temp = new TemporaryDirectory();
    var profilePath = Path.Combine(temp.Path, "profile.toml");
    var paths = new AppPaths(
        temp.Path,
        temp.Path,
        "profile.toml",
        Path.Combine(temp.Path, "gx12mouse.exe"),
        Path.Combine(temp.Path, ".gx12-profile-dir"),
        Path.Combine(temp.Path, ".gx12-default-profile"));

    var script = Gx12RuntimeService.BuildConsoleScriptText(paths, profilePath);
    var commandLine = Gx12RuntimeService.BuildTrainerCommandLine(paths, profilePath);
    var setResultLine = "set \"GX12_RESULT=%ERRORLEVEL%\"";
    var echoResultLine = "echo gx12mouse exited with code %GX12_RESULT%.";

    AssertTrue(script.Contains(commandLine, StringComparison.Ordinal),
        "Console script should run the same copyable trainer command.");
    AssertTrue(script.Contains($"{setResultLine}{Environment.NewLine}", StringComparison.Ordinal),
        "Console script should capture ERRORLEVEL on its own batch line.");
    AssertTrue(script.Contains($"{echoResultLine}{Environment.NewLine}", StringComparison.Ordinal),
        "Console script should report the captured exit code on a later batch line.");
    AssertTrue(script.IndexOf(setResultLine, StringComparison.Ordinal) < script.IndexOf(echoResultLine, StringComparison.Ordinal),
        "Console script should set GX12_RESULT before echoing it.");
}

static void DiagnosticLogStorePersistsRecentHistory()
{
    using var temp = new TemporaryDirectory();
    var paths = new AppPaths(
        temp.Path,
        temp.Path,
        "profile.toml",
        Path.Combine(temp.Path, "gx12mouse.exe"),
        Path.Combine(temp.Path, ".gx12-profile-dir"),
        Path.Combine(temp.Path, ".gx12-default-profile"));
    var service = new Gx12DiagnosticsService();
    var command = service.BuildShowProfile(paths, Path.Combine(temp.Path, "profile.toml"));
    var startedAt = new DateTimeOffset(2026, 5, 3, 12, 0, 0, TimeSpan.Zero);
    var completedAt = startedAt.AddMilliseconds(250);
    var result = new Gx12DiagnosticResult(
        true,
        "Show profile complete.",
        command.CommandLine,
        "profile ok\r\nsecond line\r\n",
        command.Name,
        startedAt,
        completedAt);
    var store = new Gx12DiagnosticLogStore();

    var saved = store.Save(paths, command, result);
    var recent = store.LoadRecent(paths, maxCount: 4);

    AssertTrue(File.Exists(saved.LogPath), "Diagnostic run should write a per-run log file.");
    AssertEqual(1, recent.Count, "Diagnostic history should load the persisted run.");
    AssertEqual("Show profile", recent[0].Name, "Persisted diagnostic name should round-trip.");
    AssertEqual("OK", recent[0].StatusLabel, "Persisted diagnostic status should be readable.");
    AssertEqual("profile ok", recent[0].OutputPreview, "History preview should use the first output line.");
    AssertTrue(store.ReadLog(recent[0]).Contains("command_line=", StringComparison.Ordinal),
        "Per-run log should include the copyable command line.");
}

static void UiSettingsServicePersistsTooltipImageMappings()
{
    using var temp = new TemporaryDirectory();
    var profilePath = Path.Combine(temp.Path, "profile.toml");
    const string profileText = "[mapper]\r\nroll_gain = 0.42\r\n";
    File.WriteAllText(profilePath, profileText);
    var sourceImage = Path.Combine(temp.Path, "source.png");
    File.WriteAllBytes(sourceImage, new byte[] { 0x89, 0x50, 0x4E, 0x47 });
    var paths = new AppPaths(
        temp.Path,
        temp.Path,
        "profile.toml",
        Path.Combine(temp.Path, "gx12mouse.exe"),
        Path.Combine(temp.Path, ".gx12-profile-dir"),
        Path.Combine(temp.Path, ".gx12-default-profile"));
    var service = new UiSettingsService();

    var imported = service.ImportTooltipImage(paths, "mapper.roll_gain", sourceImage);
    var loaded = service.Load(paths);
    var resolved = service.GetTooltipImage(paths, loaded, "mapper.roll_gain");

    AssertEqual("setting-tooltip-images/mapper.roll_gain.png", imported.RelativePath, "Setting tooltip image path should be stored relative to .gx12-ui.");
    AssertTrue(File.Exists(imported.FullPath), "Tooltip image should be copied into .gx12-ui.");
    AssertTrue(resolved is { Exists: true }, "Loaded tooltip image mapping should resolve to an existing file.");
    AssertEqual(profileText, File.ReadAllText(profilePath), "Tooltip image changes must not touch profile TOML.");
}

static void UiSettingsServiceExposesRandomTooltipSpriteFolder()
{
    using var temp = new TemporaryDirectory();
    var paths = CreateAppPaths(temp.Path);
    var service = new UiSettingsService();

    AssertEqual(
        Path.Combine(temp.Path, ".gx12-ui", "tooltip-sprites"),
        service.GetTooltipSpriteDirectory(paths),
        "Random tooltip sprites should live under the portable .gx12-ui folder.");
}

static void UiSettingsServiceExposesFixedUiSpritePath()
{
    using var temp = new TemporaryDirectory();
    var paths = CreateAppPaths(temp.Path);
    var service = new UiSettingsService();
    var spriteDirectory = Path.Combine(temp.Path, ".gx12-ui", "ui-sprites");

    AssertEqual(
        spriteDirectory,
        service.GetUiSpriteDirectory(paths),
        "Fixed UI sprites should live under the portable .gx12-ui folder.");
    AssertEqual(
        Path.Combine(spriteDirectory, "above.png"),
        service.GetAboveBarSpritePath(paths),
        "The above-bar sprite should resolve to above.png in the fixed UI sprite folder.");
}

static void UiSettingsServicePersistsAboveBarSpriteReturnSettings()
{
    using var temp = new TemporaryDirectory();
    var paths = CreateAppPaths(temp.Path);
    var service = new UiSettingsService();

    var defaults = service.Load(paths);
    AssertTrue(defaults.AboveBarSpriteRandomReturnDelay, "The panel sprite should keep the existing random-return behavior by default.");
    AssertEqual(60, defaults.AboveBarSpriteFixedReturnDelaySeconds, "The fixed panel-sprite return delay should have a stable default.");

    defaults.AboveBarSpriteRandomReturnDelay = false;
    defaults.AboveBarSpriteFixedReturnDelaySeconds = 42;
    defaults.RecordingOverwrite = true;
    defaults.PlaybackThrottle = true;
    defaults.PlaybackRudder = true;
    defaults.PlaybackRadioRightGimbal = true;
    defaults.PlaybackRecordedTrainerRight = true;
    defaults.PlaybackRadioLeftGimbal = true;
    defaults.PlaybackRecordedTrainerLeft = true;
    defaults.PlaybackBlockLiveInput = true;
    defaults.PlaybackBindings.Add(new PlaybackBindingSettings
    {
        Enabled = true,
        RecordingPath = "logs\\one.gx12rec.csv",
        Trigger = "F5",
        ChannelMask = "ail,ele",
        BlockLiveInput = true
    });
    service.Save(paths, defaults);

    var loaded = service.Load(paths);
    AssertTrue(!loaded.AboveBarSpriteRandomReturnDelay, "Panel sprite random/fixed mode should persist in .gx12-ui settings.");
    AssertEqual(42, loaded.AboveBarSpriteFixedReturnDelaySeconds, "Panel sprite fixed return seconds should persist in .gx12-ui settings.");
    AssertTrue(loaded.RecordingOverwrite, "Recording overwrite mode should persist in .gx12-ui settings.");
    AssertTrue(loaded.PlaybackThrottle, "Recording playback throttle toggle should persist in .gx12-ui settings.");
    AssertTrue(loaded.PlaybackRudder, "Recording playback rudder toggle should persist in .gx12-ui settings.");
    AssertTrue(!loaded.PlaybackRadioRightGimbal, "Recorded trainer right-stick mode should win over radio right-gimbal mode if both are set.");
    AssertTrue(loaded.PlaybackRecordedTrainerRight, "Recording playback recorded-trainer right-stick toggle should persist in .gx12-ui settings.");
    AssertTrue(!loaded.PlaybackRadioLeftGimbal, "Recorded trainer left-stick mode should win over radio left-gimbal mode if both are set.");
    AssertTrue(loaded.PlaybackRecordedTrainerLeft, "Recording playback recorded-trainer left-stick toggle should persist in .gx12-ui settings.");
    AssertTrue(loaded.PlaybackBlockLiveInput, "Recording playback live-input block toggle should persist in .gx12-ui settings.");
    AssertEqual(1, loaded.PlaybackBindings.Count, "Playback hotkey binds should persist in .gx12-ui settings.");
    AssertEqual("F5", loaded.PlaybackBindings[0].Trigger, "Playback bind trigger should round-trip.");
    AssertTrue(loaded.PlaybackBindings[0].BlockLiveInput, "Playback bind live-input block setting should round-trip.");

    loaded.AboveBarSpriteFixedReturnDelaySeconds = -5;
    service.Save(paths, loaded);
    AssertEqual(
        UiSettingsService.MinAboveBarSpriteFixedReturnDelaySeconds,
        service.Load(paths).AboveBarSpriteFixedReturnDelaySeconds,
        "Panel sprite fixed return seconds should be clamped before saving.");
}

static void TooltipSpriteServiceEnumeratesPngDropFolder()
{
    using var temp = new TemporaryDirectory();
    var spriteDirectory = Path.Combine(temp.Path, ".gx12-ui", "tooltip-sprites");
    Directory.CreateDirectory(spriteDirectory);
    File.WriteAllBytes(Path.Combine(spriteDirectory, "roll.png"), new byte[] { 0x89, 0x50 });
    File.WriteAllBytes(Path.Combine(spriteDirectory, "pitch.PNG"), new byte[] { 0x89, 0x50 });
    File.WriteAllText(Path.Combine(spriteDirectory, "notes.txt"), "ignored");

    var sprites = TooltipSpriteService.EnumerateSpritePaths(spriteDirectory);

    AssertEqual(2, sprites.Count, "Only PNG files from the sprite drop folder should be used.");
    AssertTrue(
        sprites.All(path => Path.GetExtension(path).Equals(".png", StringComparison.OrdinalIgnoreCase)),
        "Tooltip sprite enumeration should ignore non-PNG files.");
}

static void CatalogRowHandlesMissingTooltipImageMappings()
{
    using var temp = new TemporaryDirectory();
    var paths = new AppPaths(
        temp.Path,
        temp.Path,
        "profile.toml",
        Path.Combine(temp.Path, "gx12mouse.exe"),
        Path.Combine(temp.Path, ".gx12-profile-dir"),
        Path.Combine(temp.Path, ".gx12-default-profile"));
    var sourceImage = Path.Combine(temp.Path, "source.bmp");
    File.WriteAllBytes(sourceImage, new byte[] { 0x42, 0x4D, 0x00, 0x00 });
    var service = new UiSettingsService();
    var imported = service.ImportTooltipImage(paths, "mapper.roll_gain", sourceImage);
    File.Delete(imported.FullPath);
    var settings = service.Load(paths);
    var info = service.GetTooltipImage(paths, settings, "mapper.roll_gain");
    var definition = ProfileSchema.Definitions.Single(definition =>
        definition.TomlPath.Equals("mapper.roll_gain", StringComparison.OrdinalIgnoreCase));
    var row = new SettingCatalogRow(
        definition,
        TomlProfileDocument.LoadText("[mapper]\r\nroll_gain = 0.42\r\n"),
        info);

    AssertTrue(row.IsTooltipImageMissing, "Catalog row should flag missing image mappings.");
    AssertTrue(!row.HasTooltipImage, "Missing images should not expose an Image source.");
    AssertEqual("Missing", row.TooltipImageBadge, "Catalog badge should summarize missing images.");
}

static void MainViewModelKeepsEditorDuringTransientSelectedProfileClears()
{
    using var temp = CreateTempRepoWithProfiles();
    var profilePath = Path.Combine(temp.Path, "profiles", "whoop-linear.toml");
    File.WriteAllText(
        profilePath,
        "[trainer]\r\n" +
        "name = \"whoop-linear\"\r\n" +
        "frame_rate_hz = 1000\r\n" +
        "\r\n" +
        "[mapper]\r\n" +
        "roll_gain = 0.3\r\n" +
        "pitch_gain = 0.3\r\n" +
        "max_output = 512\r\n");
    File.WriteAllText(Path.Combine(temp.Path, ".gx12-default-profile"), "whoop-linear.toml\r\n");

    var repository = new ProfileRepository(
        new ProfileDirectoryService(temp.Path),
        new DelegateProfileValidator((_, _) => ProfileValidationResult.Success("ok")));
    var viewModel = new MainViewModel(
        repository,
        new NullProfileFolderPicker(),
        new UiSettingsService(),
        new NullTooltipImagePicker(),
        new Gx12RuntimeService());

    var editor = viewModel.Editor;
    AssertTrue(editor is not null, "Expected the default profile to create an editor.");
    AssertEqual("whoop-linear.toml", viewModel.SelectedProfile?.FileName, "Expected the default profile to be selected.");

    viewModel.SelectedProfile = null;
    AssertTrue(ReferenceEquals(editor, viewModel.Editor), "Transient null selection should not replace the active editor.");
    AssertEqual("whoop-linear.toml", viewModel.SelectedProfile?.FileName, "Transient null selection should keep the selected profile.");

    viewModel.Profiles.Clear();
    viewModel.SelectedProfile = null;
    AssertTrue(viewModel.Editor is null, "Clearing the actual profile list should still clear the editor.");
}

static void MainViewModelFormatsRadioGimbalPlaybackMask()
{
    using var temp = CreateTempRepoWithProfiles();
    var repository = new ProfileRepository(
        new ProfileDirectoryService(temp.Path),
        new DelegateProfileValidator((_, _) => ProfileValidationResult.Success("ok")));
    var viewModel = new MainViewModel(
        repository,
        new NullProfileFolderPicker(),
        new UiSettingsService(),
        new NullTooltipImagePicker(),
        new Gx12RuntimeService());

    AssertEqual("trainer_ail,trainer_ele", viewModel.PlaybackChannelMask, "Default playback should use the recorded final trainer right-stick channels.");
    viewModel.PlaybackRecordedTrainerRight = false;
    AssertEqual("ail,ele", viewModel.PlaybackChannelMask, "Raw PC mouse playback should remain selectable.");
    viewModel.PlaybackRadioRightGimbal = true;
    AssertEqual("radio_ail,radio_ele", viewModel.PlaybackChannelMask, "Radio right-gimbal mode should switch Ail/Ele to HID channels.");
    viewModel.PlaybackRecordedTrainerRight = true;
    AssertEqual("trainer_ail,trainer_ele", viewModel.PlaybackChannelMask, "Recorded trainer mode should switch Ail/Ele to final trainer channels.");
    AssertTrue(!viewModel.PlaybackRadioRightGimbal, "Recorded trainer mode should clear radio right-gimbal mode.");
    viewModel.PlaybackThrottle = true;
    viewModel.PlaybackRudder = true;
    AssertEqual("trainer_ail,trainer_ele,radio_thr,radio_rud", viewModel.PlaybackChannelMask, "Recorded trainer right mode should combine with radio left-gimbal channels.");
    viewModel.PlaybackRecordedTrainerLeft = true;
    AssertEqual("trainer_ail,trainer_ele,trainer_thr,trainer_rud", viewModel.PlaybackChannelMask, "Recorded trainer left mode should switch Thr/Rud to final trainer channels.");
    AssertTrue(!viewModel.PlaybackRadioLeftGimbal, "Recorded trainer left mode should clear radio left-gimbal mode.");
    viewModel.PlaybackRecordedTrainerLeft = false;
    AssertEqual("trainer_ail,trainer_ele,thr,rud", viewModel.PlaybackChannelMask, "Clearing both left source modes should select PC left-source reconstruction.");
    viewModel.PlaybackRadioLeftGimbal = true;
    AssertEqual("trainer_ail,trainer_ele,radio_thr,radio_rud", viewModel.PlaybackChannelMask, "Radio left-gimbal mode should switch Thr/Rud to HID channels.");
    viewModel.PlaybackRadioRightGimbal = true;
    AssertEqual("radio_ail,radio_ele,radio_thr,radio_rud", viewModel.PlaybackChannelMask, "Radio right-gimbal mode should still be selectable after recorded trainer mode.");
    AssertTrue(!viewModel.PlaybackRecordedTrainerRight, "Radio right-gimbal mode should clear recorded trainer mode.");
}

static void MainViewModelFallsBackToHidLeftPlaybackWithoutPcLeftSource()
{
    using var temp = CreateTempRepoWithProfiles();
    var profilePath = Path.Combine(temp.Path, "profiles", "whoop-linear.toml");
    File.WriteAllText(
        profilePath,
        "[trainer]\r\n" +
        "name = \"whoop-linear\"\r\n" +
        "frame_rate_hz = 1000\r\n");
    File.WriteAllText(Path.Combine(temp.Path, ".gx12-default-profile"), "whoop-linear.toml\r\n");

    var repository = new ProfileRepository(
        new ProfileDirectoryService(temp.Path),
        new DelegateProfileValidator((_, _) => ProfileValidationResult.Success("ok")));
    var viewModel = new MainViewModel(
        repository,
        new NullProfileFolderPicker(),
        new UiSettingsService(),
        new NullTooltipImagePicker(),
        new Gx12RuntimeService());

    viewModel.PlaybackRecordedTrainerRight = false;
    viewModel.PlaybackRadioLeftGimbal = false;
    viewModel.PlaybackRecordedTrainerLeft = false;
    viewModel.PlaybackThrottle = true;
    viewModel.PlaybackRudder = true;
    AssertEqual("ail,ele,radio_thr,radio_rud", viewModel.PlaybackChannelMask,
        "Thr/Rud should fall back to recorded GX12 HID when the selected profile has no PC left-stick playback source.");

    File.AppendAllText(
        profilePath,
        "\r\n[right_mouse_left_stick]\r\n" +
        "enabled = true\r\n");
    var rawLeftRepository = new ProfileRepository(
        new ProfileDirectoryService(temp.Path),
        new DelegateProfileValidator((_, _) => ProfileValidationResult.Success("ok")));
    var rawLeftViewModel = new MainViewModel(
        rawLeftRepository,
        new NullProfileFolderPicker(),
        new UiSettingsService(),
        new NullTooltipImagePicker(),
        new Gx12RuntimeService());

    rawLeftViewModel.PlaybackRecordedTrainerRight = false;
    rawLeftViewModel.PlaybackRadioLeftGimbal = false;
    rawLeftViewModel.PlaybackRecordedTrainerLeft = false;
    rawLeftViewModel.PlaybackThrottle = true;
    rawLeftViewModel.PlaybackRudder = true;
    AssertEqual("ail,ele,thr,rud", rawLeftViewModel.PlaybackChannelMask,
        "Bare Thr/Rud should remain selectable when the selected profile has a PC left-stick playback source.");
}

static void MainViewModelPersistsRecordingPlaybackChannelToggles()
{
    using var temp = CreateTempRepoWithProfiles();
    var repository = new ProfileRepository(
        new ProfileDirectoryService(temp.Path),
        new DelegateProfileValidator((_, _) => ProfileValidationResult.Success("ok")));
    var uiSettingsService = new UiSettingsService();
    var viewModel = new MainViewModel(
        repository,
        new NullProfileFolderPicker(),
        uiSettingsService,
        new NullTooltipImagePicker(),
        new Gx12RuntimeService());

    viewModel.PlaybackAileron = false;
    viewModel.PlaybackThrottle = true;
    viewModel.PlaybackRudder = true;
    viewModel.PlaybackRecordedTrainerRight = true;
    viewModel.PlaybackRecordedTrainerLeft = true;
    viewModel.PlaybackBlockLiveInput = true;

    var saved = uiSettingsService.Load(repository.Paths);
    AssertTrue(!saved.PlaybackAileron, "Recording playback Ail toggle should save when changed.");
    AssertTrue(saved.PlaybackElevator, "Recording playback Ele toggle should keep its default enabled value.");
    AssertTrue(saved.PlaybackThrottle, "Recording playback Thr toggle should save when enabled.");
    AssertTrue(saved.PlaybackRudder, "Recording playback Rud toggle should save when enabled.");
    AssertTrue(!saved.PlaybackRadioRightGimbal, "Recording playback radio right-gimbal toggle should stay off when recorded trainer mode is selected.");
    AssertTrue(saved.PlaybackRecordedTrainerRight, "Recording playback recorded-trainer right-stick toggle should save when enabled.");
    AssertTrue(!saved.PlaybackRadioLeftGimbal, "Recording playback radio left-gimbal toggle should stay off when recorded trainer left mode is selected.");
    AssertTrue(saved.PlaybackRecordedTrainerLeft, "Recording playback recorded-trainer left-stick toggle should save when enabled.");
    AssertTrue(saved.PlaybackBlockLiveInput, "Recording playback live-input block toggle should save when enabled.");

    var restoredViewModel = new MainViewModel(
        repository,
        new NullProfileFolderPicker(),
        uiSettingsService,
        new NullTooltipImagePicker(),
        new Gx12RuntimeService());
    AssertTrue(!restoredViewModel.PlaybackAileron, "Saved Ail toggle should survive a launcher restart.");
    AssertTrue(restoredViewModel.PlaybackThrottle, "Saved Thr toggle should survive a launcher restart.");
    AssertTrue(restoredViewModel.PlaybackRudder, "Saved Rud toggle should survive a launcher restart.");
    AssertTrue(!restoredViewModel.PlaybackRadioRightGimbal, "Saved recorded trainer mode should keep radio right-gimbal mode off.");
    AssertTrue(restoredViewModel.PlaybackRecordedTrainerRight, "Saved recorded trainer right-stick toggle should survive a launcher restart.");
    AssertTrue(!restoredViewModel.PlaybackRadioLeftGimbal, "Saved recorded trainer left mode should keep radio left-gimbal mode off.");
    AssertTrue(restoredViewModel.PlaybackRecordedTrainerLeft, "Saved recorded trainer left-stick toggle should survive a launcher restart.");
    AssertTrue(restoredViewModel.PlaybackBlockLiveInput, "Saved live-input block toggle should survive a launcher restart.");
    AssertEqual("trainer_ele,trainer_thr,trainer_rud", restoredViewModel.PlaybackChannelMask, "Restored playback mask should use the saved Recording tab toggles.");
}

static void MainViewModelPicksRecordingAndPlaybackFiles()
{
    using var temp = CreateTempRepoWithProfiles();
    var profilePath = Path.Combine(temp.Path, "profiles", "whoop-linear.toml");
    File.WriteAllText(
        profilePath,
        "[trainer]\r\n" +
        "name = \"whoop-linear\"\r\n" +
        "frame_rate_hz = 1000\r\n");
    var repository = new ProfileRepository(
        new ProfileDirectoryService(temp.Path),
        new DelegateProfileValidator((_, _) => ProfileValidationResult.Success("ok")));
    var picker = new QueueRecordingFilePicker(
        new[] { "logs\\chosen-output.gx12rec.csv" },
        new[] { "logs\\chosen-playback.gx12rec.csv", "logs\\chosen-bind.gx12rec.csv" });
    var uiSettingsService = new UiSettingsService();
    var viewModel = new MainViewModel(
        repository,
        new NullProfileFolderPicker(),
        uiSettingsService,
        new NullTooltipImagePicker(),
        new Gx12RuntimeService(),
        recordingFilePicker: picker);

    AssertEqual(viewModel.RecordingPath, viewModel.PlaybackRecordingPath, "Playback should initially track the default recording path.");

    viewModel.ChooseRecordingPathCommand.Execute(null);
    AssertEqual("logs\\chosen-output.gx12rec.csv", viewModel.RecordingPath, "Recording picker should update the recording output path.");
    AssertEqual("logs\\chosen-output.gx12rec.csv", viewModel.PlaybackRecordingPath, "Playback should keep tracking recording until the playback path is edited.");
    AssertTrue(
        viewModel.TrainerCommandLine.Contains("logs\\chosen-output.gx12rec.csv", StringComparison.Ordinal),
        "Composite trainer command should use the selected recording output path.");
    AssertTrue(
        viewModel.RecordingCommandLine.Contains("logs\\chosen-output.gx12rec.csv", StringComparison.Ordinal),
        "Dedicated recording command should use the selected recording output path.");
    AssertTrue(
        viewModel.RecordingBufferStatusText.Contains("memory", StringComparison.OrdinalIgnoreCase) &&
        viewModel.RecordingBufferStatusText.Contains("chosen-output.gx12rec.csv", StringComparison.Ordinal),
        "Recordings UI should make memory-buffered CSV commit behavior visible.");
    AssertEqual(
        "logs\\chosen-output.gx12rec.csv",
        uiSettingsService.Load(repository.Paths).RecordingPath,
        "Selected recording output path should persist in UI settings.");
    viewModel.RecordingOverwrite = true;
    AssertTrue(uiSettingsService.Load(repository.Paths).RecordingOverwrite,
        "Recording overwrite mode should persist from the Recordings tab.");
    AssertTrue(viewModel.TrainerCommandLine.Contains("--record-overwrite", StringComparison.Ordinal),
        "Composite trainer command should include overwrite mode when enabled.");
    AssertTrue(
        File.ReadAllText(Gx12RuntimeService.GetRuntimeControlPath(repository.Paths))
            .Contains("record_overwrite\t1", StringComparison.Ordinal),
        "Runtime control file should publish overwrite mode for the active trainer run.");

    var restoredViewModel = new MainViewModel(
        repository,
        new NullProfileFolderPicker(),
        uiSettingsService,
        new NullTooltipImagePicker(),
        new Gx12RuntimeService(),
        recordingFilePicker: new QueueRecordingFilePicker(Array.Empty<string?>(), Array.Empty<string?>()));
    AssertEqual(
        "logs\\chosen-output.gx12rec.csv",
        restoredViewModel.RecordingPath,
        "Saved recording output path should survive a launcher restart.");
    AssertTrue(
        restoredViewModel.TrainerCommandLine.Contains("logs\\chosen-output.gx12rec.csv", StringComparison.Ordinal),
        "Restored composite trainer command should use the saved recording output path.");

    viewModel.ChoosePlaybackPathCommand.Execute(null);
    AssertEqual("logs\\chosen-output.gx12rec.csv", viewModel.RecordingPath, "Playback picker should not change the recording output path.");
    AssertEqual("logs\\chosen-playback.gx12rec.csv", viewModel.PlaybackRecordingPath, "Playback picker should update the single playback source.");
    viewModel.PlaybackBlockLiveInput = true;
    AssertTrue(
        viewModel.PlaybackCommandLine.Contains("logs\\chosen-playback.gx12rec.csv", StringComparison.Ordinal),
        "Single playback command should use the playback file path.");
    AssertTrue(
        viewModel.PlaybackCommandLine.Contains("--trainer-profile", StringComparison.Ordinal) &&
        viewModel.PlaybackCommandLine.Contains("--bind-block F5 trainer_ail,trainer_ele", StringComparison.Ordinal),
        "Single playback command should arm inline playback with live input blocking inside the normal trainer runtime.");
    AssertTrue(
        viewModel.TrainerCommandLine.Contains("--bind-block F5 trainer_ail,trainer_ele", StringComparison.Ordinal),
        "Composite trainer command should include the selected single playback bind.");

    viewModel.AddPlaybackBindingToggle = true;
    var binding = viewModel.PlaybackBindings.Single();
    AssertEqual("logs\\chosen-playback.gx12rec.csv", binding.RecordingPath, "New playback binds should start from the selected playback file.");
    AssertTrue(binding.BlockLiveInput, "New playback bind rows should inherit the single playback live-input block setting.");

    viewModel.ChoosePlaybackBindingPathCommand.Execute(binding);
    AssertEqual("logs\\chosen-bind.gx12rec.csv", binding.RecordingPath, "Bind picker should update only the selected bind row.");
}

static void ReleaseInfoExposesLauncherAndPublishRecipe()
{
    using var temp = CreateTempRepoWithProfiles();
    var exePath = Path.Combine(temp.Path, "runtime", "gx12mouse.exe");
    var releasePackageExePath = Path.Combine(
        temp.Path,
        "dist",
        ReleaseInfoService.BuildPackageDirectoryName(ResolveTestReleaseVersion()),
        ReleaseInfoService.ReleaseLauncherFileName);
    var publishExePath = Path.Combine(
        temp.Path,
        "apps",
        "Gx12.Launcher.Wpf",
        "bin",
        "Release",
        "net7.0-windows",
        "win-x64",
        "publish",
        ReleaseInfoService.PublishedAppHostFileName);
    var projectDirectory = Path.Combine(temp.Path, "apps", "Gx12.Launcher.Wpf");
    var appIconPath = Path.Combine(projectDirectory, "Assets", "gx12.ico");
    Directory.CreateDirectory(Path.GetDirectoryName(exePath)!);
    Directory.CreateDirectory(Path.GetDirectoryName(releasePackageExePath)!);
    Directory.CreateDirectory(Path.GetDirectoryName(publishExePath)!);
    Directory.CreateDirectory(Path.GetDirectoryName(appIconPath)!);
    Directory.CreateDirectory(Path.Combine(temp.Path, "tools"));
    File.WriteAllText(Path.Combine(temp.Path, "Start GX12 Launcher WPF.bat"), "test\r\n");
    File.WriteAllText(Path.Combine(temp.Path, "Start GX12 Launcher V3.bat"), "test\r\n");
    File.WriteAllText(Path.Combine(temp.Path, "tools", "publish-gx12-distribution.ps1"), "test\r\n");
    File.WriteAllText(
        Path.Combine(projectDirectory, "Gx12.Launcher.Wpf.csproj"),
        "<Project><PropertyGroup><ApplicationIcon>Assets\\gx12.ico</ApplicationIcon></PropertyGroup></Project>\r\n");
    File.WriteAllText(appIconPath, "test\r\n");
    File.WriteAllText(exePath, "test\r\n");
    File.WriteAllText(releasePackageExePath, "test\r\n");
    File.WriteAllText(publishExePath, "test\r\n");

    var paths = new AppPaths(
        temp.Path,
        Path.Combine(temp.Path, "profiles"),
        "whoop-linear.toml",
        exePath,
        Path.Combine(temp.Path, ".gx12-profile-dir"),
        Path.Combine(temp.Path, ".gx12-default-profile"));

    var info = ReleaseInfoService.Create(paths);

    AssertEqual("Asukaflight", info.ProductName, "Release metadata should name the WPF app.");
    AssertEqual("Asukaflight.exe", info.PrimaryLauncherName, "WPF should package an exe as the primary launcher.");
    AssertEqual(releasePackageExePath, info.WpfLauncherPath, "WPF launcher path should point to the GitHub release exe.");
    AssertTrue(info.RuntimeStatus.Contains("enabled", StringComparison.Ordinal),
        "Release info should expose that runtime controls are enabled.");
    AssertTrue(info.PublishCommand.Contains(@"tools\publish-gx12-distribution.ps1", StringComparison.Ordinal),
        "Publish recipe should target the firmware-included distribution packaging script.");
    AssertTrue(info.SettingsBehavior.Contains(".gx12-ui", StringComparison.Ordinal),
        "Release info should explain portable UI settings.");
    AssertTrue(info.ParityGateStatus.Contains("GitHub release users run Asukaflight.exe", StringComparison.Ordinal),
        "Release info should keep the V3 fallback explicit.");
    AssertTrue(typeof(ReleaseInfo).GetProperty("DpiPreviewCommand") is null,
        "Release info should not expose development-only DPI preview commands to the user surface.");
    AssertTrue(typeof(ReleaseInfo).GetProperty("DpiPreviewStatus") is null,
        "Release info should not expose development-only DPI preview status to the user surface.");
    AssertTrue(info.ReadinessChecks.Any(check =>
            check.Label.Equals("Release exe", StringComparison.Ordinal) &&
            check.Kind.Equals("Accent", StringComparison.Ordinal)),
        "Release checks should pass the present packaged WPF launcher.");
    AssertTrue(info.ReadinessChecks.Any(check =>
            check.Label.Equals("App icon", StringComparison.Ordinal) &&
            check.Kind.Equals("Accent", StringComparison.Ordinal)),
        "Release checks should pass the configured app icon.");
    AssertTrue(info.ReadinessSummary.Contains("Release checks clear", StringComparison.Ordinal),
        "Release summary should not warn about development-only DPI preview artifacts.");
    AssertEqual(
        "Asukaflight-0.9.0-p1-win-x64",
        ReleaseInfoService.BuildPackageDirectoryName("0.9.0-preview.1"),
        "Preview package names should use the short pN token.");
}

static void ProfileDirectoryServiceRecognizesReleasePackageRoots()
{
    using var temp = new TemporaryDirectory();
    Directory.CreateDirectory(Path.Combine(temp.Path, "profiles"));
    Directory.CreateDirectory(Path.Combine(temp.Path, "runtime"));
    File.WriteAllText(Path.Combine(temp.Path, "runtime", "gx12mouse.exe"), "test\r\n");
    File.WriteAllText(Path.Combine(temp.Path, ProfileDirectoryService.ReleaseMarkerFileName), "test\r\n");

    AssertTrue(ProfileDirectoryService.IsRecognizedRoot(temp.Path),
        "Release package roots should be recognized by their release marker.");

    using var legacy = new TemporaryDirectory();
    Directory.CreateDirectory(Path.Combine(legacy.Path, "profiles"));
    File.WriteAllText(Path.Combine(legacy.Path, ProfileDirectoryService.LegacyReleaseMarkerFileName), "test\r\n");

    AssertTrue(ProfileDirectoryService.IsRecognizedRoot(legacy.Path),
        "Legacy GX12 release package markers should remain recognized.");
}

static void ProfileDirectoryServiceRecognizesCleanSourceRoots()
{
    using var temp = new TemporaryDirectory();
    Directory.CreateDirectory(Path.Combine(temp.Path, "profiles"));
    Directory.CreateDirectory(Path.Combine(temp.Path, "apps", "Gx12.Launcher.Wpf"));
    File.WriteAllText(Path.Combine(temp.Path, "CMakeLists.txt"), "cmake_minimum_required(VERSION 3.24)\r\n");
    File.WriteAllText(
        Path.Combine(temp.Path, "apps", "Gx12.Launcher.Wpf", "Gx12.Launcher.Wpf.csproj"),
        "<Project Sdk=\"Microsoft.NET.Sdk\"></Project>\r\n");

    AssertTrue(ProfileDirectoryService.IsRecognizedRoot(temp.Path),
        "Clean source roots should not need a prebuilt runtime exe.");
}

static void ReleasePublishScriptOmitsPublicSymbols()
{
    var scriptPath = Path.Combine(FindRepoRoot(), "tools", "publish-gx12-release.ps1");
    var script = File.ReadAllText(scriptPath);

    AssertTrue(script.Contains("\"-p:DebugType=none\"", StringComparison.Ordinal),
        "Release publish should suppress PDB generation.");
    AssertTrue(script.Contains("\"-p:DebugSymbols=false\"", StringComparison.Ordinal),
        "Release publish should disable debug symbols.");
    AssertTrue(Regex.IsMatch(script, @"Get-ChildItem[^\r\n]+-Filter\s+""\*\.pdb""", RegexOptions.CultureInvariant),
        "Release publish should scrub any PDB files that still appear in the package.");
    AssertTrue(script.Contains("Remove-Item -LiteralPath $_.FullName -Force", StringComparison.Ordinal),
        "Release publish should remove package PDB files before zipping.");
    AssertTrue(script.Contains(".gx12-ui\\ui-sprites", StringComparison.Ordinal),
        "Release publish should carry fixed UI sprites into the portable package.");
    AssertTrue(script.Contains(".gx12-ui\\tooltip-sprites", StringComparison.Ordinal),
        "Release publish should carry random tooltip sprites into the portable package.");
    AssertTrue(Regex.IsMatch(script, @"Get-ChildItem[^\r\n]+uiSpritesDir[^\r\n]+-Filter\s+""\*\.png""", RegexOptions.CultureInvariant),
        "Release publish should package PNG UI sprites without copying unrelated files.");
    AssertTrue(Regex.IsMatch(script, @"Get-ChildItem[^\r\n]+tooltipSpritesDir[^\r\n]+-Filter\s+""\*\.png""", RegexOptions.CultureInvariant),
        "Release publish should package PNG tooltip sprites without copying unrelated files.");
    AssertTrue(script.Contains("Join-Path $repoRoot \"LICENSE\"", StringComparison.Ordinal),
        "Release publish should require the root GPLv2 license file.");
    AssertTrue(script.Contains("GNU General Public License, version 2 only", StringComparison.Ordinal),
        "Release publish should disclose the GPLv2-only license in packaged text files.");
    AssertTrue(script.Contains("wpf-launcher-self-test.txt", StringComparison.Ordinal),
        "Release publish should verify the packaged self-test log before scrubbing it.");
    AssertTrue(script.Contains("Remove-Item -LiteralPath $packageLogsDir -Recurse -Force", StringComparison.Ordinal),
        "Release publish should remove self-test logs before zipping public packages.");
}

static void DistributionPublishScriptPackagesFirmwareAndReleaseBundle()
{
    var scriptPath = Path.Combine(FindRepoRoot(), "tools", "publish-gx12-distribution.ps1");
    var script = File.ReadAllText(scriptPath);

    AssertTrue(script.Contains("firmware\\R2X-7D8.BIN", StringComparison.Ordinal),
        "Distribution publish should use the current short-name GX12 firmware by default.");
    AssertTrue(script.Contains("7D8FAE80FDC88E872832DEDAABB0DE3DCAC47125DC35386BE8FE5B9EE0FCE071", StringComparison.Ordinal),
        "Distribution publish should pin the current firmware hash.");
    AssertTrue(script.Contains("R2X-7D8.BIN", StringComparison.Ordinal),
        "Distribution publish should stage the tested radio firmware filename.");
    AssertTrue(!script.Contains("firmware\\FIRMWARE", StringComparison.Ordinal),
        "Distribution publish should not hide the GX12 firmware under a nested FIRMWARE folder.");
    AssertTrue(script.Contains("README-FLASH-AND-RUN.txt", StringComparison.Ordinal),
        "Distribution publish should add user-facing flash/run instructions.");
    AssertTrue(script.Contains("MANIFEST-SHA256.txt", StringComparison.Ordinal),
        "Distribution publish should generate a file hash manifest.");
    AssertTrue(script.Contains("publish-gx12-release.ps1", StringComparison.Ordinal),
        "Distribution publish should wrap the portable app release publisher.");
    AssertTrue(script.Contains("-SkipZip", StringComparison.Ordinal),
        "Distribution publish should not also create the portable app-only zip.");
    AssertTrue(script.Contains("$distributionRoot = $packageRoot", StringComparison.Ordinal),
        "Distribution publish should add firmware directly beside Asukaflight.exe instead of nesting the app folder.");
    AssertTrue(!script.Contains("Copy-Item -LiteralPath $packageRoot -Destination (Join-Path $distributionRoot $packageName)", StringComparison.Ordinal),
        "Distribution publish should not create a same-name app folder inside the zip root.");
    AssertTrue(script.Contains("Asukaflight-$safeVersion-$RuntimeIdentifier", StringComparison.Ordinal),
        "Distribution publish should use the short app package name as the final zip name.");
    AssertTrue(script.Contains("Compress-DirectoryContents -SourceDirectory $distributionRoot", StringComparison.Ordinal),
        "Distribution publish should zip the distribution contents, not the distribution folder itself.");
    AssertTrue(script.Contains("preview[.-]?([0-9]+)", StringComparison.Ordinal),
        "Distribution publish should shorten preview.N versions to pN in artifact names.");
    AssertTrue(Regex.IsMatch(script, @"Get-ChildItem[^\r\n]+-Filter\s+""\*\.pdb""", RegexOptions.CultureInvariant),
        "Distribution publish should reject public PDB files.");
    AssertTrue(script.Contains("- LICENSE - GNU General Public License, version 2 only", StringComparison.Ordinal),
        "Distribution publish should document the root GPLv2 license.");
    AssertTrue(script.Contains("firmware\\edgetx-gx12-2.11.0", StringComparison.Ordinal),
        "Distribution publish should point to the matching firmware source tree.");
    AssertTrue(script.Contains("Get-ReadmeUserDirections", StringComparison.Ordinal),
        "Distribution publish should generate flash/run directions from README.md.");
}

static string ResolveTestReleaseVersion()
{
    var assembly = typeof(ReleaseInfoService).Assembly;
    return assembly.GetCustomAttribute<AssemblyInformationalVersionAttribute>()?.InformationalVersion ??
           assembly.GetName().Version?.ToString() ??
           "0.0.0";
}

static void MainWindowGridPlacementsFitDeclaredTracks()
{
    foreach (var xamlPath in new[]
             {
                 Path.Combine(FindRepoRoot(), "apps", "Gx12.Launcher.Wpf", "MainWindow.xaml")
             })
    {
        var document = XDocument.Load(xamlPath, LoadOptions.SetLineInfo);

        foreach (var grid in document.Descendants().Where(element => element.Name.LocalName.Equals("Grid", StringComparison.Ordinal)))
        {
            AssertGridPlacementsFitDeclaredTracks(
                grid,
                "Grid.RowDefinitions",
                "RowDefinition",
                "Grid.Row",
                "Grid.RowSpan",
                "row");
            AssertGridPlacementsFitDeclaredTracks(
                grid,
                "Grid.ColumnDefinitions",
                "ColumnDefinition",
                "Grid.Column",
                "Grid.ColumnSpan",
                "column");
        }
    }
}

static void MainWindowOptionControlsResolveTooltips()
{
    var xamlPath = Path.Combine(FindRepoRoot(), "apps", "Gx12.Launcher.Wpf", "MainWindow.xaml");
    var document = XDocument.Load(xamlPath, LoadOptions.SetLineInfo);

    AssertTrue(
        document.Root?.Attributes().Any(attribute =>
            attribute.Name.LocalName.Equals("OptionToolTips.IsEnabled", StringComparison.Ordinal) &&
            attribute.Value.Equals("True", StringComparison.OrdinalIgnoreCase)) == true,
        "MainWindow should enable automatic option tooltips.");

    var optionElements = document
        .Descendants()
        .Where(IsTooltipOptionElement)
        .ToList();
    AssertTrue(optionElements.Count >= 70, "Expected to inspect the editable and command option controls.");

    var missing = new List<string>();
    foreach (var element in optionElements)
    {
        if (HasExplicitToolTip(element))
        {
            continue;
        }

        var bindingPath = GetTooltipOptionBindingPath(element);
        var resolved = element.Name.LocalName.Equals("Button", StringComparison.Ordinal)
            ? OptionTooltipCatalog.TryGetTooltipForCommandPath(bindingPath, out _)
            : OptionTooltipCatalog.TryGetTooltipForBindingPath(bindingPath, out _);

        if (!resolved)
        {
            missing.Add($"{DescribeElement(element)} binding '{bindingPath}'");
        }
    }

    AssertTrue(
        missing.Count == 0,
        $"Every option control should have an explicit or catalog-backed tooltip. Missing: {string.Join("; ", missing)}");
}

static void WorkspaceTabsKeepHoverScopedToHeadersAndAllowOverflow()
{
    var appXamlPath = Path.Combine(FindRepoRoot(), "apps", "Gx12.Launcher.Wpf", "App.xaml");
    var document = XDocument.Load(appXamlPath, LoadOptions.SetLineInfo);

    var tabPanelScrollViewer = document.Descendants().FirstOrDefault(element =>
        element.Name.LocalName.Equals("ScrollViewer", StringComparison.Ordinal) &&
        HasAttributeValue(element, "HorizontalScrollBarVisibility", "Auto") &&
        element.Descendants().Any(child =>
            child.Name.LocalName.Equals("TabPanel", StringComparison.Ordinal) &&
            IsTrueAttribute(child, "IsItemsHost")));
    AssertTrue(tabPanelScrollViewer is not null, "TabControl should wrap the tab header TabPanel in an auto horizontal ScrollViewer.");

    var tabItemStyle = document.Descendants().FirstOrDefault(element =>
        element.Name.LocalName.Equals("Style", StringComparison.Ordinal) &&
        element.Attributes().Any(attribute =>
            attribute.Name.LocalName.Equals("TargetType", StringComparison.Ordinal) &&
            attribute.Value.Contains("TabItem", StringComparison.Ordinal)));
    AssertTrue(tabItemStyle is not null, "App.xaml should define the shared TabItem style.");
    var tabItemStyleElement = tabItemStyle ?? throw new InvalidOperationException("App.xaml should define the shared TabItem style.");

    var directMouseOverTrigger = tabItemStyleElement.Descendants().Any(element =>
        element.Name.LocalName.Equals("Trigger", StringComparison.Ordinal) &&
        HasAttributeValue(element, "Property", "IsMouseOver"));
    AssertTrue(!directMouseOverTrigger, "TabItem hover should not bind to TabItem.IsMouseOver because selected tab content can keep it active.");

    var headerHoverTrigger = tabItemStyleElement.Descendants().Any(element =>
        element.Name.LocalName.Equals("MultiTrigger", StringComparison.Ordinal) &&
        element.Descendants().Any(condition =>
            condition.Name.LocalName.Equals("Condition", StringComparison.Ordinal) &&
            HasAttributeValue(condition, "SourceName", "TabChrome") &&
            HasAttributeValue(condition, "Property", "IsMouseOver")) &&
        element.Descendants().Any(condition =>
            condition.Name.LocalName.Equals("Condition", StringComparison.Ordinal) &&
            HasAttributeValue(condition, "Property", "IsSelected") &&
            HasAttributeValue(condition, "Value", "False")));
    AssertTrue(headerHoverTrigger, "TabItem hover should target TabChrome.IsMouseOver only for unselected tabs.");
}

static void WorkspaceTabsRenderOptionalAboveBarSprite()
{
    var appXamlPath = Path.Combine(FindRepoRoot(), "apps", "Gx12.Launcher.Wpf", "App.xaml");
    var document = XDocument.Load(appXamlPath, LoadOptions.SetLineInfo);

    var spriteImage = document.Descendants().FirstOrDefault(element =>
        element.Name.LocalName.Equals("Image", StringComparison.Ordinal) &&
        GetBindingPath(element, "Source").Equals("AboveBarSpritePath", StringComparison.Ordinal));
    AssertTrue(spriteImage is not null, "TabControl should render the optional above-bar sprite image.");

    var image = spriteImage ?? throw new InvalidOperationException("TabControl should render the optional above-bar sprite image.");
    AssertTrue(
        GetBindingPath(image, "Visibility").Equals("HasAboveBarSprite", StringComparison.Ordinal),
        "The above-bar sprite should collapse when above.png is missing.");
    AssertTrue(HasAttributeValue(image, "IsHitTestVisible", "True"), "The above-bar sprite should accept clicks for its hide animation.");
    AssertTrue(HasAttributeValue(image, "Cursor", "Hand"), "The clickable above-bar sprite should advertise that it is interactive.");
    AssertTrue(HasAttributeValue(image, "Panel.ZIndex", "5"), "The above-bar sprite should render above the transparent tab-strip hit-test surface.");
    AssertTrue(HasAttributeValue(image, "RenderOptions.BitmapScalingMode", "NearestNeighbor"), "The pixel-art sprite should keep sharp edges.");
    AssertTrue(HasAttributeValue(image, "AboveBarSpriteBehavior.IsEnabled", "True"), "The above-bar sprite should use its click-hide behavior.");
    AssertTrue(HasAttributeValue(image, "AboveBarSpriteBehavior.MinReturnDelaySeconds", "10"), "The sprite return delay should be at least 10 seconds.");
    AssertTrue(HasAttributeValue(image, "AboveBarSpriteBehavior.MaxReturnDelaySeconds", "600"), "The sprite return delay should be at most 600 seconds.");
    AssertTrue(
        GetBindingPath(image, "AboveBarSpriteBehavior.UseRandomReturnDelay").Equals("Editor.AboveBarSpriteRandomReturnDelay", StringComparison.Ordinal),
        "The sprite return behavior should bind to the Files tab random/fixed setting.");
    AssertTrue(
        GetBindingPath(image, "AboveBarSpriteBehavior.FixedReturnDelaySeconds").Equals("Editor.AboveBarSpriteFixedReturnDelaySeconds", StringComparison.Ordinal),
        "The sprite return behavior should bind to the Files tab fixed seconds setting.");

    var translateTransform = image.Elements()
        .FirstOrDefault(element => element.Name.LocalName.Equals("Image.RenderTransform", StringComparison.Ordinal))
        ?.Elements()
        .FirstOrDefault(child => child.Name.LocalName.Equals("TranslateTransform", StringComparison.Ordinal));
    var hasTranslateTransform = translateTransform is not null;
    AssertTrue(
        translateTransform is not null && HasAttributeValue(translateTransform, "X", "40"),
        "The above-bar sprite should sit 40 pixels farther right so it clears the tab strip.");
    AssertTrue(hasTranslateTransform, "The above-bar sprite should have a translate transform for the hide/return animation.");

    var behaviorPath = Path.Combine(FindRepoRoot(), "apps", "Gx12.Launcher.Wpf", "Controls", "AboveBarSpriteBehavior.cs");
    var behavior = File.ReadAllText(behaviorPath);
    AssertTrue(behavior.Contains("Panel.SetZIndex(image, HiddenZIndex)", StringComparison.Ordinal),
        "The sprite behavior should move the sprite below the content panel when clicked.");
    AssertTrue(!behavior.Contains("async void OnMouseLeftButtonDown", StringComparison.Ordinal),
        "The sprite click handler should stay synchronous so animation faults cannot crash the process through async void.");
    AssertTrue(behavior.Contains("DispatcherTimer", StringComparison.Ordinal),
        "The sprite behavior should use a WPF dispatcher timer for the delayed return.");
    AssertTrue(behavior.Contains("Random.Shared.Next(min, max + 1)", StringComparison.Ordinal),
        "The sprite behavior should choose a fresh random return delay inside the configured range.");
    AssertTrue(behavior.Contains("X = currentX", StringComparison.Ordinal),
        "The sprite behavior should preserve the template X offset when replacing frozen transforms.");
    AssertTrue(behavior.Contains("GetUseRandomReturnDelay(image)", StringComparison.Ordinal),
        "The sprite behavior should allow random return delay to be disabled.");
    AssertTrue(behavior.Contains("GetFixedReturnDelaySeconds(image)", StringComparison.Ordinal),
        "The sprite behavior should use the configured fixed return delay when random return is disabled.");
    AssertTrue(behavior.Contains("Interval = TimeSpan.FromSeconds(delaySeconds)", StringComparison.Ordinal),
        "The sprite behavior should use the randomized delay before creeping back up.");
    AssertTrue(behavior.Contains("Panel.SetZIndex(image, RestingZIndex)", StringComparison.Ordinal),
        "The sprite behavior should restore the top layer after returning to the original position.");
}

static void AboveBarSpriteClickHandlerDoesNotThrow()
{
    Exception? failure = null;
    var thread = new Thread(() =>
    {
        try
        {
            var frozenTransform = new TranslateTransform
            {
                X = 40
            };
            frozenTransform.Freeze();
            var image = new Image
            {
                RenderTransform = frozenTransform
            };
            Panel.SetZIndex(image, 5);
            AboveBarSpriteBehavior.SetMinReturnDelaySeconds(image, 10);
            AboveBarSpriteBehavior.SetMaxReturnDelaySeconds(image, 600);
            AboveBarSpriteBehavior.SetIsEnabled(image, true);

            var args = new MouseButtonEventArgs(Mouse.PrimaryDevice, Environment.TickCount, MouseButton.Left)
            {
                RoutedEvent = UIElement.MouseLeftButtonDownEvent,
                Source = image
            };
            image.RaiseEvent(args);

            AssertTrue(args.Handled, "The sprite click should be handled by the hide behavior.");
            AssertEqual(1, Panel.GetZIndex(image), "Clicking the sprite should immediately move it below the content panel.");
            AssertTrue(
                image.RenderTransform is TranslateTransform { IsFrozen: false },
                "The click handler should replace frozen template transforms before animating.");
            AssertNear(40, ((TranslateTransform)image.RenderTransform).X, "The click handler should preserve the sprite's template X offset.");
            AboveBarSpriteBehavior.SetIsEnabled(image, false);
        }
        catch (Exception exception)
        {
            failure = exception;
        }
    });
    thread.SetApartmentState(ApartmentState.STA);
    thread.Start();
    thread.Join();

    if (failure is not null)
    {
        throw new InvalidOperationException("The above-bar sprite click handler threw on the WPF UI thread.", failure);
    }
}

static void MainWindowAboveBarSpriteClickDoesNotThrow()
{
    Exception? failure = null;
    var thread = new Thread(() =>
    {
        MainWindow? window = null;
        try
        {
            if (Application.Current is null)
            {
                var app = new App();
                app.InitializeComponent();
            }

            window = new MainWindow(CompositionRoot.CreateMainViewModel())
            {
                Width = 1440,
                Height = 850,
                Left = -32000,
                Top = -32000,
                WindowStartupLocation = WindowStartupLocation.Manual,
                WindowStyle = WindowStyle.None,
                ShowInTaskbar = false
            };

            window.Show();
            window.UpdateLayout();
            window.Dispatcher.Invoke(() => { }, System.Windows.Threading.DispatcherPriority.Render);

            var sprite = FindVisualChild<Image>(window, image => image.Name.Equals("AboveBarSprite", StringComparison.Ordinal));
            AssertTrue(sprite is not null, "The real MainWindow visual tree should contain the above-bar sprite image.");

            var args = new MouseButtonEventArgs(Mouse.PrimaryDevice, Environment.TickCount, MouseButton.Left)
            {
                RoutedEvent = UIElement.MouseLeftButtonDownEvent,
                Source = sprite
            };
            sprite!.RaiseEvent(args);

            AssertTrue(args.Handled, "The real above-bar sprite click should be handled.");
            AssertEqual(1, Panel.GetZIndex(sprite), "The real above-bar sprite click should move the sprite below the content panel.");
        }
        catch (Exception exception)
        {
            failure = exception;
        }
        finally
        {
            window?.Close();
        }
    });
    thread.SetApartmentState(ApartmentState.STA);
    thread.Start();
    thread.Join();

    if (failure is not null)
    {
        throw new InvalidOperationException("The real MainWindow above-bar sprite click threw on the WPF UI thread.", failure);
    }
}

static void MainWindowHidesReleaseReadinessUserSurface()
{
    var xamlPath = Path.Combine(FindRepoRoot(), "apps", "Gx12.Launcher.Wpf", "MainWindow.xaml");
    var xaml = File.ReadAllText(xamlPath);

    AssertTrue(!xaml.Contains("Release Readiness", StringComparison.Ordinal), "Release readiness should not be visible in the user window.");
    AssertTrue(!xaml.Contains("Header=\"Files + Release\"", StringComparison.Ordinal), "The files tab should not advertise release tooling.");
    AssertTrue(!xaml.Contains("DPI preview", StringComparison.OrdinalIgnoreCase), "DPI preview tooling should not be visible in the user window.");
}

static void HotkeyCaptureFormatsKeysAndMouseButtons()
{
    AssertEqual("F5", HotkeyCapture.FormatKey(Key.F5), "Function keys should format as native trigger names.");
    AssertEqual("0", HotkeyCapture.FormatKey(Key.D0), "Top-row digits should format as native trigger names.");
    AssertEqual("Space", HotkeyCapture.FormatKey(Key.Space), "Special keys should format as native trigger names.");
    AssertEqual("Mouse4", HotkeyCapture.FormatMouseButton(MouseButton.XButton1), "Mouse side button 1 should format as Mouse4.");
    AssertEqual("Mouse5", HotkeyCapture.FormatMouseButton(MouseButton.XButton2), "Mouse side button 2 should format as Mouse5.");
    AssertTrue(HotkeyCapture.TryParseKeyboardVirtualKey("F1", out var f1) && f1 == 0x70,
        "Launcher start/stop hotkey registration should parse the selected profile key.");
    AssertTrue(!HotkeyCapture.TryParseKeyboardVirtualKey("Mouse4", out _),
        "Launcher start/stop hotkey registration should stay keyboard-only.");
}

static void MainWindowFilesTabExposesAboveBarSpriteReturnControls()
{
    var xamlPath = Path.Combine(FindRepoRoot(), "apps", "Gx12.Launcher.Wpf", "MainWindow.xaml");
    var document = XDocument.Load(xamlPath, LoadOptions.SetLineInfo);

    var randomToggle = document.Descendants().FirstOrDefault(element =>
        element.Name.LocalName.Equals("CheckBox", StringComparison.Ordinal) &&
        GetBindingPath(element, "IsChecked").Equals("Editor.AboveBarSpriteRandomReturnDelay", StringComparison.Ordinal));
    AssertTrue(randomToggle is not null, "The Files tab should expose a random/fixed panel-sprite return toggle.");

    var fixedDelayTextBox = document.Descendants().FirstOrDefault(element =>
        element.Name.LocalName.Equals("TextBox", StringComparison.Ordinal) &&
        GetBindingPath(element, "Text").Equals("Editor.AboveBarSpriteFixedReturnDelaySecondsText", StringComparison.Ordinal));
    AssertTrue(fixedDelayTextBox is not null, "The Files tab should expose the fixed panel-sprite return seconds editor.");
    AssertTrue(
        GetBindingPath(fixedDelayTextBox!, "IsEnabled").Equals("Editor.IsAboveBarSpriteFixedReturnDelayEnabled", StringComparison.Ordinal),
        "The fixed return editor should only be enabled when random return is off.");

    AssertTrue(
        document.Descendants().Any(element =>
            element.Name.LocalName.Equals("TextBlock", StringComparison.Ordinal) &&
            GetBindingPath(element, "Text").Equals("Editor.AboveBarSpriteReturnSummary", StringComparison.Ordinal)),
        "The Files tab should summarize the active panel-sprite return mode.");
}

static void MainWindowExposesRecordingControls()
{
    var xamlPath = Path.Combine(FindRepoRoot(), "apps", "Gx12.Launcher.Wpf", "MainWindow.xaml");
    var document = XDocument.Load(xamlPath, LoadOptions.SetLineInfo);

    AssertTrue(
        document.Descendants().Any(element =>
            element.Name.LocalName.Equals("TabItem", StringComparison.Ordinal) &&
            HasAttributeValue(element, "Header", "Recordings")),
        "MainWindow should expose a Recordings tab.");
    AssertTrue(HasHotkeyCapture(document, "Editor.StopKey"),
        "Profile start/stop key should use click-to-capture editing.");
    AssertTrue(HasHotkeyCapture(document, "Editor.FreezeKey"),
        "Profile freeze key should use click-to-capture editing.");
    AssertTrue(HasBoundElement(document, "TextBox", "Text", "RecordingPath"),
        "Recordings tab should expose the recording file path.");
    AssertTrue(HasBoundElement(document, "Button", "Command", "ChooseRecordingPathCommand"),
        "Recordings tab should expose a picker for the recording output path.");
    AssertTrue(HasBoundElement(document, "TextBox", "Text", "RecordingDurationSeconds"),
        "Recordings tab should expose the timed capture duration.");
    AssertTrue(HasBoundElement(document, "CheckBox", "IsChecked", "RecordingLiveReload"),
        "Recordings tab should expose the live reload toggle.");
    AssertTrue(HasBoundElement(document, "TextBox", "Text", "RecordingToggleKey"),
        "Recordings tab should expose the recording toggle key.");
    AssertTrue(HasHotkeyCapture(document, "RecordingToggleKey"),
        "Recording toggle key should capture key/mouse presses instead of requiring typed names.");
    AssertTrue(HasBoundElement(document, "CheckBox", "IsChecked", "RecordingOverwrite"),
        "Recordings tab should expose overwrite mode for the selected recording file.");
    AssertTrue(HasBoundElement(document, "TextBlock", "Text", "RecordingBufferStatusText"),
        "Recordings tab should expose the memory-buffered recording commit status.");
    AssertTrue(!HasBoundElement(document, "Button", "Command", "StartTrainerRecordingCommand"),
        "Recordings tab should not require a separate arm-recording button.");
    AssertTrue(HasBoundElement(document, "Button", "Command", "InspectRecordingCommand"),
        "Recordings tab should expose the recording-info command.");

    foreach (var bindingPath in new[] { "PlaybackAileron", "PlaybackElevator", "PlaybackThrottle", "PlaybackRudder", "PlaybackRadioRightGimbal", "PlaybackRecordedTrainerRight", "PlaybackRadioLeftGimbal", "PlaybackRecordedTrainerLeft" })
    {
        AssertTrue(HasBoundElement(document, "CheckBox", "IsChecked", bindingPath),
            $"Recordings tab should expose the {bindingPath} playback channel toggle.");
    }

    AssertTrue(HasBoundElement(document, "TextBox", "Text", "PlaybackPort"),
        "Recordings tab should expose the playback port.");
    AssertTrue(HasBoundElement(document, "TextBox", "Text", "PlaybackRecordingPath"),
        "Recordings tab should expose the single playback file path.");
    AssertTrue(HasBoundElement(document, "Button", "Command", "ChoosePlaybackPathCommand"),
        "Recordings tab should expose a picker for the single playback file.");
    AssertTrue(HasBoundElement(document, "TextBox", "Text", "PlaybackTrigger"),
        "Recordings tab should expose the playback trigger.");
    AssertTrue(HasHotkeyCapture(document, "PlaybackTrigger"),
        "Playback trigger should capture key/mouse presses instead of requiring typed names.");
    AssertTrue(HasBoundElement(document, "CheckBox", "IsChecked", "PlaybackLoop"),
        "Recordings tab should expose the playback loop toggle.");
    AssertTrue(HasBoundElement(document, "CheckBox", "IsChecked", "PlaybackBlockLiveInput"),
        "Recordings tab should expose the single playback live-input block toggle.");
    AssertTrue(!HasBoundElement(document, "Button", "Command", "StartTrainerPlaybackCommand"),
        "Recordings tab should not require arming inline playback.");
    AssertTrue(HasBoundElement(document, "CheckBox", "IsChecked", "AddPlaybackBindingToggle"),
        "Recordings tab should expose the add playback bind toggle.");
    AssertTrue(HasBoundElement(document, "ItemsControl", "ItemsSource", "PlaybackBindings"),
        "Recordings tab should expose the playback bind list.");
    AssertTrue(HasHotkeyCapture(document, "Trigger"),
        "Playback bind rows should capture key/mouse presses instead of requiring typed names.");
    AssertTrue(HasBoundElement(document, "CheckBox", "IsChecked", "BlockLiveInput"),
        "Playback bind rows should expose live-input block toggles.");
    AssertTrue(HasBoundElement(document, "Button", "Command", "DataContext.ChoosePlaybackBindingPathCommand"),
        "Recordings tab should expose a picker for playback bind files.");
    AssertTrue(!HasBoundElement(document, "Button", "Command", "StartTrainerPlaybackBankCommand"),
        "Recordings tab should not require a separate start-bind-bank button.");
    AssertTrue(HasBoundElement(document, "TextBox", "Text", "PlaybackBankCommandLine"),
        "Recordings tab should expose the playback bank command line.");
    AssertTrue(HasBoundElement(document, "TextBox", "Text", "RecordingInfoText"),
        "Recordings tab should expose recording-info output.");
}

static bool IsTooltipOptionElement(XElement element)
{
    var localName = element.Name.LocalName;
    return localName switch
    {
        "TextBox" => !IsTrueAttribute(element, "IsReadOnly") &&
                     !string.IsNullOrWhiteSpace(GetBindingPath(element, "Text")),
        "ComboBox" => !string.IsNullOrWhiteSpace(GetBindingPath(element, "SelectedValue")),
        "CheckBox" => !string.IsNullOrWhiteSpace(GetBindingPath(element, "IsChecked")),
        "StickShapeEditor" => !string.IsNullOrWhiteSpace(GetBindingPath(element, "NodesText")),
        "Button" => !string.IsNullOrWhiteSpace(GetBindingPath(element, "Command")),
        _ => false
    };
}

static string GetTooltipOptionBindingPath(XElement element)
{
    return element.Name.LocalName switch
    {
        "TextBox" => GetBindingPath(element, "Text"),
        "ComboBox" => GetBindingPath(element, "SelectedValue"),
        "CheckBox" => GetBindingPath(element, "IsChecked"),
        "StickShapeEditor" => GetBindingPath(element, "NodesText"),
        "Button" => GetBindingPath(element, "Command"),
        _ => ""
    };
}

static bool HasBoundElement(XDocument document, string localName, string attributeName, string bindingPath)
{
    return document
        .Descendants()
        .Any(element =>
            element.Name.LocalName.Equals(localName, StringComparison.Ordinal) &&
            GetBindingPath(element, attributeName).Equals(bindingPath, StringComparison.Ordinal));
}

static bool HasHotkeyCapture(XDocument document, string bindingPath)
{
    return document
        .Descendants()
        .Any(element =>
            element.Name.LocalName.Equals("TextBox", StringComparison.Ordinal) &&
            GetBindingPath(element, "Text").Equals(bindingPath, StringComparison.Ordinal) &&
            HasAttributeValue(element, "HotkeyCapture.IsEnabled", "True"));
}

static string GetBindingPath(XElement element, string attributeName)
{
    var value = element
        .Attributes()
        .FirstOrDefault(attribute => attribute.Name.LocalName.Equals(attributeName, StringComparison.Ordinal))
        ?.Value;
    if (string.IsNullOrWhiteSpace(value))
    {
        return "";
    }

    var match = Regex.Match(value, @"^\{Binding\s+(?<path>[^,\}\s]+)", RegexOptions.CultureInvariant);
    return match.Success ? match.Groups["path"].Value : "";
}

static bool HasExplicitToolTip(XElement element)
{
    return element.Attributes().Any(attribute => attribute.Name.LocalName.Equals("ToolTip", StringComparison.Ordinal)) ||
           element.Elements().Any(child => child.Name.LocalName.EndsWith(".ToolTip", StringComparison.Ordinal));
}

static bool IsTrueAttribute(XElement element, string attributeName)
{
    return element
        .Attributes()
        .Any(attribute =>
            attribute.Name.LocalName.Equals(attributeName, StringComparison.Ordinal) &&
            attribute.Value.Equals("True", StringComparison.OrdinalIgnoreCase));
}

static bool HasAttributeValue(XElement element, string attributeName, string expectedValue)
{
    return element
        .Attributes()
        .Any(attribute =>
            attribute.Name.LocalName.Equals(attributeName, StringComparison.Ordinal) &&
            attribute.Value.Equals(expectedValue, StringComparison.Ordinal));
}

static void AssertGridPlacementsFitDeclaredTracks(
    XElement grid,
    string definitionsName,
    string definitionName,
    string attachedIndexName,
    string attachedSpanName,
    string axis)
{
    var definitions = grid.Elements().FirstOrDefault(element =>
        element.Name.LocalName.Equals(definitionsName, StringComparison.Ordinal));
    if (definitions is null)
    {
        return;
    }

    var trackCount = definitions.Elements().Count(element =>
        element.Name.LocalName.Equals(definitionName, StringComparison.Ordinal));
    if (trackCount == 0)
    {
        return;
    }

    foreach (var child in grid.Elements().Where(element => !element.Name.LocalName.EndsWith("Definitions", StringComparison.Ordinal)))
    {
        var index = GetAttachedInt(child, attachedIndexName, 0);
        var span = Math.Max(1, GetAttachedInt(child, attachedSpanName, 1));
        AssertTrue(index >= 0, $"{DescribeElement(child)} uses a negative Grid.{axis} value.");
        AssertTrue(
            index + span <= trackCount,
            $"{DescribeElement(child)} uses Grid.{axis} {index} span {span}, but the parent declares {trackCount} {axis}(s).");
    }
}

static int GetAttachedInt(XElement element, string localName, int fallback)
{
    var attribute = element.Attributes().FirstOrDefault(candidate =>
        candidate.Name.LocalName.Equals(localName, StringComparison.Ordinal));
    return attribute is not null && int.TryParse(attribute.Value, out var value)
        ? value
        : fallback;
}

static string DescribeElement(XElement element)
{
    if (element is IXmlLineInfo lineInfo && lineInfo.HasLineInfo())
    {
        return $"{element.Name.LocalName} at line {lineInfo.LineNumber}";
    }

    return element.Name.LocalName;
}

static DeviceAssignmentStatus FindStatus(GameInputDeviceAssignmentReport report, string role)
{
    return report.Statuses.Single(status => status.Role.Equals(role, StringComparison.OrdinalIgnoreCase));
}

static string SampleGameInputDeviceOutput()
{
    return "\r\n" +
        "--mouse-devices-gameinput: move one mouse at a time for 2 second(s).\r\n" +
        "Use the root token as mouse_devices.right / mouse_devices.left in a profile. Esc stops early.\r\n" +
        "\r\n" +
        "[0.250s] devices=2\r\n" +
        "  [0] root=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa vid=0x046d pid=0xc539 cb=   14 dx=    +60 dy=    -10 buttons=0x00000000 name='Right Mouse'\r\n" +
        "  [1] root=bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb vid=0x1532 pid=0x0099 cb=   12 dx=    -20 dy=    +40 buttons=0x00000000 name='Left Mouse'\r\n" +
        "\r\n" +
        "summary devices=2\r\n" +
        "  [0] root=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa device=11111111111111111111111111111111 vid=0x046d pid=0xc539 total_dx=+120 total_dy=-30 name='Right Mouse'\r\n" +
        @"      pnp=HID\VID_046D&PID_C539&MI_00" + "\r\n" +
        "  [1] root=bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb device=22222222222222222222222222222222 vid=0x1532 pid=0x0099 total_dx=-50 total_dy=+90 name='Left Mouse'\r\n";
}

static TemporaryDirectory CreateTempRepoWithProfiles()
{
    var temp = new TemporaryDirectory();
    Directory.CreateDirectory(Path.Combine(temp.Path, "profiles"));
    return temp;
}

static AppPaths CreateAppPaths(string repoRoot)
{
    return new AppPaths(
        repoRoot,
        Path.Combine(repoRoot, "profiles"),
        "profile.toml",
        Path.Combine(repoRoot, "runtime", "gx12mouse.exe"),
        Path.Combine(repoRoot, ".gx12-profile-dir"),
        Path.Combine(repoRoot, ".gx12-default-profile"));
}

static T? FindVisualChild<T>(DependencyObject root, Func<T, bool> predicate)
    where T : DependencyObject
{
    var count = VisualTreeHelper.GetChildrenCount(root);
    for (var index = 0; index < count; index++)
    {
        var child = VisualTreeHelper.GetChild(root, index);
        if (child is T candidate && predicate(candidate))
        {
            return candidate;
        }

        var match = FindVisualChild(child, predicate);
        if (match is not null)
        {
            return match;
        }
    }

    return null;
}

static string FindRepoRoot()
{
    foreach (var start in new[] { Directory.GetCurrentDirectory(), AppContext.BaseDirectory })
    {
        var directory = new DirectoryInfo(start);
        while (directory is not null)
        {
            if (ProfileDirectoryService.IsRecognizedRoot(directory.FullName))
            {
                return directory.FullName;
            }

            directory = directory.Parent;
        }
    }

    throw new DirectoryNotFoundException("Could not find repo root.");
}

static int CountChangedLines(string left, string right)
{
    var leftLines = left.Replace("\r\n", "\n", StringComparison.Ordinal).Split('\n');
    var rightLines = right.Replace("\r\n", "\n", StringComparison.Ordinal).Split('\n');
    var count = Math.Max(leftLines.Length, rightLines.Length);
    var changed = 0;
    for (var index = 0; index < count; index++)
    {
        var leftLine = index < leftLines.Length ? leftLines[index] : "";
        var rightLine = index < rightLines.Length ? rightLines[index] : "";
        if (!leftLine.Equals(rightLine, StringComparison.Ordinal))
        {
            changed++;
        }
    }

    return changed;
}

static void AssertTrue(bool condition, string message)
{
    if (!condition)
    {
        throw new InvalidOperationException(message);
    }
}

static void AssertEqual<T>(T expected, T actual, string message)
{
    if (!EqualityComparer<T>.Default.Equals(expected, actual))
    {
        throw new InvalidOperationException($"{message}\nExpected: {expected}\nActual:   {actual}");
    }
}

static void AssertNear(double expected, double actual, string message)
{
    if (Math.Abs(expected - actual) > 0.000001)
    {
        throw new InvalidOperationException($"{message}\nExpected: {expected}\nActual:   {actual}");
    }
}

static void AssertSequenceEqual(byte[] expected, byte[] actual, string label)
{
    if (!expected.SequenceEqual(actual))
    {
        throw new InvalidOperationException($"{label} changed during round-trip save.");
    }
}

sealed class DelegateProfileValidator : IProfileValidator
{
    private readonly Func<AppPaths, string, ProfileValidationResult> _validate;

    public DelegateProfileValidator(Func<AppPaths, string, ProfileValidationResult> validate)
    {
        _validate = validate;
    }

    public int CallCount { get; private set; }

    public ProfileValidationResult Validate(AppPaths paths, string profilePath)
    {
        CallCount++;
        return _validate(paths, profilePath);
    }
}

sealed class NullProfileFolderPicker : IProfileFolderPicker
{
    public string? PickFolder(string currentDirectory, string repoRoot)
    {
        return null;
    }
}

sealed class NullTooltipImagePicker : ITooltipImagePicker
{
    public string? PickTooltipImage(string initialDirectory)
    {
        return null;
    }
}

sealed class QueueRecordingFilePicker : IRecordingFilePicker
{
    private readonly Queue<string?> _recordingOutputs;
    private readonly Queue<string?> _playbackRecordings;

    public QueueRecordingFilePicker(IEnumerable<string?> recordingOutputs, IEnumerable<string?> playbackRecordings)
    {
        _recordingOutputs = new Queue<string?>(recordingOutputs);
        _playbackRecordings = new Queue<string?>(playbackRecordings);
    }

    public string? PickRecordingOutput(string currentPath, string repoRoot)
    {
        return _recordingOutputs.Count == 0 ? null : _recordingOutputs.Dequeue();
    }

    public string? PickPlaybackRecording(string currentPath, string repoRoot)
    {
        return _playbackRecordings.Count == 0 ? null : _playbackRecordings.Dequeue();
    }
}

sealed class TemporaryDirectory : IDisposable
{
    public TemporaryDirectory()
    {
        Path = System.IO.Path.Combine(
            System.IO.Path.GetTempPath(),
            $"gx12-wpf-tests-{Guid.NewGuid():N}");
        Directory.CreateDirectory(Path);
    }

    public string Path { get; }

    public void Dispose()
    {
        try
        {
            Directory.Delete(Path, recursive: true);
        }
        catch
        {
        }
    }
}
