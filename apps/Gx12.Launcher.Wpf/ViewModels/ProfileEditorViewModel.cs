using System;
using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Data;
using System.Windows.Input;
using Gx12.Launcher.Wpf.Models;
using Gx12.Launcher.Wpf.Services;

namespace Gx12.Launcher.Wpf.ViewModels;

public sealed class ProfileEditorViewModel : ObservableObject
{
    public const string LeftSourceOff = "Off";
    public const string LeftSourceKeyboard = "Keyboard / Wooting";
    public const string LeftSourceMouse = "Second mouse";

    private readonly ProfileRepository _repository;
    private readonly Gx12DiagnosticsService _diagnosticsService;
    private readonly Gx12DiagnosticLogStore _diagnosticLogStore;
    private readonly UiSettingsService _uiSettingsService;
    private readonly ITooltipImagePicker _tooltipImagePicker;
    private CancellationTokenSource? _diagnosticCancellation;
    private UiSettings _uiSettings = new();
    private bool _loading;
    private ProfileSummary _profile;
    private string _profileName = "";
    private string _frameRateHz = "";
    private string _resolutionMode = "legacy";
    private string _controlMode = "direct_mouse";
    private string _stopKey = "";
    private string _freezeKey = "";
    private string _rollGain = "";
    private string _pitchGain = "";
    private string _maxOutput = "";
    private string _deadband = "";
    private string _expo = "";
    private string _outputCurve = "expo";
    private string _actualCenter = "";
    private string _actualMax = "";
    private string _actualExpo = "";
    private bool _returnEnabled;
    private string _returnRate = "";
    private string _returnIdle = "";
    private bool _constantReturnEnabled;
    private string _constantReturnRate = "";
    private bool _elasticReturnEnabled;
    private string _elasticReturnMode = "progressive";
    private string _elasticReturnCoefficient = "";
    private string _elasticReturnCurve = "";
    private bool _returnShapingEnabled;
    private string _outputShapeNodesText = "[]";
    private string _returnShapeNodesText = "[]";
    private string _inputFilter = "off";
    private string _smoothing = "";
    private string _oneEuroMinCutoffHz = "";
    private string _oneEuroBeta = "";
    private string _oneEuroDcutoffHz = "";
    private bool _despikeEnabled;
    private bool _despikeCountEnabled;
    private string _despikeWindow = "";
    private string _despikeThresholdSigma = "";
    private string _positionModel = "integrator";
    private string _gimbalFrequencyHz = "";
    private string _gimbalDampingRatio = "";
    private string _gimbalInputImpulse = "";
    private string _gimbalStaticFriction = "";
    private string _gimbalDynamicFriction = "";
    private string _gimbalEdgeBumper = "";
    private bool _gimbalAntiwindupEnabled;
    private string _gimbalAntiwindupStart = "";
    private string _gimbalAntiwindupMinGain = "";
    private string _inputGainMode = "flat";
    private string _adaptiveSlowGain = "";
    private string _adaptiveFastGain = "";
    private string _adaptiveSpeedLow = "";
    private string _adaptiveSpeedHigh = "";
    private string _adaptiveCurve = "";
    private string _adaptiveTrackerMs = "";
    private string _gateShape = "axis";
    private string _diagonalScale = "";
    private bool _mouseRightEnabled;
    private bool _invertRoll;
    private bool _invertPitch;
    private bool _swapAxes;
    private string _leftStickSource = LeftSourceOff;
    private string _keyboardInputSource = "gameinput";
    private bool _keyboardRequireAnalog;
    private bool _mouseLeftRequireDevice;
    private string _mouseLeftThrottleRate = "";
    private string _mouseLeftYawGain = "";
    private string _mouseLeftYawPulse = "";
    private string _mouseLeftYawDeadband = "";
    private bool _mouseLeftInvertThrottle;
    private bool _mouseLeftInvertYaw;
    private bool _mouseLeftSwapAxes;
    private bool _mouseLeftYawShapingEnabled;
    private string _mouseLeftYawOutputCurve = "expo";
    private bool _mouseLeftYawOutputShapingEnabled;
    private bool _mouseLeftYawReturnShapingEnabled;
    private string _mouseLeftYawOutputShapeNodesText = "[]";
    private string _mouseLeftYawReturnShapeNodesText = "[]";
    private string _aimSensitivityX = "";
    private string _aimSensitivityY = "";
    private string _aimReticleLimit = "";
    private string _aimReticleDeadband = "";
    private string _aimReturnRate = "";
    private string _aimOutputSmoothing = "";
    private string _aimRollGain = "";
    private string _aimYawGain = "";
    private string _aimPitchGain = "";
    private string _aimRollMax = "";
    private string _aimYawMax = "";
    private string _aimPitchMax = "";
    private string _aimSlewRate = "";
    private bool _aimInvertX;
    private bool _aimInvertY;
    private string _catalogSearchText = "";
    private bool _showBasicSettings = true;
    private bool _showAdvancedSettings = true;
    private bool _showExperimentalSettings = true;
    private bool _showChangedSettingsOnly;
    private bool _showInvalidSettingsOnly;
    private SettingCatalogRow? _selectedCatalogRow;
    private string _catalogJumpText = "No catalog row selected.";
    private string _catalogEditValue = "";
    private string _tooltipImageStatus = "Select a catalog setting to manage its tooltip image.";
    private bool _aboveBarSpriteRandomReturnDelay = true;
    private string _aboveBarSpriteFixedReturnDelaySeconds = UiSettingsService.DefaultAboveBarSpriteFixedReturnDelaySeconds.ToString(CultureInfo.InvariantCulture);
    private string _rawTomlText = "";
    private string _deviceScanSeconds = "10";
    private string _deviceAssignmentSummary = "Run GameInput enumeration to check this profile's mouse assignments.";
    private string _mouseLeftDryRunSeconds = "10";
    private bool _isDiagnosticRunning;
    private string _diagnosticStatus = "Diagnostics not run.";
    private string _diagnosticCommandLine = "";
    private string _diagnosticOutput = "";
    private string _latestDiagnosticLogPath = "";
    private DiagnosticHistoryItem? _selectedDiagnosticHistoryItem;
    private string _saveStateKind = "Saved";
    private string _saveStateText = "Loaded.";

    public ProfileEditorViewModel(ProfileRepository repository, ProfileSummary profile)
        : this(repository, profile, new UiSettingsService(), new TooltipImagePicker())
    {
    }

    public ProfileEditorViewModel(
        ProfileRepository repository,
        ProfileSummary profile,
        UiSettingsService uiSettingsService,
        ITooltipImagePicker tooltipImagePicker)
    {
        _repository = repository;
        _diagnosticsService = new Gx12DiagnosticsService();
        _diagnosticLogStore = new Gx12DiagnosticLogStore();
        _uiSettingsService = uiSettingsService;
        _tooltipImagePicker = tooltipImagePicker;
        _uiSettings = _uiSettingsService.Load(repository.Paths);
        LoadAboveBarSpriteSettings();
        _profile = profile;
        CatalogRows = new ObservableCollection<SettingCatalogRow>();
        GameInputDevices = new ObservableCollection<GameInputMouseDeviceRecord>();
        DeviceAssignmentStatuses = new ObservableCollection<DeviceAssignmentStatus>();
        DiagnosticHistory = new ObservableCollection<DiagnosticHistoryItem>();
        CatalogRowsView = CollectionViewSource.GetDefaultView(CatalogRows);
        CatalogRowsView.Filter = FilterCatalogRow;
        JumpToCatalogRowCommand = new RelayCommand(parameter => JumpToCatalogRow(parameter as SettingCatalogRow));
        ApplyCatalogValueCommand = new RelayCommand(ApplyCatalogValue, () => SelectedCatalogRow is not null);
        ResetCatalogValueCommand = new RelayCommand(ResetCatalogValue, () => SelectedCatalogRow is not null);
        RunMouseDevicesCommand = new RelayCommand(RunMouseDevices, CanStartDiagnostic);
        RunShowProfileCommand = new RelayCommand(RunShowProfile, CanStartDiagnostic);
        RunMouseLeftDryRunCommand = new RelayCommand(RunMouseLeftDryRun, CanStartDiagnostic);
        RunGimbalPreviewCommand = new RelayCommand(RunGimbalPreview, CanStartDiagnostic);
        StopDiagnosticCommand = new RelayCommand(StopDiagnostic, () => IsDiagnosticRunning);
        CopyCommandLineCommand = new RelayCommand(CopyCommandLine);
        CopyDiagnosticOutputCommand = new RelayCommand(CopyDiagnosticOutput, () => !string.IsNullOrWhiteSpace(DiagnosticText));
        CopyDiagnosticLogPathCommand = new RelayCommand(CopyDiagnosticLogPath, () => !string.IsNullOrWhiteSpace(LatestDiagnosticLogPath));
        ChooseTooltipImageCommand = new RelayCommand(ChooseTooltipImage, () => SelectedCatalogRow is not null);
        ClearTooltipImageCommand = new RelayCommand(ClearTooltipImage, () => SelectedCatalogRow?.HasTooltipImageMapping == true);
        CopyTooltipImagePathCommand = new RelayCommand(CopyTooltipImagePath, () => SelectedCatalogRow?.HasTooltipImageMapping == true);
        CopyTooltipSpriteDirectoryCommand = new RelayCommand(CopyTooltipSpriteDirectory);
        RefreshTooltipSpritesCommand = new RelayCommand(RefreshTooltipSprites);
        TooltipSpriteService.Shared.Configure(_uiSettingsService.GetTooltipSpriteDirectory(_repository.Paths));
        Reload(profile);
    }

    public event EventHandler? ProfileSaved;

    public ObservableCollection<SettingCatalogRow> CatalogRows { get; }

    public ObservableCollection<GameInputMouseDeviceRecord> GameInputDevices { get; }

    public ObservableCollection<DeviceAssignmentStatus> DeviceAssignmentStatuses { get; }

    public ObservableCollection<DiagnosticHistoryItem> DiagnosticHistory { get; }

    public ICollectionView CatalogRowsView { get; }

    public ICommand JumpToCatalogRowCommand { get; }

    public RelayCommand ApplyCatalogValueCommand { get; }

    public RelayCommand ResetCatalogValueCommand { get; }

    public RelayCommand ChooseTooltipImageCommand { get; }

    public RelayCommand ClearTooltipImageCommand { get; }

    public RelayCommand CopyTooltipImagePathCommand { get; }

    public RelayCommand CopyTooltipSpriteDirectoryCommand { get; }

    public RelayCommand RefreshTooltipSpritesCommand { get; }

    public RelayCommand RunMouseDevicesCommand { get; }

    public RelayCommand RunShowProfileCommand { get; }

    public RelayCommand RunMouseLeftDryRunCommand { get; }

    public RelayCommand RunGimbalPreviewCommand { get; }

    public RelayCommand StopDiagnosticCommand { get; }

    public ICommand CopyCommandLineCommand { get; }

    public RelayCommand CopyDiagnosticOutputCommand { get; }

    public RelayCommand CopyDiagnosticLogPathCommand { get; }

    public string FileName => _profile.FileName;

    public string FullPath => _profile.FullPath;

    public string SaveStateKind
    {
        get => _saveStateKind;
        private set => SetProperty(ref _saveStateKind, value);
    }

    public string SaveStateText
    {
        get => _saveStateText;
        private set => SetProperty(ref _saveStateText, value);
    }

    public bool IsMouseAimMode => ControlMode.Equals("drone_mouse_aim", StringComparison.OrdinalIgnoreCase);

    public bool IsDirectMouseMode => !IsMouseAimMode;

    public bool MouseAimEnabled
    {
        get => IsMouseAimMode;
        set
        {
            var targetMode = value ? "drone_mouse_aim" : "direct_mouse";
            if (!ControlMode.Equals(targetMode, StringComparison.OrdinalIgnoreCase))
            {
                ControlMode = targetMode;
            }
        }
    }

    public bool IsRightStickBasicActive => IsDirectMouseMode && MouseRightEnabled;

    public bool IsKeyboardLeftSelected =>
        IsDirectMouseMode &&
        LeftStickSource.Equals(LeftSourceKeyboard, StringComparison.Ordinal);

    public bool IsMouseLeftSelected =>
        IsDirectMouseMode &&
        LeftStickSource.Equals(LeftSourceMouse, StringComparison.Ordinal);

    public bool IsOutputShapeActive =>
        IsDirectMouseMode &&
        OutputCurve.Equals("nodes", StringComparison.OrdinalIgnoreCase);

    public bool IsOutputCurveExpoSelected =>
        IsDirectMouseMode &&
        OutputCurve.Equals("expo", StringComparison.OrdinalIgnoreCase);

    public bool IsOutputCurveActualSelected =>
        IsDirectMouseMode &&
        OutputCurve.Equals("actual", StringComparison.OrdinalIgnoreCase);

    public bool IsIdleReturnActive => IsDirectMouseMode && ReturnEnabled;

    public bool IsConstantReturnActive => IsDirectMouseMode && ConstantReturnEnabled;

    public bool IsElasticReturnActive => IsDirectMouseMode && ElasticReturnEnabled;

    public bool IsReturnShapeActive => IsDirectMouseMode && ReturnShapingEnabled;

    public bool IsInputFilterSmoothingSelected =>
        IsDirectMouseMode &&
        InputFilter.Equals("smoothing", StringComparison.OrdinalIgnoreCase);

    public bool IsInputFilterOneEuroSelected =>
        IsDirectMouseMode &&
        InputFilter.Equals("one_euro", StringComparison.OrdinalIgnoreCase);

    public bool IsDespikeActive => IsDirectMouseMode && DespikeEnabled;

    public bool IsDynamicGimbalSelected =>
        IsDirectMouseMode &&
        PositionModel.Equals("dynamic_gimbal", StringComparison.OrdinalIgnoreCase);

    public bool IsGimbalAntiwindupParameterActive =>
        IsDynamicGimbalSelected &&
        GimbalAntiwindupEnabled;

    public bool IsAdaptiveGainSelected =>
        IsDirectMouseMode &&
        InputGainMode.Equals("adaptive", StringComparison.OrdinalIgnoreCase);

    public bool IsRadialGateSelected =>
        IsDirectMouseMode &&
        !GateShape.Equals("axis", StringComparison.OrdinalIgnoreCase);

    public bool IsMouseLeftYawShapingActive =>
        IsMouseLeftSelected &&
        MouseLeftYawShapingEnabled;

    public bool IsMouseLeftYawOutputShapeActive =>
        IsMouseLeftYawShapingActive &&
        (MouseLeftYawOutputShapingEnabled ||
         MouseLeftYawOutputCurve.Equals("nodes", StringComparison.OrdinalIgnoreCase));

    public bool IsMouseLeftYawReturnShapeActive =>
        IsMouseLeftYawShapingActive &&
        MouseLeftYawReturnShapingEnabled;

    public string CatalogSearchText
    {
        get => _catalogSearchText;
        set
        {
            if (SetProperty(ref _catalogSearchText, value))
            {
                RefreshCatalogView();
            }
        }
    }

    public bool ShowBasicSettings
    {
        get => _showBasicSettings;
        set
        {
            if (SetProperty(ref _showBasicSettings, value))
            {
                RefreshCatalogView();
            }
        }
    }

    public bool ShowAdvancedSettings
    {
        get => _showAdvancedSettings;
        set
        {
            if (SetProperty(ref _showAdvancedSettings, value))
            {
                RefreshCatalogView();
            }
        }
    }

    public bool ShowExperimentalSettings
    {
        get => _showExperimentalSettings;
        set
        {
            if (SetProperty(ref _showExperimentalSettings, value))
            {
                RefreshCatalogView();
            }
        }
    }

    public bool ShowChangedSettingsOnly
    {
        get => _showChangedSettingsOnly;
        set
        {
            if (SetProperty(ref _showChangedSettingsOnly, value))
            {
                RefreshCatalogView();
            }
        }
    }

    public bool ShowInvalidSettingsOnly
    {
        get => _showInvalidSettingsOnly;
        set
        {
            if (SetProperty(ref _showInvalidSettingsOnly, value))
            {
                RefreshCatalogView();
            }
        }
    }

    public string CatalogSummary
    {
        get
        {
            var visibleCount = CatalogRowsView.Cast<object>().Count();
            var changedCount = CatalogRows.Count(row => row.IsChangedFromDefault);
            var invalidCount = CatalogRows.Count(row => row.IsInvalid);
            return $"{visibleCount} of {CatalogRows.Count} settings / {changedCount} changed / {invalidCount} invalid";
        }
    }

    public SettingCatalogRow? SelectedCatalogRow
    {
        get => _selectedCatalogRow;
        set
        {
            if (SetProperty(ref _selectedCatalogRow, value))
            {
                CatalogJumpText = value is null
                    ? "No catalog row selected."
                    : $"{value.TomlPath} lives on {value.Page}; current value is {value.DisplayValue}.";
                TooltipImageStatus = value?.TooltipImageStatus ?? "Select a catalog setting to manage its tooltip image.";
                OnPropertyChanged(nameof(SelectedCatalogLabel));
                OnPropertyChanged(nameof(SelectedCatalogHelp));
                OnPropertyChanged(nameof(SelectedCatalogDetail));
                OnPropertyChanged(nameof(SelectedCatalogCurrentValue));
                OnPropertyChanged(nameof(SelectedCatalogDefaultValue));
                OnPropertyChanged(nameof(SelectedCatalogRawValue));
                OnPropertyChanged(nameof(SelectedCatalogAllowedValues));
                OnPropertyChanged(nameof(IsCatalogEditEnabled));
                OnPropertyChanged(nameof(SelectedTooltipImagePath));
                OnPropertyChanged(nameof(HasSelectedTooltipImage));
                OnPropertyChanged(nameof(SelectedTooltipImageDisplayPath));
                CatalogEditValue = value?.DisplayValue ?? "";
                ApplyCatalogValueCommand.RaiseCanExecuteChanged();
                ResetCatalogValueCommand.RaiseCanExecuteChanged();
                ChooseTooltipImageCommand.RaiseCanExecuteChanged();
                ClearTooltipImageCommand.RaiseCanExecuteChanged();
                CopyTooltipImagePathCommand.RaiseCanExecuteChanged();
            }
        }
    }

    public string SelectedCatalogLabel => SelectedCatalogRow?.Label ?? "No setting selected";

    public string SelectedCatalogHelp => SelectedCatalogRow?.Help ?? "";

    public string SelectedCatalogDetail => SelectedCatalogRow is null
        ? "Select a setting to edit it."
        : $"{SelectedCatalogRow.TomlPath} / {SelectedCatalogRow.KindLabel} / {SelectedCatalogRow.TierLabel}";

    public string SelectedCatalogCurrentValue => SelectedCatalogRow?.DisplayValue ?? "";

    public string SelectedCatalogDefaultValue => SelectedCatalogRow?.DefaultDisplayValue ?? "";

    public string SelectedCatalogRawValue => SelectedCatalogRow?.RawValue ?? "";

    public string SelectedCatalogAllowedValues => SelectedCatalogRow is null ||
                                                  string.IsNullOrWhiteSpace(SelectedCatalogRow.AllowedValuesText)
        ? ""
        : $"Allowed: {SelectedCatalogRow.AllowedValuesText}";

    public bool IsCatalogEditEnabled => SelectedCatalogRow is not null;

    public string CatalogEditValue
    {
        get => _catalogEditValue;
        set => SetProperty(ref _catalogEditValue, value);
    }

    public string SelectedTooltipImagePath => SelectedCatalogRow?.TooltipImagePath ?? "";

    public bool HasSelectedTooltipImage => SelectedCatalogRow?.HasTooltipImage == true;

    public string SelectedTooltipImageDisplayPath => SelectedCatalogRow?.TooltipImageFullPath ?? "";

    public string CatalogJumpText
    {
        get => _catalogJumpText;
        private set => SetProperty(ref _catalogJumpText, value);
    }

    public string TooltipImageStatus
    {
        get => _tooltipImageStatus;
        private set => SetProperty(ref _tooltipImageStatus, value);
    }

    public string UiSettingsFilePath => _uiSettingsService.GetSettingsPath(_repository.Paths);

    public string TooltipImageDirectory => _uiSettingsService.GetTooltipImageDirectory(_repository.Paths);

    public string TooltipSpriteDirectory => _uiSettingsService.GetTooltipSpriteDirectory(_repository.Paths);

    public string TooltipSettingsSummary => $"{_uiSettings.TooltipImages.Count} setting tooltip image(s) mapped.";

    public string TooltipSpriteSummary
    {
        get
        {
            var count = TooltipSpriteService.EnumerateSpritePaths(TooltipSpriteDirectory).Count;
            return $"{count} PNG sprite(s) available; one is randomly added when any tooltip opens.";
        }
    }

    public bool AboveBarSpriteRandomReturnDelay
    {
        get => _aboveBarSpriteRandomReturnDelay;
        set
        {
            if (SetProperty(ref _aboveBarSpriteRandomReturnDelay, value))
            {
                _uiSettings.AboveBarSpriteRandomReturnDelay = value;
                SaveUiSettings();
                OnPropertyChanged(nameof(IsAboveBarSpriteFixedReturnDelayEnabled));
                OnPropertyChanged(nameof(AboveBarSpriteReturnSummary));
                TooltipImageStatus = value
                    ? "Asuka sprite now returns after a random delay."
                    : "Asuka sprite now returns after the fixed delay.";
            }
        }
    }

    public bool IsAboveBarSpriteFixedReturnDelayEnabled => !AboveBarSpriteRandomReturnDelay;

    public int AboveBarSpriteFixedReturnDelaySeconds => _uiSettings.AboveBarSpriteFixedReturnDelaySeconds;

    public string AboveBarSpriteFixedReturnDelaySecondsText
    {
        get => _aboveBarSpriteFixedReturnDelaySeconds;
        set
        {
            if (!TryNormalizeAboveBarSpriteDelay(value, out var delaySeconds, out var normalizedText, out var status))
            {
                _aboveBarSpriteFixedReturnDelaySeconds = _uiSettings.AboveBarSpriteFixedReturnDelaySeconds.ToString(CultureInfo.InvariantCulture);
                OnPropertyChanged();
                TooltipImageStatus = status;
                return;
            }

            var changedText = SetProperty(ref _aboveBarSpriteFixedReturnDelaySeconds, normalizedText);
            if (_uiSettings.AboveBarSpriteFixedReturnDelaySeconds == delaySeconds)
            {
                if (changedText)
                {
                    OnPropertyChanged(nameof(AboveBarSpriteFixedReturnDelaySeconds));
                    OnPropertyChanged(nameof(AboveBarSpriteReturnSummary));
                }

                return;
            }

            _uiSettings.AboveBarSpriteFixedReturnDelaySeconds = delaySeconds;
            SaveUiSettings();
            OnPropertyChanged(nameof(AboveBarSpriteFixedReturnDelaySeconds));
            OnPropertyChanged(nameof(AboveBarSpriteReturnSummary));
            TooltipImageStatus = status;
        }
    }

    public string AboveBarSpriteReturnSummary => AboveBarSpriteRandomReturnDelay
        ? "Asuka returns after a random 10-600 second delay."
        : $"Asuka returns after {AboveBarSpriteFixedReturnDelaySeconds} second(s).";


    public string RawTomlText
    {
        get => _rawTomlText;
        private set => SetProperty(ref _rawTomlText, value);
    }

    public string DeviceScanSeconds
    {
        get => _deviceScanSeconds;
        set
        {
            if (SetProperty(ref _deviceScanSeconds, value))
            {
                OnPropertyChanged(nameof(MouseDevicesCommandLine));
            }
        }
    }

    public string DeviceAssignmentSummary
    {
        get => _deviceAssignmentSummary;
        private set => SetProperty(ref _deviceAssignmentSummary, value);
    }

    public string MouseLeftDryRunSeconds
    {
        get => _mouseLeftDryRunSeconds;
        set
        {
            if (SetProperty(ref _mouseLeftDryRunSeconds, value))
            {
                OnPropertyChanged(nameof(MouseLeftDryRunCommandLine));
            }
        }
    }

    public bool IsDiagnosticRunning
    {
        get => _isDiagnosticRunning;
        private set
        {
            if (SetProperty(ref _isDiagnosticRunning, value))
            {
                RaiseDiagnosticCanExecuteChanged();
            }
        }
    }

    public string DiagnosticStatus
    {
        get => _diagnosticStatus;
        private set
        {
            if (SetProperty(ref _diagnosticStatus, value))
            {
                OnPropertyChanged(nameof(GimbalPreviewStatus));
            }
        }
    }

    public string DiagnosticCommandLine
    {
        get => _diagnosticCommandLine;
        private set
        {
            if (SetProperty(ref _diagnosticCommandLine, value))
            {
                OnPropertyChanged(nameof(DiagnosticText));
                OnPropertyChanged(nameof(GimbalPreviewText));
                CopyDiagnosticOutputCommand.RaiseCanExecuteChanged();
            }
        }
    }

    public string DiagnosticOutput
    {
        get => _diagnosticOutput;
        private set
        {
            if (SetProperty(ref _diagnosticOutput, value))
            {
                OnPropertyChanged(nameof(DiagnosticText));
                OnPropertyChanged(nameof(GimbalPreviewText));
                CopyDiagnosticOutputCommand.RaiseCanExecuteChanged();
            }
        }
    }

    public string DiagnosticText => string.IsNullOrWhiteSpace(DiagnosticOutput)
        ? DiagnosticCommandLine
        : $"{DiagnosticCommandLine}{Environment.NewLine}{Environment.NewLine}{DiagnosticOutput}";

    public string LatestDiagnosticLogPath
    {
        get => _latestDiagnosticLogPath;
        private set
        {
            if (SetProperty(ref _latestDiagnosticLogPath, value))
            {
                OnPropertyChanged(nameof(DiagnosticLogPathDisplay));
                CopyDiagnosticLogPathCommand.RaiseCanExecuteChanged();
            }
        }
    }

    public string DiagnosticLogPathDisplay => string.IsNullOrWhiteSpace(LatestDiagnosticLogPath)
        ? "No diagnostic log written yet."
        : LatestDiagnosticLogPath;

    public string DiagnosticHistorySummary => DiagnosticHistory.Count == 0
        ? "No persisted diagnostics yet."
        : $"{DiagnosticHistory.Count} recent diagnostic(s), newest {DiagnosticHistory[0].CompletedAtText}.";

    public DiagnosticHistoryItem? SelectedDiagnosticHistoryItem
    {
        get => _selectedDiagnosticHistoryItem;
        set
        {
            if (SetProperty(ref _selectedDiagnosticHistoryItem, value) && value is not null)
            {
                LoadDiagnosticHistoryItem(value);
            }
        }
    }

    public string MouseDevicesCommandLine =>
        _diagnosticsService.BuildMouseDevicesGameInput(
            _repository.Paths,
            GetDurationForCommandLine(DeviceScanSeconds, fallback: 10)).CommandLine;

    public string ShowProfileCommandLine =>
        _diagnosticsService.BuildShowProfile(_repository.Paths, _profile.FullPath).CommandLine;

    public string MouseLeftDryRunCommandLine =>
        _diagnosticsService.BuildMouseLeftDryRun(
            _repository.Paths,
            _profile.FullPath,
            GetDurationForCommandLine(MouseLeftDryRunSeconds, fallback: 10)).CommandLine;

    public string GimbalPreviewCommandLine =>
        _diagnosticsService.BuildGimbalPreview(_repository.Paths, _profile.FullPath).CommandLine;

    public string GimbalPreviewStatus
    {
        get => DiagnosticStatus;
        private set => DiagnosticStatus = value;
    }

    public string GimbalPreviewText
    {
        get => DiagnosticText;
        private set => DiagnosticOutput = value;
    }

    private void ApplyCatalogValue()
    {
        if (SelectedCatalogRow is null)
        {
            return;
        }

        var definition = SelectedCatalogRow.Definition;
        var rawValue = ConvertCatalogEditToRaw(definition, CatalogEditValue);
        var result = SaveSingle(definition.Section, definition.Key, rawValue);
        if (result?.IsSuccess == true)
        {
            ApplyCatalogValueToEditor(definition, CatalogEditValue);
        }
    }

    private void ResetCatalogValue()
    {
        if (SelectedCatalogRow is null)
        {
            return;
        }

        var definition = SelectedCatalogRow.Definition;
        var result = SaveSingle(definition.Section, definition.Key, definition.DefaultRawValue);
        if (result?.IsSuccess == true)
        {
            ApplyCatalogValueToEditor(definition, FormatCatalogRawForEdit(definition, definition.DefaultRawValue));
        }
    }

    public string ProfileName
    {
        get => _profileName;
        set => SetStringValue(ref _profileName, value, "trainer", "name");
    }

    public string FrameRateHz
    {
        get => _frameRateHz;
        set => SetRawValue(ref _frameRateHz, value, "trainer", "frame_rate_hz");
    }

    public string ResolutionMode
    {
        get => _resolutionMode;
        set => SetResolutionMode(value);
    }

    public bool HighResolution2xEnabled
    {
        get => ResolutionMode.Equals("gx12_2x", StringComparison.OrdinalIgnoreCase);
        set
        {
            var targetMode = value ? "gx12_2x" : "legacy";
            if (!ResolutionMode.Equals(targetMode, StringComparison.OrdinalIgnoreCase))
            {
                ResolutionMode = targetMode;
            }
        }
    }

    public string ControlMode
    {
        get => _controlMode;
        set => SetControlMode(value);
    }

    public string StopKey
    {
        get => _stopKey;
        set => SetStringValue(ref _stopKey, value, "safety", "stop_key");
    }

    public string FreezeKey
    {
        get => _freezeKey;
        set => SetStringValue(ref _freezeKey, value, "safety", "freeze_key");
    }

    public string RollGain
    {
        get => _rollGain;
        set => SetRawValue(ref _rollGain, value, "mapper", "roll_gain");
    }

    public string PitchGain
    {
        get => _pitchGain;
        set => SetRawValue(ref _pitchGain, value, "mapper", "pitch_gain");
    }

    public string MaxOutput
    {
        get => _maxOutput;
        set => SetRawValue(ref _maxOutput, value, "mapper", "max_output");
    }

    public string Deadband
    {
        get => _deadband;
        set => SetRawValue(ref _deadband, value, "mapper", "deadband");
    }

    public string Expo
    {
        get => _expo;
        set => SetRawValue(ref _expo, value, "mapper", "expo");
    }

    public string OutputCurve
    {
        get => _outputCurve;
        set => SetOutputCurve(value);
    }

    public string ActualCenter
    {
        get => _actualCenter;
        set => SetRawValue(ref _actualCenter, value, "mapper", "actual_center");
    }

    public string ActualMax
    {
        get => _actualMax;
        set => SetRawValue(ref _actualMax, value, "mapper", "actual_max");
    }

    public string ActualExpo
    {
        get => _actualExpo;
        set => SetRawValue(ref _actualExpo, value, "mapper", "actual_expo");
    }

    public bool ReturnEnabled
    {
        get => _returnEnabled;
        set => SetBoolValue(ref _returnEnabled, value, "mapper", "return_enabled");
    }

    public string ReturnRate
    {
        get => _returnRate;
        set => SetRawValue(ref _returnRate, value, "mapper", "return_rate");
    }

    public string ReturnIdle
    {
        get => _returnIdle;
        set => SetRawValue(ref _returnIdle, value, "mapper", "return_idle_ms");
    }

    public bool ConstantReturnEnabled
    {
        get => _constantReturnEnabled;
        set => SetBoolValue(ref _constantReturnEnabled, value, "mapper", "constant_return_enabled");
    }

    public string ConstantReturnRate
    {
        get => _constantReturnRate;
        set => SetRawValue(ref _constantReturnRate, value, "mapper", "constant_return_rate");
    }

    public bool ElasticReturnEnabled
    {
        get => _elasticReturnEnabled;
        set => SetBoolValue(ref _elasticReturnEnabled, value, "mapper", "elastic_return_enabled");
    }

    public string ElasticReturnMode
    {
        get => _elasticReturnMode;
        set => SetStringValue(ref _elasticReturnMode, value, "mapper", "elastic_return_mode");
    }

    public string ElasticReturnCoefficient
    {
        get => _elasticReturnCoefficient;
        set => SetRawValue(ref _elasticReturnCoefficient, value, "mapper", "elastic_return_coefficient");
    }

    public string ElasticReturnCurve
    {
        get => _elasticReturnCurve;
        set => SetRawValue(ref _elasticReturnCurve, value, "mapper", "elastic_return_curve");
    }

    public bool ReturnShapingEnabled
    {
        get => _returnShapingEnabled;
        set => SetReturnShapingEnabled(value);
    }

    public string OutputShapeNodesText
    {
        get => _outputShapeNodesText;
        set => SetShapeNodesText(ref _outputShapeNodesText, value, "mapper", "output_shape_nodes");
    }

    public string ReturnShapeNodesText
    {
        get => _returnShapeNodesText;
        set => SetShapeNodesText(ref _returnShapeNodesText, value, "mapper", "return_shape_nodes");
    }

    public string OutputShapeSummary => $"Output nodes: {StickShapeCurve.DescribeNodes(OutputShapeNodesText)}";

    public string ReturnShapeSummary => $"Return nodes: {StickShapeCurve.DescribeNodes(ReturnShapeNodesText)}";

    public string InputFilter
    {
        get => _inputFilter;
        set => SetStringValue(ref _inputFilter, value, "mapper", "input_filter");
    }

    public string Smoothing
    {
        get => _smoothing;
        set => SetRawValue(ref _smoothing, value, "mapper", "smoothing");
    }

    public string OneEuroMinCutoffHz
    {
        get => _oneEuroMinCutoffHz;
        set => SetRawValue(ref _oneEuroMinCutoffHz, value, "mapper", "one_euro_min_cutoff_hz");
    }

    public string OneEuroBeta
    {
        get => _oneEuroBeta;
        set => SetRawValue(ref _oneEuroBeta, value, "mapper", "one_euro_beta");
    }

    public string OneEuroDcutoffHz
    {
        get => _oneEuroDcutoffHz;
        set => SetRawValue(ref _oneEuroDcutoffHz, value, "mapper", "one_euro_dcutoff_hz");
    }

    public bool DespikeEnabled
    {
        get => _despikeEnabled;
        set => SetBoolValue(ref _despikeEnabled, value, "mapper", "despike_enabled");
    }

    public bool DespikeCountEnabled
    {
        get => _despikeCountEnabled;
        set => SetBoolValue(ref _despikeCountEnabled, value, "mapper", "despike_count_enabled");
    }

    public string DespikeWindow
    {
        get => _despikeWindow;
        set => SetRawValue(ref _despikeWindow, value, "mapper", "despike_window");
    }

    public string DespikeThresholdSigma
    {
        get => _despikeThresholdSigma;
        set => SetRawValue(ref _despikeThresholdSigma, value, "mapper", "despike_threshold_sigma");
    }

    public string PositionModel
    {
        get => _positionModel;
        set => SetStringValue(ref _positionModel, value, "mapper", "position_model");
    }

    public string GimbalFrequencyHz
    {
        get => _gimbalFrequencyHz;
        set => SetRawValue(ref _gimbalFrequencyHz, value, "mapper", "gimbal_frequency_hz");
    }

    public string GimbalDampingRatio
    {
        get => _gimbalDampingRatio;
        set => SetRawValue(ref _gimbalDampingRatio, value, "mapper", "gimbal_damping_ratio");
    }

    public string GimbalInputImpulse
    {
        get => _gimbalInputImpulse;
        set => SetRawValue(ref _gimbalInputImpulse, value, "mapper", "gimbal_input_impulse");
    }

    public string GimbalStaticFriction
    {
        get => _gimbalStaticFriction;
        set => SetRawValue(ref _gimbalStaticFriction, value, "mapper", "gimbal_static_friction");
    }

    public string GimbalDynamicFriction
    {
        get => _gimbalDynamicFriction;
        set => SetRawValue(ref _gimbalDynamicFriction, value, "mapper", "gimbal_dynamic_friction");
    }

    public string GimbalEdgeBumper
    {
        get => _gimbalEdgeBumper;
        set => SetRawValue(ref _gimbalEdgeBumper, value, "mapper", "gimbal_edge_bumper");
    }

    public bool GimbalAntiwindupEnabled
    {
        get => _gimbalAntiwindupEnabled;
        set => SetBoolValue(ref _gimbalAntiwindupEnabled, value, "mapper", "gimbal_antiwindup_enabled");
    }

    public string GimbalAntiwindupStart
    {
        get => _gimbalAntiwindupStart;
        set => SetRawValue(ref _gimbalAntiwindupStart, value, "mapper", "gimbal_antiwindup_start");
    }

    public string GimbalAntiwindupMinGain
    {
        get => _gimbalAntiwindupMinGain;
        set => SetRawValue(ref _gimbalAntiwindupMinGain, value, "mapper", "gimbal_antiwindup_min_gain");
    }

    public string InputGainMode
    {
        get => _inputGainMode;
        set => SetStringValue(ref _inputGainMode, value, "mapper", "input_gain_mode");
    }

    public string AdaptiveSlowGain
    {
        get => _adaptiveSlowGain;
        set => SetRawValue(ref _adaptiveSlowGain, value, "mapper", "adaptive_slow_gain");
    }

    public string AdaptiveFastGain
    {
        get => _adaptiveFastGain;
        set => SetRawValue(ref _adaptiveFastGain, value, "mapper", "adaptive_fast_gain");
    }

    public string AdaptiveSpeedLow
    {
        get => _adaptiveSpeedLow;
        set => SetRawValue(ref _adaptiveSpeedLow, value, "mapper", "adaptive_speed_low");
    }

    public string AdaptiveSpeedHigh
    {
        get => _adaptiveSpeedHigh;
        set => SetRawValue(ref _adaptiveSpeedHigh, value, "mapper", "adaptive_speed_high");
    }

    public string AdaptiveCurve
    {
        get => _adaptiveCurve;
        set => SetRawValue(ref _adaptiveCurve, value, "mapper", "adaptive_curve");
    }

    public string AdaptiveTrackerMs
    {
        get => _adaptiveTrackerMs;
        set => SetRawValue(ref _adaptiveTrackerMs, value, "mapper", "adaptive_tracker_ms");
    }

    public string GateShape
    {
        get => _gateShape;
        set => SetStringValue(ref _gateShape, value, "mapper", "gate_shape");
    }

    public string DiagonalScale
    {
        get => _diagonalScale;
        set => SetRawValue(ref _diagonalScale, value, "mapper", "diagonal_scale");
    }

    public bool MouseRightEnabled
    {
        get => _mouseRightEnabled;
        set => SetBoolValue(ref _mouseRightEnabled, value, "mouse_right_stick", "enabled");
    }

    public bool InvertRoll
    {
        get => _invertRoll;
        set => SetBoolValue(ref _invertRoll, value, "mapper", "invert_roll");
    }

    public bool InvertPitch
    {
        get => _invertPitch;
        set => SetBoolValue(ref _invertPitch, value, "mapper", "invert_pitch");
    }

    public bool SwapAxes
    {
        get => _swapAxes;
        set => SetBoolValue(ref _swapAxes, value, "mapper", "swap_axes");
    }

    public string LeftStickSource
    {
        get => _leftStickSource;
        set => SetLeftStickSource(value);
    }

    public string KeyboardInputSource
    {
        get => _keyboardInputSource;
        set => SetStringValue(ref _keyboardInputSource, value, "keyboard_left_stick", "input_source");
    }

    public bool KeyboardRequireAnalog
    {
        get => _keyboardRequireAnalog;
        set => SetBoolValue(ref _keyboardRequireAnalog, value, "keyboard_left_stick", "require_analog");
    }

    public bool MouseLeftRequireDevice
    {
        get => _mouseLeftRequireDevice;
        set => SetBoolValue(ref _mouseLeftRequireDevice, value, "mouse_left_stick", "require_device");
    }

    public string MouseLeftThrottleRate
    {
        get => _mouseLeftThrottleRate;
        set => SetRawValue(ref _mouseLeftThrottleRate, value, "mouse_left_stick", "throttle_rate");
    }

    public string MouseLeftYawGain
    {
        get => _mouseLeftYawGain;
        set => SetRawValue(ref _mouseLeftYawGain, value, "mouse_left_stick", "yaw_gain");
    }

    public string MouseLeftYawPulse
    {
        get => _mouseLeftYawPulse;
        set => SetRawValue(ref _mouseLeftYawPulse, value, "mouse_left_stick", "yaw_pulse");
    }

    public string MouseLeftYawDeadband
    {
        get => _mouseLeftYawDeadband;
        set => SetRawValue(ref _mouseLeftYawDeadband, value, "mouse_left_stick", "yaw_deadband");
    }

    public bool MouseLeftInvertThrottle
    {
        get => _mouseLeftInvertThrottle;
        set => SetBoolValue(ref _mouseLeftInvertThrottle, value, "mouse_left_stick", "invert_throttle");
    }

    public bool MouseLeftInvertYaw
    {
        get => _mouseLeftInvertYaw;
        set => SetBoolValue(ref _mouseLeftInvertYaw, value, "mouse_left_stick", "invert_yaw");
    }

    public bool MouseLeftSwapAxes
    {
        get => _mouseLeftSwapAxes;
        set => SetBoolValue(ref _mouseLeftSwapAxes, value, "mouse_left_stick", "swap_axes");
    }

    public bool MouseLeftYawShapingEnabled
    {
        get => _mouseLeftYawShapingEnabled;
        set => SetMouseLeftYawBoolValue(ref _mouseLeftYawShapingEnabled, value, "yaw_shaping_enabled");
    }

    public string MouseLeftYawOutputCurve
    {
        get => _mouseLeftYawOutputCurve;
        set => SetMouseLeftYawOutputCurve(value);
    }

    public bool MouseLeftYawOutputShapingEnabled
    {
        get => _mouseLeftYawOutputShapingEnabled;
        set => SetMouseLeftYawBoolValue(ref _mouseLeftYawOutputShapingEnabled, value, "yaw_output_shaping_enabled");
    }

    public bool MouseLeftYawReturnShapingEnabled
    {
        get => _mouseLeftYawReturnShapingEnabled;
        set => SetMouseLeftYawBoolValue(ref _mouseLeftYawReturnShapingEnabled, value, "yaw_return_shaping_enabled");
    }

    public string MouseLeftYawOutputShapeNodesText
    {
        get => _mouseLeftYawOutputShapeNodesText;
        set => SetMouseLeftYawShapeNodesText(ref _mouseLeftYawOutputShapeNodesText, value, "yaw_output_shape_nodes");
    }

    public string MouseLeftYawReturnShapeNodesText
    {
        get => _mouseLeftYawReturnShapeNodesText;
        set => SetMouseLeftYawShapeNodesText(ref _mouseLeftYawReturnShapeNodesText, value, "yaw_return_shape_nodes");
    }

    public string MouseLeftYawOutputShapeSummary => $"Yaw output nodes: {StickShapeCurve.DescribeNodes(MouseLeftYawOutputShapeNodesText)}";

    public string MouseLeftYawReturnShapeSummary => $"Yaw return nodes: {StickShapeCurve.DescribeNodes(MouseLeftYawReturnShapeNodesText)}";

    public string AimSensitivityX
    {
        get => _aimSensitivityX;
        set => SetRawValue(ref _aimSensitivityX, value, "mouse_aim", "sensitivity_x");
    }

    public string AimSensitivityY
    {
        get => _aimSensitivityY;
        set => SetRawValue(ref _aimSensitivityY, value, "mouse_aim", "sensitivity_y");
    }

    public string AimReticleLimit
    {
        get => _aimReticleLimit;
        set => SetRawValue(ref _aimReticleLimit, value, "mouse_aim", "reticle_limit");
    }

    public string AimReticleDeadband
    {
        get => _aimReticleDeadband;
        set => SetRawValue(ref _aimReticleDeadband, value, "mouse_aim", "reticle_deadband");
    }

    public string AimReturnRate
    {
        get => _aimReturnRate;
        set => SetRawValue(ref _aimReturnRate, value, "mouse_aim", "reticle_return_rate");
    }

    public string AimOutputSmoothing
    {
        get => _aimOutputSmoothing;
        set => SetRawValue(ref _aimOutputSmoothing, value, "mouse_aim", "output_smoothing");
    }

    public string AimRollGain
    {
        get => _aimRollGain;
        set => SetRawValue(ref _aimRollGain, value, "mouse_aim", "roll_gain");
    }

    public string AimYawGain
    {
        get => _aimYawGain;
        set => SetRawValue(ref _aimYawGain, value, "mouse_aim", "yaw_gain");
    }

    public string AimPitchGain
    {
        get => _aimPitchGain;
        set => SetRawValue(ref _aimPitchGain, value, "mouse_aim", "pitch_gain");
    }

    public string AimRollMax
    {
        get => _aimRollMax;
        set => SetRawValue(ref _aimRollMax, value, "mouse_aim", "roll_max");
    }

    public string AimYawMax
    {
        get => _aimYawMax;
        set => SetRawValue(ref _aimYawMax, value, "mouse_aim", "yaw_max");
    }

    public string AimPitchMax
    {
        get => _aimPitchMax;
        set => SetRawValue(ref _aimPitchMax, value, "mouse_aim", "pitch_max");
    }

    public string AimSlewRate
    {
        get => _aimSlewRate;
        set => SetRawValue(ref _aimSlewRate, value, "mouse_aim", "slew_rate");
    }

    public bool AimInvertX
    {
        get => _aimInvertX;
        set => SetBoolValue(ref _aimInvertX, value, "mouse_aim", "invert_x");
    }

    public bool AimInvertY
    {
        get => _aimInvertY;
        set => SetBoolValue(ref _aimInvertY, value, "mouse_aim", "invert_y");
    }

    public void Reload(ProfileSummary profile)
    {
        _profile = profile;
        _uiSettings = _uiSettingsService.Load(_repository.Paths);
        OnTooltipSettingsPropertiesChanged();
        _loading = true;

        try
        {
            var document = File.Exists(profile.FullPath)
                ? TomlProfileDocument.Load(profile.FullPath)
                : TomlProfileDocument.LoadText("");

            ProfileName = document.GetString("trainer", "name", Path.GetFileNameWithoutExtension(profile.FileName));
            FrameRateHz = document.GetRaw("trainer", "frame_rate_hz", "1000");
            ResolutionMode = document.GetString("trainer", "resolution_mode", "legacy");
            ControlMode = document.GetString("control", "mode", "direct_mouse");
            StopKey = document.GetString("safety", "stop_key", "Esc");
            FreezeKey = document.GetString("safety", "freeze_key", "F2");
            RollGain = document.GetRaw("mapper", "roll_gain", "50.0");
            PitchGain = document.GetRaw("mapper", "pitch_gain", "50.0");
            MaxOutput = document.GetRaw("mapper", "max_output", "512");
            Deadband = document.GetRaw("mapper", "deadband", "0");
            Expo = document.GetRaw("mapper", "expo", "0");
            ActualCenter = document.GetRaw("mapper", "actual_center", "0.45");
            ActualMax = document.GetRaw("mapper", "actual_max", "1.0");
            ActualExpo = document.GetRaw("mapper", "actual_expo", "0.30");
            ReturnEnabled = document.GetBool("mapper", "return_enabled", false);
            ReturnRate = document.GetRaw("mapper", "return_rate", "0");
            ReturnIdle = document.GetRaw("mapper", "return_idle_ms", "0");
            ConstantReturnEnabled = document.GetBool("mapper", "constant_return_enabled", false);
            ConstantReturnRate = document.GetRaw("mapper", "constant_return_rate", "0");
            ElasticReturnEnabled = document.GetBool("mapper", "elastic_return_enabled", false);
            ElasticReturnMode = document.GetString("mapper", "elastic_return_mode", "progressive");
            ElasticReturnCoefficient = document.GetRaw("mapper", "elastic_return_coefficient", "0");
            ElasticReturnCurve = document.GetRaw("mapper", "elastic_return_curve", "0");
            var outputShapingEnabled = document.GetBool("mapper", "output_shaping_enabled", false);
            OutputCurve = document.GetString("mapper", "output_curve", outputShapingEnabled ? "nodes" : "expo");
            ReturnShapingEnabled = document.GetBool("mapper", "return_shaping_enabled", false);
            OutputShapeNodesText = document.GetRaw("mapper", "output_shape_nodes", "[]");
            ReturnShapeNodesText = document.GetRaw("mapper", "return_shape_nodes", "[]");
            InputFilter = document.GetString("mapper", "input_filter", "off");
            Smoothing = document.GetRaw("mapper", "smoothing", "0.0");
            OneEuroMinCutoffHz = document.GetRaw("mapper", "one_euro_min_cutoff_hz", "1.0");
            OneEuroBeta = document.GetRaw("mapper", "one_euro_beta", "0.05");
            OneEuroDcutoffHz = document.GetRaw("mapper", "one_euro_dcutoff_hz", "1.0");
            DespikeEnabled = document.GetBool("mapper", "despike_enabled", false);
            DespikeCountEnabled = document.GetBool("mapper", "despike_count_enabled", false);
            DespikeWindow = document.GetRaw("mapper", "despike_window", "5");
            DespikeThresholdSigma = document.GetRaw("mapper", "despike_threshold_sigma", "3.0");
            PositionModel = document.GetString("mapper", "position_model", "integrator");
            GimbalFrequencyHz = document.GetRaw("mapper", "gimbal_frequency_hz", "5.0");
            GimbalDampingRatio = document.GetRaw("mapper", "gimbal_damping_ratio", "1.15");
            GimbalInputImpulse = document.GetRaw("mapper", "gimbal_input_impulse", "1.0");
            GimbalStaticFriction = document.GetRaw("mapper", "gimbal_static_friction", "0.0");
            GimbalDynamicFriction = document.GetRaw("mapper", "gimbal_dynamic_friction", "0.0");
            GimbalEdgeBumper = document.GetRaw("mapper", "gimbal_edge_bumper", "0.0");
            GimbalAntiwindupEnabled = document.GetBool("mapper", "gimbal_antiwindup_enabled", true);
            GimbalAntiwindupStart = document.GetRaw("mapper", "gimbal_antiwindup_start", "0.92");
            GimbalAntiwindupMinGain = document.GetRaw("mapper", "gimbal_antiwindup_min_gain", "0.10");
            InputGainMode = document.GetString("mapper", "input_gain_mode", "flat");
            AdaptiveSlowGain = document.GetRaw("mapper", "adaptive_slow_gain", "0.65");
            AdaptiveFastGain = document.GetRaw("mapper", "adaptive_fast_gain", "1.60");
            AdaptiveSpeedLow = document.GetRaw("mapper", "adaptive_speed_low", "120.0");
            AdaptiveSpeedHigh = document.GetRaw("mapper", "adaptive_speed_high", "1800.0");
            AdaptiveCurve = document.GetRaw("mapper", "adaptive_curve", "1.0");
            AdaptiveTrackerMs = document.GetRaw("mapper", "adaptive_tracker_ms", "35.0");
            GateShape = document.GetString("mapper", "gate_shape", "axis");
            DiagonalScale = document.GetRaw("mapper", "diagonal_scale", "1.0");
            MouseRightEnabled = document.GetBool("mouse_right_stick", "enabled", true);
            InvertRoll = document.GetBool("mapper", "invert_roll", false);
            InvertPitch = document.GetBool("mapper", "invert_pitch", false);
            SwapAxes = document.GetBool("mapper", "swap_axes", false);

            var keyboardEnabled = document.GetBool("keyboard_left_stick", "enabled", false);
            var mouseLeftEnabled = document.GetBool("mouse_left_stick", "enabled", false);
            LeftStickSource = mouseLeftEnabled
                ? LeftSourceMouse
                : keyboardEnabled
                    ? LeftSourceKeyboard
                    : LeftSourceOff;

            KeyboardInputSource = document.GetString("keyboard_left_stick", "input_source", "gameinput");
            KeyboardRequireAnalog = document.GetBool("keyboard_left_stick", "require_analog", false);
            MouseLeftRequireDevice = document.GetBool("mouse_left_stick", "require_device", true);
            MouseLeftThrottleRate = document.GetRaw("mouse_left_stick", "throttle_rate", "0.7");
            MouseLeftYawGain = document.GetRaw("mouse_left_stick", "yaw_gain", "0.7");
            MouseLeftYawPulse = document.GetRaw("mouse_left_stick", "yaw_pulse", "512");
            MouseLeftYawDeadband = document.GetRaw("mouse_left_stick", "yaw_deadband", "0");
            MouseLeftInvertThrottle = document.GetBool("mouse_left_stick", "invert_throttle", false);
            MouseLeftInvertYaw = document.GetBool("mouse_left_stick", "invert_yaw", false);
            MouseLeftSwapAxes = document.GetBool("mouse_left_stick", "swap_axes", false);
            MouseLeftYawShapingEnabled = document.GetBool(
                "mouse_left_stick",
                "yaw_shaping_enabled",
                document.GetBool("mouse_left_stick", "yaw_mapper_shaping_enabled", false));
            MouseLeftYawOutputShapingEnabled = document.GetBool("mouse_left_stick", "yaw_output_shaping_enabled", false);
            MouseLeftYawOutputCurve = document.GetString(
                "mouse_left_stick",
                "yaw_output_curve",
                MouseLeftYawOutputShapingEnabled ? "nodes" : "expo");
            MouseLeftYawReturnShapingEnabled = document.GetBool("mouse_left_stick", "yaw_return_shaping_enabled", false);
            MouseLeftYawOutputShapeNodesText = document.GetRaw("mouse_left_stick", "yaw_output_shape_nodes", "[]");
            MouseLeftYawReturnShapeNodesText = document.GetRaw("mouse_left_stick", "yaw_return_shape_nodes", "[]");

            AimSensitivityX = document.GetRaw("mouse_aim", "sensitivity_x", "1.0");
            AimSensitivityY = document.GetRaw("mouse_aim", "sensitivity_y", "1.0");
            AimReticleLimit = document.GetRaw("mouse_aim", "reticle_limit", "512");
            AimReticleDeadband = document.GetRaw("mouse_aim", "reticle_deadband", "8");
            AimReturnRate = document.GetRaw("mouse_aim", "reticle_return_rate", "0");
            AimOutputSmoothing = document.GetRaw("mouse_aim", "output_smoothing", "0.10");
            AimRollGain = document.GetRaw("mouse_aim", "roll_gain", "0.65");
            AimYawGain = document.GetRaw("mouse_aim", "yaw_gain", "0.55");
            AimPitchGain = document.GetRaw("mouse_aim", "pitch_gain", "0.85");
            AimRollMax = document.GetRaw("mouse_aim", "roll_max", "420");
            AimYawMax = document.GetRaw("mouse_aim", "yaw_max", "360");
            AimPitchMax = document.GetRaw("mouse_aim", "pitch_max", "420");
            AimSlewRate = document.GetRaw("mouse_aim", "slew_rate", "9000.0");
            AimInvertX = document.GetBool("mouse_aim", "invert_x", false);
            AimInvertY = document.GetBool("mouse_aim", "invert_y", false);

            RefreshCatalog(document);
            LoadDiagnosticHistory();
            ResetDiagnostics($"Diagnostics ready for {profile.FileName}.");
            ResetDeviceAssignmentCheck();
            SetSaveState("Saved", $"Loaded {profile.FileName}.");
        }
        catch (Exception exception)
        {
            SetSaveState("Invalid", $"Load failed: {exception.Message}");
        }
        finally
        {
            _loading = false;
            OnModePropertiesChanged();
            OnLeftSourcePropertiesChanged();
            OnShapePropertiesChanged();
            OnMouseLeftYawShapePropertiesChanged();
            OnPropertyChanged(nameof(FileName));
            OnPropertyChanged(nameof(FullPath));
            OnDiagnosticCommandLinePropertiesChanged();
            RefreshCatalogView();
        }
    }

    private void SetRawValue(ref string field, string value, string section, string key)
    {
        if (!SetProperty(ref field, value))
        {
            return;
        }

        if (!_loading)
        {
            SaveSingle(section, key, value.Trim());
        }
    }

    private void SetStringValue(ref string field, string value, string section, string key)
    {
        if (!SetProperty(ref field, value))
        {
            return;
        }

        OnOptionVisibilityPropertiesChanged();

        if (!_loading)
        {
            SaveSingle(section, key, TomlProfileDocument.QuoteString(value.Trim()));
        }
    }

    private void SetBoolValue(ref bool field, bool value, string section, string key)
    {
        if (!SetProperty(ref field, value))
        {
            return;
        }

        OnOptionVisibilityPropertiesChanged();

        if (!_loading)
        {
            SaveSingle(section, key, ToTomlBool(value));
        }
    }

    private void SetOutputCurve(string value)
    {
        var normalized = string.IsNullOrWhiteSpace(value) ? "expo" : value.Trim();
        if (!SetProperty(ref _outputCurve, normalized))
        {
            return;
        }

        OnShapePropertiesChanged();

        if (!_loading)
        {
            SaveSingle("mapper", "output_curve", TomlProfileDocument.QuoteString(normalized));
        }
    }

    private void SetResolutionMode(string value)
    {
        var normalized = string.IsNullOrWhiteSpace(value) ? "legacy" : value.Trim();
        if (!SetProperty(ref _resolutionMode, normalized))
        {
            return;
        }

        OnPropertyChanged(nameof(ResolutionMode));
        OnPropertyChanged(nameof(HighResolution2xEnabled));

        if (!_loading)
        {
            SaveSingle("trainer", "resolution_mode", TomlProfileDocument.QuoteString(normalized));
        }
    }

    private void SetReturnShapingEnabled(bool value)
    {
        if (!SetProperty(ref _returnShapingEnabled, value))
        {
            return;
        }

        OnShapePropertiesChanged();

        if (!_loading)
        {
            SaveSingle("mapper", "return_shaping_enabled", ToTomlBool(value));
        }
    }

    private void SetShapeNodesText(ref string field, string value, string section, string key)
    {
        var normalized = string.IsNullOrWhiteSpace(value) ? "[]" : value.Trim();
        if (!SetProperty(ref field, normalized))
        {
            return;
        }

        OnShapePropertiesChanged();

        if (!_loading)
        {
            SaveSingle(section, key, normalized);
        }
    }

    private void SetMouseLeftYawBoolValue(ref bool field, bool value, string key)
    {
        if (!SetProperty(ref field, value))
        {
            return;
        }

        OnMouseLeftYawShapePropertiesChanged();

        if (!_loading)
        {
            SaveSingle("mouse_left_stick", key, ToTomlBool(value));
        }
    }

    private void SetMouseLeftYawOutputCurve(string value)
    {
        var normalized = string.IsNullOrWhiteSpace(value) ? "expo" : value.Trim();
        if (!SetProperty(ref _mouseLeftYawOutputCurve, normalized))
        {
            return;
        }

        OnMouseLeftYawShapePropertiesChanged();

        if (!_loading)
        {
            SaveSingle("mouse_left_stick", "yaw_output_curve", TomlProfileDocument.QuoteString(normalized));
        }
    }

    private void SetMouseLeftYawShapeNodesText(ref string field, string value, string key)
    {
        var normalized = string.IsNullOrWhiteSpace(value) ? "[]" : value.Trim();
        if (!SetProperty(ref field, normalized))
        {
            return;
        }

        OnMouseLeftYawShapePropertiesChanged();

        if (!_loading)
        {
            SaveSingle("mouse_left_stick", key, normalized);
        }
    }

    private void SetControlMode(string value)
    {
        var normalized = string.IsNullOrWhiteSpace(value) ? "direct_mouse" : value;
        if (!SetProperty(ref _controlMode, normalized))
        {
            return;
        }

        OnModePropertiesChanged();

        if (_loading)
        {
            return;
        }

        if (IsMouseAimMode)
        {
            _leftStickSource = LeftSourceOff;
            OnPropertyChanged(nameof(LeftStickSource));
            OnLeftSourcePropertiesChanged();
            SaveMany(new[]
            {
                new ProfileValueUpdate("control", "mode", TomlProfileDocument.QuoteString(normalized)),
                new ProfileValueUpdate("keyboard_left_stick", "enabled", "false"),
                new ProfileValueUpdate("mouse_left_stick", "enabled", "false")
            });
            return;
        }

        SaveSingle("control", "mode", TomlProfileDocument.QuoteString(normalized));
    }

    private void SetLeftStickSource(string value)
    {
        var normalized = string.IsNullOrWhiteSpace(value) ? LeftSourceOff : value;
        if (!SetProperty(ref _leftStickSource, normalized))
        {
            return;
        }

        OnLeftSourcePropertiesChanged();

        if (_loading)
        {
            return;
        }

        var updates = new List<ProfileValueUpdate>();
        if (normalized.Equals(LeftSourceMouse, StringComparison.Ordinal) && IsMouseAimMode)
        {
            _controlMode = "direct_mouse";
            OnPropertyChanged(nameof(ControlMode));
            OnModePropertiesChanged();
            updates.Add(new ProfileValueUpdate("control", "mode", TomlProfileDocument.QuoteString("direct_mouse")));
        }

        updates.Add(new ProfileValueUpdate(
            "keyboard_left_stick",
            "enabled",
            normalized.Equals(LeftSourceKeyboard, StringComparison.Ordinal) ? "true" : "false"));
        updates.Add(new ProfileValueUpdate(
            "mouse_left_stick",
            "enabled",
            normalized.Equals(LeftSourceMouse, StringComparison.Ordinal) ? "true" : "false"));

        SaveMany(updates);
    }

    private ProfileSaveResult? SaveSingle(string section, string key, string rawValue)
    {
        return SaveMany(new[] { new ProfileValueUpdate(section, key, rawValue) });
    }

    private ProfileSaveResult? SaveMany(IReadOnlyList<ProfileValueUpdate> updates)
    {
        try
        {
            SetSaveState("Dirty", "Dirty: validating autosave...");
            var result = _repository.SaveProfileValues(_profile.FullPath, updates);
            if (!result.IsSuccess)
            {
                SetSaveState("Invalid", $"Invalid: {result.Message}");
                return result;
            }

            SetSaveState("Saved", result.Changed ? "Saved." : "Saved: no file change.");
            RefreshCatalog(TomlProfileDocument.Load(_profile.FullPath));
            if (result.Changed)
            {
                ProfileSaved?.Invoke(this, EventArgs.Empty);
            }

            return result;
        }
        catch (Exception exception)
        {
            SetSaveState("Invalid", $"Invalid: {exception.Message}");
            return null;
        }
    }

    private void SetSaveState(string kind, string text)
    {
        SaveStateKind = kind;
        SaveStateText = text;
    }

    private void OnModePropertiesChanged()
    {
        OnPropertyChanged(nameof(IsMouseAimMode));
        OnPropertyChanged(nameof(IsDirectMouseMode));
        OnPropertyChanged(nameof(MouseAimEnabled));
        OnPropertyChanged(nameof(IsKeyboardLeftSelected));
        OnPropertyChanged(nameof(IsMouseLeftSelected));
        OnShapePropertiesChanged();
        OnMouseLeftYawShapePropertiesChanged();
    }

    private void OnLeftSourcePropertiesChanged()
    {
        OnPropertyChanged(nameof(IsKeyboardLeftSelected));
        OnPropertyChanged(nameof(IsMouseLeftSelected));
        OnMouseLeftYawShapePropertiesChanged();
    }

    private void OnShapePropertiesChanged()
    {
        OnPropertyChanged(nameof(IsOutputShapeActive));
        OnPropertyChanged(nameof(IsOutputCurveExpoSelected));
        OnPropertyChanged(nameof(IsOutputCurveActualSelected));
        OnPropertyChanged(nameof(IsReturnShapeActive));
        OnPropertyChanged(nameof(OutputShapeSummary));
        OnPropertyChanged(nameof(ReturnShapeSummary));
        OnOptionVisibilityPropertiesChanged();
    }

    private void OnMouseLeftYawShapePropertiesChanged()
    {
        OnPropertyChanged(nameof(IsMouseLeftYawShapingActive));
        OnPropertyChanged(nameof(IsMouseLeftYawOutputShapeActive));
        OnPropertyChanged(nameof(IsMouseLeftYawReturnShapeActive));
        OnPropertyChanged(nameof(MouseLeftYawOutputShapeSummary));
        OnPropertyChanged(nameof(MouseLeftYawReturnShapeSummary));
    }

    private void OnOptionVisibilityPropertiesChanged()
    {
        OnPropertyChanged(nameof(IsRightStickBasicActive));
        OnPropertyChanged(nameof(IsIdleReturnActive));
        OnPropertyChanged(nameof(IsConstantReturnActive));
        OnPropertyChanged(nameof(IsElasticReturnActive));
        OnPropertyChanged(nameof(IsInputFilterSmoothingSelected));
        OnPropertyChanged(nameof(IsInputFilterOneEuroSelected));
        OnPropertyChanged(nameof(IsDespikeActive));
        OnPropertyChanged(nameof(IsDynamicGimbalSelected));
        OnPropertyChanged(nameof(IsGimbalAntiwindupParameterActive));
        OnPropertyChanged(nameof(IsAdaptiveGainSelected));
        OnPropertyChanged(nameof(IsRadialGateSelected));
        OnPropertyChanged(nameof(IsMouseLeftYawShapingActive));
        OnPropertyChanged(nameof(IsMouseLeftYawOutputShapeActive));
        OnPropertyChanged(nameof(IsMouseLeftYawReturnShapeActive));
    }

    private void ResetDiagnostics(string status)
    {
        DiagnosticStatus = status;
        DiagnosticCommandLine = "";
        DiagnosticOutput = "";
    }

    private void OnDiagnosticCommandLinePropertiesChanged()
    {
        OnPropertyChanged(nameof(MouseDevicesCommandLine));
        OnPropertyChanged(nameof(ShowProfileCommandLine));
        OnPropertyChanged(nameof(MouseLeftDryRunCommandLine));
        OnPropertyChanged(nameof(GimbalPreviewCommandLine));
    }

    private bool CanStartDiagnostic()
    {
        return !IsDiagnosticRunning;
    }

    private void RunMouseDevices()
    {
        if (!TryParseDuration(DeviceScanSeconds, 1, 30, "GameInput enumeration", out var seconds))
        {
            return;
        }

        StartDiagnostic(_diagnosticsService.BuildMouseDevicesGameInput(_repository.Paths, seconds));
    }

    private void RunShowProfile()
    {
        StartDiagnostic(_diagnosticsService.BuildShowProfile(_repository.Paths, _profile.FullPath));
    }

    private void RunMouseLeftDryRun()
    {
        if (!TryParseDuration(MouseLeftDryRunSeconds, 1, 60, "Second mouse dry run", out var seconds))
        {
            return;
        }

        StartDiagnostic(_diagnosticsService.BuildMouseLeftDryRun(_repository.Paths, _profile.FullPath, seconds));
    }

    private void RunGimbalPreview()
    {
        StartDiagnostic(_diagnosticsService.BuildGimbalPreview(_repository.Paths, _profile.FullPath));
    }

    private void StartDiagnostic(Gx12DiagnosticCommand command)
    {
        if (IsDiagnosticRunning)
        {
            return;
        }

        _ = RunDiagnosticAsync(command);
    }

    private async Task RunDiagnosticAsync(Gx12DiagnosticCommand command)
    {
        _diagnosticCancellation?.Dispose();
        _diagnosticCancellation = new CancellationTokenSource();
        IsDiagnosticRunning = true;
        DiagnosticStatus = $"Running {command.Name}...";
        DiagnosticCommandLine = command.CommandLine;
        DiagnosticOutput = "";

        try
        {
            var result = await _diagnosticsService
                .RunAsync(_repository.Paths, command, _diagnosticCancellation.Token);
            DiagnosticStatus = result.IsSuccess ? result.Message : result.Message;
            DiagnosticCommandLine = result.CommandLine;
            DiagnosticOutput = string.IsNullOrWhiteSpace(result.Output)
                ? "(no output)"
                : result.Output.TrimEnd();
            if (command.Name.Equals(
                Gx12DiagnosticsService.MouseDevicesGameInputDiagnosticName,
                StringComparison.OrdinalIgnoreCase))
            {
                UpdateDeviceAssignmentCheck(result);
            }

            PersistDiagnosticResult(command, result);
        }
        finally
        {
            _diagnosticCancellation.Dispose();
            _diagnosticCancellation = null;
            IsDiagnosticRunning = false;
        }
    }

    private void StopDiagnostic()
    {
        if (!IsDiagnosticRunning)
        {
            return;
        }

        DiagnosticStatus = "Stopping diagnostic...";
        _diagnosticCancellation?.Cancel();
    }

    private void CopyCommandLine(object? parameter)
    {
        var commandLine = parameter as string;
        if (string.IsNullOrWhiteSpace(commandLine))
        {
            return;
        }

        Clipboard.SetText(commandLine);
        DiagnosticStatus = "Copied command line.";
    }

    private void CopyDiagnosticOutput()
    {
        if (string.IsNullOrWhiteSpace(DiagnosticText))
        {
            return;
        }

        Clipboard.SetText(DiagnosticText);
        DiagnosticStatus = "Copied diagnostic output.";
    }

    private void CopyDiagnosticLogPath()
    {
        if (string.IsNullOrWhiteSpace(LatestDiagnosticLogPath))
        {
            return;
        }

        Clipboard.SetText(LatestDiagnosticLogPath);
        DiagnosticStatus = "Copied diagnostic log path.";
    }

    private void ChooseTooltipImage()
    {
        if (SelectedCatalogRow is null)
        {
            TooltipImageStatus = "Select a catalog setting first.";
            return;
        }

        var settingId = SelectedCatalogRow.Definition.Id;
        var initialDirectory = SelectedCatalogRow.HasTooltipImage
            ? Path.GetDirectoryName(SelectedCatalogRow.TooltipImagePath) ?? _repository.Paths.RepoRoot
            : _repository.Paths.RepoRoot;
        var selectedPath = _tooltipImagePicker.PickTooltipImage(initialDirectory);
        if (string.IsNullOrWhiteSpace(selectedPath))
        {
            return;
        }

        try
        {
            var label = SelectedCatalogRow.Label;
            _uiSettingsService.ImportTooltipImage(_repository.Paths, settingId, selectedPath);
            _uiSettings = _uiSettingsService.Load(_repository.Paths);
            RefreshCatalog(TomlProfileDocument.Load(_profile.FullPath), settingId);
            TooltipImageStatus = $"Attached setting tooltip image to {label}.";
            OnTooltipSettingsPropertiesChanged();
        }
        catch (Exception exception)
        {
            TooltipImageStatus = $"Tooltip image failed: {exception.Message}";
        }
    }

    private void ClearTooltipImage()
    {
        if (SelectedCatalogRow is null)
        {
            TooltipImageStatus = "Select a catalog setting first.";
            return;
        }

        var settingId = SelectedCatalogRow.Definition.Id;
        var label = SelectedCatalogRow.Label;
        try
        {
            _uiSettingsService.ClearTooltipImage(_repository.Paths, settingId);
            _uiSettings = _uiSettingsService.Load(_repository.Paths);
            RefreshCatalog(TomlProfileDocument.Load(_profile.FullPath), settingId);
            TooltipImageStatus = $"Cleared tooltip image for {label}.";
            OnTooltipSettingsPropertiesChanged();
        }
        catch (Exception exception)
        {
            TooltipImageStatus = $"Clear tooltip image failed: {exception.Message}";
        }
    }

    private void CopyTooltipImagePath()
    {
        if (SelectedCatalogRow is null || string.IsNullOrWhiteSpace(SelectedCatalogRow.TooltipImageFullPath))
        {
            return;
        }

        Clipboard.SetText(SelectedCatalogRow.TooltipImageFullPath);
        TooltipImageStatus = "Copied setting tooltip image path.";
    }

    private void CopyTooltipSpriteDirectory()
    {
        var directory = TooltipSpriteDirectory;
        Directory.CreateDirectory(directory);
        TooltipSpriteService.Shared.Configure(directory);
        Clipboard.SetText(directory);
        TooltipImageStatus = "Copied tooltip sprite folder. Drop PNG files there; tooltips rescan it on hover.";
        OnTooltipSettingsPropertiesChanged();
    }

    private void RefreshTooltipSprites()
    {
        TooltipSpriteService.Shared.Configure(TooltipSpriteDirectory);
        TooltipImageStatus = "Refreshed tooltip sprite count.";
        OnTooltipSettingsPropertiesChanged();
    }

    private void LoadAboveBarSpriteSettings()
    {
        _aboveBarSpriteRandomReturnDelay = _uiSettings.AboveBarSpriteRandomReturnDelay;
        _aboveBarSpriteFixedReturnDelaySeconds =
            _uiSettings.AboveBarSpriteFixedReturnDelaySeconds.ToString(CultureInfo.InvariantCulture);
    }

    private void SaveUiSettings()
    {
        _uiSettingsService.Save(_repository.Paths, _uiSettings);
        _uiSettings = _uiSettingsService.Load(_repository.Paths);
        LoadAboveBarSpriteSettings();
        OnTooltipSettingsPropertiesChanged();
    }

    private static bool TryNormalizeAboveBarSpriteDelay(
        string? value,
        out int delaySeconds,
        out string normalizedText,
        out string status)
    {
        if (!int.TryParse(value?.Trim(), NumberStyles.Integer, CultureInfo.InvariantCulture, out var parsed))
        {
            delaySeconds = UiSettingsService.DefaultAboveBarSpriteFixedReturnDelaySeconds;
            normalizedText = delaySeconds.ToString(CultureInfo.InvariantCulture);
            status = "Asuka sprite fixed return delay must be a whole number of seconds.";
            return false;
        }

        delaySeconds = UiSettingsService.ClampAboveBarSpriteFixedReturnDelaySeconds(parsed);
        normalizedText = delaySeconds.ToString(CultureInfo.InvariantCulture);
        status = parsed == delaySeconds
            ? $"Asuka sprite fixed return delay saved at {delaySeconds} second(s)."
            : $"Asuka sprite fixed return delay clamped to {delaySeconds} second(s).";
        return true;
    }

    private void OnTooltipSettingsPropertiesChanged()
    {
        OnPropertyChanged(nameof(UiSettingsFilePath));
        OnPropertyChanged(nameof(TooltipImageDirectory));
        OnPropertyChanged(nameof(TooltipSpriteDirectory));
        OnPropertyChanged(nameof(TooltipSettingsSummary));
        OnPropertyChanged(nameof(TooltipSpriteSummary));
        OnPropertyChanged(nameof(AboveBarSpriteRandomReturnDelay));
        OnPropertyChanged(nameof(IsAboveBarSpriteFixedReturnDelayEnabled));
        OnPropertyChanged(nameof(AboveBarSpriteFixedReturnDelaySeconds));
        OnPropertyChanged(nameof(AboveBarSpriteFixedReturnDelaySecondsText));
        OnPropertyChanged(nameof(AboveBarSpriteReturnSummary));
    }

    private void RaiseDiagnosticCanExecuteChanged()
    {
        RunMouseDevicesCommand.RaiseCanExecuteChanged();
        RunShowProfileCommand.RaiseCanExecuteChanged();
        RunMouseLeftDryRunCommand.RaiseCanExecuteChanged();
        RunGimbalPreviewCommand.RaiseCanExecuteChanged();
        StopDiagnosticCommand.RaiseCanExecuteChanged();
    }

    private void ResetDeviceAssignmentCheck()
    {
        GameInputDevices.Clear();
        DeviceAssignmentStatuses.Clear();
        DeviceAssignmentSummary = "Run GameInput enumeration to check this profile's mouse assignments.";
    }

    private void UpdateDeviceAssignmentCheck(Gx12DiagnosticResult result)
    {
        var report = GameInputMouseDeviceScan.Analyze(
            _profile.RightDevice,
            _profile.LeftDevice,
            MouseRightEnabled,
            IsMouseLeftSelected,
            MouseLeftRequireDevice,
            result.Output);

        GameInputDevices.Clear();
        foreach (var device in report.Devices)
        {
            GameInputDevices.Add(device);
        }

        DeviceAssignmentStatuses.Clear();
        foreach (var status in report.Statuses)
        {
            DeviceAssignmentStatuses.Add(status);
        }

        DeviceAssignmentSummary = result.IsSuccess
            ? report.Summary
            : $"{result.Message} {report.Summary}";
    }

    private void LoadDiagnosticHistory()
    {
        DiagnosticHistory.Clear();
        foreach (var item in _diagnosticLogStore.LoadRecent(_repository.Paths))
        {
            DiagnosticHistory.Add(item);
        }

        LatestDiagnosticLogPath = DiagnosticHistory.FirstOrDefault()?.LogPath ?? "";
        OnPropertyChanged(nameof(DiagnosticHistorySummary));
    }

    private void PersistDiagnosticResult(Gx12DiagnosticCommand command, Gx12DiagnosticResult result)
    {
        try
        {
            var item = _diagnosticLogStore.Save(_repository.Paths, command, result);
            DiagnosticHistory.Insert(0, item);
            while (DiagnosticHistory.Count > 12)
            {
                DiagnosticHistory.RemoveAt(DiagnosticHistory.Count - 1);
            }

            LatestDiagnosticLogPath = item.LogPath;
            OnPropertyChanged(nameof(DiagnosticHistorySummary));
        }
        catch (Exception exception)
        {
            DiagnosticStatus = $"{DiagnosticStatus} Log write failed: {exception.Message}";
        }
    }

    private void LoadDiagnosticHistoryItem(DiagnosticHistoryItem item)
    {
        if (IsDiagnosticRunning)
        {
            return;
        }

        DiagnosticStatus = $"{item.StatusLabel} {item.Name}: {item.Message}";
        DiagnosticCommandLine = item.CommandLine;
        DiagnosticOutput = _diagnosticLogStore.ReadLog(item).TrimEnd();
        LatestDiagnosticLogPath = item.LogPath;
    }

    private bool TryParseDuration(string text, int min, int max, string label, out int seconds)
    {
        if (int.TryParse(text, System.Globalization.NumberStyles.Integer, System.Globalization.CultureInfo.InvariantCulture, out seconds) &&
            seconds >= min &&
            seconds <= max)
        {
            return true;
        }

        seconds = 0;
        DiagnosticStatus = $"{label} duration must be {min}..{max} seconds.";
        DiagnosticCommandLine = "";
        DiagnosticOutput = "";
        return false;
    }

    private static int GetDurationForCommandLine(string text, int fallback)
    {
        return int.TryParse(text, System.Globalization.NumberStyles.Integer, System.Globalization.CultureInfo.InvariantCulture, out var seconds)
            ? seconds
            : fallback;
    }

    private void RefreshCatalog(TomlProfileDocument document)
    {
        RefreshCatalog(document, SelectedCatalogRow?.Definition.Id);
    }

    private void RefreshCatalog(TomlProfileDocument document, string? selectedSettingId)
    {
        CatalogRows.Clear();
        foreach (var definition in ProfileSchema.Definitions)
        {
            CatalogRows.Add(new SettingCatalogRow(
                definition,
                document,
                _uiSettingsService.GetTooltipImage(_repository.Paths, _uiSettings, definition.Id)));
        }

        RawTomlText = document.ToText();
        RefreshCatalogView();
        SelectedCatalogRow = string.IsNullOrWhiteSpace(selectedSettingId)
            ? CatalogRows.FirstOrDefault()
            : CatalogRows.FirstOrDefault(row => row.Definition.Id.Equals(selectedSettingId, StringComparison.OrdinalIgnoreCase)) ??
              CatalogRows.FirstOrDefault();
    }

    private void RefreshCatalogView()
    {
        CatalogRowsView.Refresh();
        OnPropertyChanged(nameof(CatalogSummary));
    }

    private bool FilterCatalogRow(object item)
    {
        if (item is not SettingCatalogRow row)
        {
            return false;
        }

        var tierAllowed =
            (ShowBasicSettings && row.Definition.Tier == SettingTier.Basic) ||
            (ShowAdvancedSettings && row.Definition.Tier == SettingTier.Advanced) ||
            (ShowExperimentalSettings && row.Definition.Tier == SettingTier.Experimental);
        if (!tierAllowed)
        {
            return false;
        }

        if (ShowChangedSettingsOnly && !row.IsChangedFromDefault)
        {
            return false;
        }

        if (ShowInvalidSettingsOnly && !row.IsInvalid)
        {
            return false;
        }

        return row.MatchesSearch(CatalogSearchText);
    }

    private void JumpToCatalogRow(SettingCatalogRow? row)
    {
        if (row is null)
        {
            return;
        }

        SelectedCatalogRow = row;
    }

    private static string ToTomlBool(bool value)
    {
        return value ? "true" : "false";
    }

    private static string ConvertCatalogEditToRaw(ProfileSettingDefinition definition, string value)
    {
        var trimmed = value.Trim();
        return definition.Kind switch
        {
            SettingKind.Boolean => NormalizeBoolForToml(trimmed),
            SettingKind.String or SettingKind.Enum => TomlProfileDocument.QuoteString(trimmed),
            SettingKind.Array => string.IsNullOrWhiteSpace(trimmed) ? "[]" : trimmed,
            _ => trimmed
        };
    }

    private void ApplyCatalogValueToEditor(ProfileSettingDefinition definition, string editValue)
    {
        var wasLoading = _loading;
        _loading = true;
        try
        {
            switch (definition.TomlPath)
            {
                case "trainer.name":
                    ProfileName = editValue;
                    break;
                case "trainer.frame_rate_hz":
                    FrameRateHz = editValue;
                    break;
                case "trainer.resolution_mode":
                    ResolutionMode = editValue;
                    break;
                case "control.mode":
                    ControlMode = editValue;
                    break;
                case "safety.stop_key":
                    StopKey = editValue;
                    break;
                case "safety.freeze_key":
                    FreezeKey = editValue;
                    break;
                case "mapper.roll_gain":
                    RollGain = editValue;
                    break;
                case "mapper.pitch_gain":
                    PitchGain = editValue;
                    break;
                case "mapper.max_output":
                    MaxOutput = editValue;
                    break;
                case "mapper.deadband":
                    Deadband = editValue;
                    break;
                case "mapper.expo":
                    Expo = editValue;
                    break;
                case "mapper.output_curve":
                    OutputCurve = editValue;
                    break;
                case "mapper.actual_center":
                    ActualCenter = editValue;
                    break;
                case "mapper.actual_max":
                    ActualMax = editValue;
                    break;
                case "mapper.actual_expo":
                    ActualExpo = editValue;
                    break;
                case "mapper.return_enabled":
                    ReturnEnabled = ParseCatalogBool(editValue);
                    break;
                case "mapper.return_rate":
                    ReturnRate = editValue;
                    break;
                case "mapper.return_idle_ms":
                    ReturnIdle = editValue;
                    break;
                case "mapper.constant_return_enabled":
                    ConstantReturnEnabled = ParseCatalogBool(editValue);
                    break;
                case "mapper.constant_return_rate":
                    ConstantReturnRate = editValue;
                    break;
                case "mapper.elastic_return_enabled":
                    ElasticReturnEnabled = ParseCatalogBool(editValue);
                    break;
                case "mapper.elastic_return_mode":
                    ElasticReturnMode = editValue;
                    break;
                case "mapper.elastic_return_coefficient":
                    ElasticReturnCoefficient = editValue;
                    break;
                case "mapper.elastic_return_curve":
                    ElasticReturnCurve = editValue;
                    break;
                case "mapper.return_shaping_enabled":
                    ReturnShapingEnabled = ParseCatalogBool(editValue);
                    break;
                case "mapper.output_shape_nodes":
                    OutputShapeNodesText = editValue;
                    break;
                case "mapper.return_shape_nodes":
                    ReturnShapeNodesText = editValue;
                    break;
                case "mapper.input_filter":
                    InputFilter = editValue;
                    break;
                case "mapper.smoothing":
                    Smoothing = editValue;
                    break;
                case "mapper.one_euro_min_cutoff_hz":
                    OneEuroMinCutoffHz = editValue;
                    break;
                case "mapper.one_euro_beta":
                    OneEuroBeta = editValue;
                    break;
                case "mapper.one_euro_dcutoff_hz":
                    OneEuroDcutoffHz = editValue;
                    break;
                case "mapper.despike_enabled":
                    DespikeEnabled = ParseCatalogBool(editValue);
                    break;
                case "mapper.despike_count_enabled":
                    DespikeCountEnabled = ParseCatalogBool(editValue);
                    break;
                case "mapper.despike_window":
                    DespikeWindow = editValue;
                    break;
                case "mapper.despike_threshold_sigma":
                    DespikeThresholdSigma = editValue;
                    break;
                case "mapper.position_model":
                    PositionModel = editValue;
                    break;
                case "mapper.gimbal_frequency_hz":
                    GimbalFrequencyHz = editValue;
                    break;
                case "mapper.gimbal_damping_ratio":
                    GimbalDampingRatio = editValue;
                    break;
                case "mapper.gimbal_input_impulse":
                    GimbalInputImpulse = editValue;
                    break;
                case "mapper.gimbal_static_friction":
                    GimbalStaticFriction = editValue;
                    break;
                case "mapper.gimbal_dynamic_friction":
                    GimbalDynamicFriction = editValue;
                    break;
                case "mapper.gimbal_edge_bumper":
                    GimbalEdgeBumper = editValue;
                    break;
                case "mapper.gimbal_antiwindup_enabled":
                    GimbalAntiwindupEnabled = ParseCatalogBool(editValue);
                    break;
                case "mapper.gimbal_antiwindup_start":
                    GimbalAntiwindupStart = editValue;
                    break;
                case "mapper.gimbal_antiwindup_min_gain":
                    GimbalAntiwindupMinGain = editValue;
                    break;
                case "mapper.input_gain_mode":
                    InputGainMode = editValue;
                    break;
                case "mapper.adaptive_slow_gain":
                    AdaptiveSlowGain = editValue;
                    break;
                case "mapper.adaptive_fast_gain":
                    AdaptiveFastGain = editValue;
                    break;
                case "mapper.adaptive_speed_low":
                    AdaptiveSpeedLow = editValue;
                    break;
                case "mapper.adaptive_speed_high":
                    AdaptiveSpeedHigh = editValue;
                    break;
                case "mapper.adaptive_curve":
                    AdaptiveCurve = editValue;
                    break;
                case "mapper.adaptive_tracker_ms":
                    AdaptiveTrackerMs = editValue;
                    break;
                case "mapper.gate_shape":
                    GateShape = editValue;
                    break;
                case "mapper.diagonal_scale":
                    DiagonalScale = editValue;
                    break;
                case "mapper.invert_roll":
                    InvertRoll = ParseCatalogBool(editValue);
                    break;
                case "mapper.invert_pitch":
                    InvertPitch = ParseCatalogBool(editValue);
                    break;
                case "mapper.swap_axes":
                    SwapAxes = ParseCatalogBool(editValue);
                    break;
                case "mouse_right_stick.enabled":
                    MouseRightEnabled = ParseCatalogBool(editValue);
                    break;
                case "keyboard_left_stick.input_source":
                    KeyboardInputSource = editValue;
                    break;
                case "keyboard_left_stick.enabled":
                    LeftStickSource = ParseCatalogBool(editValue) ? LeftSourceKeyboard : LeftSourceOff;
                    break;
                case "keyboard_left_stick.require_analog":
                    KeyboardRequireAnalog = ParseCatalogBool(editValue);
                    break;
                case "mouse_left_stick.enabled":
                    LeftStickSource = ParseCatalogBool(editValue) ? LeftSourceMouse : LeftSourceOff;
                    break;
                case "mouse_left_stick.require_device":
                    MouseLeftRequireDevice = ParseCatalogBool(editValue);
                    break;
                case "mouse_left_stick.throttle_rate":
                    MouseLeftThrottleRate = editValue;
                    break;
                case "mouse_left_stick.yaw_gain":
                    MouseLeftYawGain = editValue;
                    break;
                case "mouse_left_stick.yaw_pulse":
                    MouseLeftYawPulse = editValue;
                    break;
                case "mouse_left_stick.yaw_deadband":
                    MouseLeftYawDeadband = editValue;
                    break;
                case "mouse_left_stick.invert_throttle":
                    MouseLeftInvertThrottle = ParseCatalogBool(editValue);
                    break;
                case "mouse_left_stick.invert_yaw":
                    MouseLeftInvertYaw = ParseCatalogBool(editValue);
                    break;
                case "mouse_left_stick.swap_axes":
                    MouseLeftSwapAxes = ParseCatalogBool(editValue);
                    break;
                case "mouse_left_stick.yaw_shaping_enabled":
                case "mouse_left_stick.yaw_mapper_shaping_enabled":
                    MouseLeftYawShapingEnabled = ParseCatalogBool(editValue);
                    break;
                case "mouse_left_stick.yaw_output_curve":
                    MouseLeftYawOutputCurve = editValue;
                    break;
                case "mouse_left_stick.yaw_output_shaping_enabled":
                    MouseLeftYawOutputShapingEnabled = ParseCatalogBool(editValue);
                    break;
                case "mouse_left_stick.yaw_return_shaping_enabled":
                    MouseLeftYawReturnShapingEnabled = ParseCatalogBool(editValue);
                    break;
                case "mouse_left_stick.yaw_output_shape_nodes":
                    MouseLeftYawOutputShapeNodesText = editValue;
                    break;
                case "mouse_left_stick.yaw_return_shape_nodes":
                    MouseLeftYawReturnShapeNodesText = editValue;
                    break;
                case "mouse_aim.sensitivity_x":
                    AimSensitivityX = editValue;
                    break;
                case "mouse_aim.sensitivity_y":
                    AimSensitivityY = editValue;
                    break;
                case "mouse_aim.reticle_limit":
                    AimReticleLimit = editValue;
                    break;
                case "mouse_aim.reticle_deadband":
                    AimReticleDeadband = editValue;
                    break;
                case "mouse_aim.reticle_return_rate":
                    AimReturnRate = editValue;
                    break;
                case "mouse_aim.output_smoothing":
                    AimOutputSmoothing = editValue;
                    break;
                case "mouse_aim.roll_gain":
                    AimRollGain = editValue;
                    break;
                case "mouse_aim.yaw_gain":
                    AimYawGain = editValue;
                    break;
                case "mouse_aim.pitch_gain":
                    AimPitchGain = editValue;
                    break;
                case "mouse_aim.roll_max":
                    AimRollMax = editValue;
                    break;
                case "mouse_aim.yaw_max":
                    AimYawMax = editValue;
                    break;
                case "mouse_aim.pitch_max":
                    AimPitchMax = editValue;
                    break;
                case "mouse_aim.slew_rate":
                    AimSlewRate = editValue;
                    break;
                case "mouse_aim.invert_x":
                    AimInvertX = ParseCatalogBool(editValue);
                    break;
                case "mouse_aim.invert_y":
                    AimInvertY = ParseCatalogBool(editValue);
                    break;
            }
        }
        finally
        {
            _loading = wasLoading;
        }
    }

    private static string FormatCatalogRawForEdit(ProfileSettingDefinition definition, string rawValue)
    {
        return definition.Kind is SettingKind.String or SettingKind.Enum
            ? UnquoteCatalogValue(rawValue)
            : rawValue.Trim();
    }

    private static bool ParseCatalogBool(string value)
    {
        return NormalizeBoolForToml(value).Equals("true", StringComparison.OrdinalIgnoreCase);
    }

    private static string UnquoteCatalogValue(string value)
    {
        var trimmed = value.Trim();
        if (trimmed.Length >= 2 &&
            trimmed.StartsWith("\"", StringComparison.Ordinal) &&
            trimmed.EndsWith("\"", StringComparison.Ordinal))
        {
            return trimmed[1..^1]
                .Replace("\\\"", "\"", StringComparison.Ordinal)
                .Replace("\\\\", "\\", StringComparison.Ordinal);
        }

        return trimmed;
    }

    private static string NormalizeBoolForToml(string value)
    {
        var normalized = value.Trim().Trim('"').ToLowerInvariant();
        if (normalized is "true" or "1" or "yes" or "on")
        {
            return "true";
        }

        if (normalized is "false" or "0" or "no" or "off")
        {
            return "false";
        }

        return normalized;
    }
}
