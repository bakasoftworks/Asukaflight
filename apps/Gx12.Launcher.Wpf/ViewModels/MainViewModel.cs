using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Data;
using System.Windows.Input;
using Gx12.Launcher.Wpf.Models;
using Gx12.Launcher.Wpf.Services;

namespace Gx12.Launcher.Wpf.ViewModels;

public sealed class MainViewModel : ObservableObject
{
    private readonly ProfileRepository _repository;
    private readonly IProfileFolderPicker _folderPicker;
    private readonly UiSettingsService _uiSettingsService;
    private readonly ITooltipImagePicker _tooltipImagePicker;
    private readonly IRecordingFilePicker _recordingFilePicker;
    private readonly Gx12RuntimeService _runtimeService;
    private readonly Gx12DiagnosticsService _diagnosticsService;
    private AppPaths _paths;
    private ReleaseInfo _releaseInfo;
    private ProfileSummary? _selectedProfile;
    private ProfileEditorViewModel? _editor;
    private string _searchText = string.Empty;
    private string _statusText = string.Empty;
    private string _runtimeStatusText = "Runtime ready. Select a profile and start the composite trainer.";
    private string _recordingPath = string.Empty;
    private string _recordingDurationSeconds = "30";
    private string _recordingToggleKey = "F4";
    private string _recordingStatusText = "Recording ready.";
    private string _recordingInfoText = string.Empty;
    private string _playbackRecordingPath = string.Empty;
    private string _playbackPort = "auto";
    private string _playbackTrigger = "F5";
    private bool _isRuntimeBusy;
    private bool _isRunActive;
    private bool _isRecordingInfoBusy;
    private bool _recordingLiveReload = true;
    private bool _recordingOverwrite;
    private bool _recordingPathEditedByUser;
    private bool _playbackPathEditedByUser;
    private bool _playbackAileron = true;
    private bool _playbackElevator = true;
    private bool _playbackThrottle;
    private bool _playbackRudder;
    private bool _playbackRadioRightGimbal;
    private bool _playbackRecordedTrainerRight;
    private bool _playbackRadioLeftGimbal = true;
    private bool _playbackRecordedTrainerLeft;
    private bool _playbackBlockLiveInput;
    private bool _playbackLoop;
    private bool _suppressRecordingPathSave;
    private bool _suppressPlaybackChannelSave;
    private bool _suppressPlaybackBindingSave;

    public MainViewModel(ProfileRepository repository, IProfileFolderPicker folderPicker)
        : this(
            repository,
            folderPicker,
            new UiSettingsService(),
            new TooltipImagePicker(),
            new Gx12RuntimeService(),
            new Gx12DiagnosticsService(),
            new RecordingFilePicker())
    {
    }

    public MainViewModel(
        ProfileRepository repository,
        IProfileFolderPicker folderPicker,
        UiSettingsService uiSettingsService,
        ITooltipImagePicker tooltipImagePicker,
        Gx12RuntimeService runtimeService,
        Gx12DiagnosticsService? diagnosticsService = null,
        IRecordingFilePicker? recordingFilePicker = null)
    {
        _repository = repository;
        _folderPicker = folderPicker;
        _uiSettingsService = uiSettingsService;
        _tooltipImagePicker = tooltipImagePicker;
        _recordingFilePicker = recordingFilePicker ?? new RecordingFilePicker();
        _runtimeService = runtimeService;
        _diagnosticsService = diagnosticsService ?? new Gx12DiagnosticsService();
        _paths = repository.Paths;
        _releaseInfo = ReleaseInfoService.Create(_paths);
        TooltipSpriteService.Shared.Configure(_uiSettingsService.GetTooltipSpriteDirectory(_paths), ensureDirectory: true);
        Profiles = new ObservableCollection<ProfileSummary>();
        PlaybackBindings = new ObservableCollection<PlaybackBindingViewModel>();
        ProfilesView = CollectionViewSource.GetDefaultView(Profiles);
        ProfilesView.Filter = FilterProfile;
        RefreshCommand = new RelayCommand(RefreshProfiles);
        ClearSearchCommand = new RelayCommand(() => SearchText = string.Empty, () => !string.IsNullOrWhiteSpace(SearchText));
        CopyPathCommand = new RelayCommand(CopySelectedProfilePath, () => SelectedProfile is not null);
        CloneProfileCommand = new RelayCommand(CloneSelectedProfile, () => SelectedProfile is not null);
        SetDefaultCommand = new RelayCommand(SetSelectedProfileAsDefault, () => SelectedProfile is not null);
        ChangeFolderCommand = new RelayCommand(ChangeProfileFolder);
        CopyPublishCommandCommand = new RelayCommand(CopyPublishCommandLine);
        CopyWpfLauncherPathCommand = new RelayCommand(CopyWpfLauncherPath);
        CopyReleaseSummaryCommand = new RelayCommand(CopyReleaseSummary);
        CopyTrainerCommandLineCommand = new RelayCommand(CopyTrainerCommandLine, () => SelectedProfile is not null);
        CopyConsoleScriptPathCommand = new RelayCommand(CopyConsoleScriptPath);
        StartCompositeTrainerCommand = new RelayCommand(() => _ = StartCompositeTrainerAsync(), CanStartCompositeTrainer);
        StopRunCommand = new RelayCommand(() => _ = StopRunAsync(), CanStopRun);
        StartTrainerRecordingCommand = new RelayCommand(() => _ = StartTrainerRecordingAsync(), CanStartTrainerRecording);
        StartTrainerPlaybackCommand = new RelayCommand(() => _ = StartTrainerPlaybackAsync(), CanStartTrainerPlayback);
        StartTrainerPlaybackBankCommand = new RelayCommand(() => _ = StartTrainerPlaybackBankAsync(), CanStartTrainerPlaybackBank);
        InspectRecordingCommand = new RelayCommand(() => _ = InspectRecordingAsync(), CanInspectRecording);
        ChooseRecordingPathCommand = new RelayCommand(ChooseRecordingPath);
        ChoosePlaybackPathCommand = new RelayCommand(ChoosePlaybackPath);
        ChoosePlaybackBindingPathCommand = new RelayCommand(ChoosePlaybackBindingPath, parameter => parameter is PlaybackBindingViewModel);
        CopyRecordingCommandLineCommand = new RelayCommand(CopyRecordingCommandLine, CanCopyRecordingCommandLine);
        CopyPlaybackCommandLineCommand = new RelayCommand(CopyPlaybackCommandLine, CanCopyPlaybackCommandLine);
        CopyPlaybackBankCommandLineCommand = new RelayCommand(CopyPlaybackBankCommandLine, CanCopyPlaybackBankCommandLine);
        CopyRecordingInfoCommandLineCommand = new RelayCommand(CopyRecordingInfoCommandLine, CanCopyRecordingInfoCommandLine);

        LoadRecordingPathFromSettings();
        LoadPlaybackChannelsFromSettings();
        LoadPlaybackBindingsFromSettings();
        RefreshProfiles();
    }

    public ObservableCollection<ProfileSummary> Profiles { get; }

    public ObservableCollection<PlaybackBindingViewModel> PlaybackBindings { get; }

    public ICollectionView ProfilesView { get; }

    public ICommand RefreshCommand { get; }

    public RelayCommand ClearSearchCommand { get; }

    public RelayCommand CopyPathCommand { get; }

    public RelayCommand CloneProfileCommand { get; }

    public RelayCommand SetDefaultCommand { get; }

    public RelayCommand ChangeFolderCommand { get; }

    public RelayCommand CopyPublishCommandCommand { get; }

    public RelayCommand CopyWpfLauncherPathCommand { get; }

    public RelayCommand CopyReleaseSummaryCommand { get; }

    public RelayCommand CopyTrainerCommandLineCommand { get; }

    public RelayCommand CopyConsoleScriptPathCommand { get; }

    public RelayCommand StartCompositeTrainerCommand { get; }

    public RelayCommand StopRunCommand { get; }

    public RelayCommand StartTrainerRecordingCommand { get; }

    public RelayCommand StartTrainerPlaybackCommand { get; }

    public RelayCommand StartTrainerPlaybackBankCommand { get; }

    public RelayCommand InspectRecordingCommand { get; }

    public RelayCommand ChooseRecordingPathCommand { get; }

    public RelayCommand ChoosePlaybackPathCommand { get; }

    public RelayCommand ChoosePlaybackBindingPathCommand { get; }

    public RelayCommand CopyRecordingCommandLineCommand { get; }

    public RelayCommand CopyPlaybackCommandLineCommand { get; }

    public RelayCommand CopyPlaybackBankCommandLineCommand { get; }

    public RelayCommand CopyRecordingInfoCommandLineCommand { get; }

    public bool RuntimeActionsEnabled => !IsRuntimeBusy;

    public bool IsRuntimeBusy
    {
        get => _isRuntimeBusy;
        private set
        {
            if (SetProperty(ref _isRuntimeBusy, value))
            {
                OnPropertyChanged(nameof(RuntimeActionsEnabled));
                OnPropertyChanged(nameof(RuntimeStateLabel));
                OnPropertyChanged(nameof(RuntimeStateKind));
                OnPropertyChanged(nameof(RuntimeStateDetail));
                OnPropertyChanged(nameof(StartTrainerButtonText));
                OnPropertyChanged(nameof(StopRunButtonText));
                RaiseRuntimeCommandStates();
                RaiseRecordingCommandStates();
            }
        }
    }

    public bool IsRunActive
    {
        get => _isRunActive;
        private set
        {
            if (SetProperty(ref _isRunActive, value))
            {
                OnPropertyChanged(nameof(RuntimeStateLabel));
                OnPropertyChanged(nameof(RuntimeStateKind));
                OnPropertyChanged(nameof(RuntimeStateDetail));
                OnPropertyChanged(nameof(StartTrainerButtonText));
                OnPropertyChanged(nameof(StopRunButtonText));
            }
        }
    }

    public string RuntimeStatusText
    {
        get => _runtimeStatusText;
        private set => SetProperty(ref _runtimeStatusText, value);
    }

    public bool IsRecordingInfoBusy
    {
        get => _isRecordingInfoBusy;
        private set
        {
            if (SetProperty(ref _isRecordingInfoBusy, value))
            {
                OnPropertyChanged(nameof(RecordingInfoButtonText));
                RaiseRecordingCommandStates();
            }
        }
    }

    public string RecordingPath
    {
        get => _recordingPath;
        set => SetRecordingPath(value, userEdited: true);
    }

    public string RecordingDurationSeconds
    {
        get => _recordingDurationSeconds;
        set
        {
            if (SetProperty(ref _recordingDurationSeconds, value))
            {
                RaiseRecordingCommandStates();
            }
        }
    }

    public bool RecordingLiveReload
    {
        get => _recordingLiveReload;
        set
        {
            if (SetProperty(ref _recordingLiveReload, value))
            {
                RaiseRecordingCommandStates();
            }
        }
    }

    public bool RecordingOverwrite
    {
        get => _recordingOverwrite;
        set
        {
            if (SetProperty(ref _recordingOverwrite, value))
            {
                SaveRecordingSettingsToSettings();
                RaiseRecordingCommandStates();
            }
        }
    }

    public string RecordingToggleKey
    {
        get => _recordingToggleKey;
        set
        {
            if (SetProperty(ref _recordingToggleKey, value ?? string.Empty))
            {
                RaiseRecordingCommandStates();
            }
        }
    }

    public string PlaybackRecordingPath
    {
        get => _playbackRecordingPath;
        set => SetPlaybackRecordingPath(value, userEdited: true);
    }

    public string PlaybackPort
    {
        get => _playbackPort;
        set
        {
            if (SetProperty(ref _playbackPort, value))
            {
                RaiseRecordingCommandStates();
            }
        }
    }

    public bool PlaybackAileron
    {
        get => _playbackAileron;
        set => SetPlaybackChannel(ref _playbackAileron, value, nameof(PlaybackAileron));
    }

    public bool PlaybackElevator
    {
        get => _playbackElevator;
        set => SetPlaybackChannel(ref _playbackElevator, value, nameof(PlaybackElevator));
    }

    public bool PlaybackThrottle
    {
        get => _playbackThrottle;
        set => SetPlaybackChannel(ref _playbackThrottle, value, nameof(PlaybackThrottle));
    }

    public bool PlaybackRudder
    {
        get => _playbackRudder;
        set => SetPlaybackChannel(ref _playbackRudder, value, nameof(PlaybackRudder));
    }

    public bool PlaybackRadioRightGimbal
    {
        get => _playbackRadioRightGimbal;
        set => SetPlaybackRightSource(ref _playbackRadioRightGimbal, value, nameof(PlaybackRadioRightGimbal));
    }

    public bool PlaybackRecordedTrainerRight
    {
        get => _playbackRecordedTrainerRight;
        set => SetPlaybackRightSource(ref _playbackRecordedTrainerRight, value, nameof(PlaybackRecordedTrainerRight));
    }

    public bool PlaybackRadioLeftGimbal
    {
        get => _playbackRadioLeftGimbal;
        set => SetPlaybackLeftSource(ref _playbackRadioLeftGimbal, value, nameof(PlaybackRadioLeftGimbal));
    }

    public bool PlaybackRecordedTrainerLeft
    {
        get => _playbackRecordedTrainerLeft;
        set => SetPlaybackLeftSource(ref _playbackRecordedTrainerLeft, value, nameof(PlaybackRecordedTrainerLeft));
    }

    public bool PlaybackBlockLiveInput
    {
        get => _playbackBlockLiveInput;
        set => SetPlaybackChannel(ref _playbackBlockLiveInput, value, nameof(PlaybackBlockLiveInput));
    }

    public bool PlaybackLoop
    {
        get => _playbackLoop;
        set
        {
            if (SetProperty(ref _playbackLoop, value))
            {
                RaiseRecordingCommandStates();
            }
        }
    }

    public string PlaybackTrigger
    {
        get => _playbackTrigger;
        set
        {
            if (SetProperty(ref _playbackTrigger, value))
            {
                RaiseRecordingCommandStates();
            }
        }
    }

    public string RecordingStatusText
    {
        get => _recordingStatusText;
        private set => SetProperty(ref _recordingStatusText, value);
    }

    public string RecordingInfoText
    {
        get => _recordingInfoText;
        private set => SetProperty(ref _recordingInfoText, value);
    }

    public string RecordingCommandLine
    {
        get
        {
            if (SelectedProfile is null ||
                string.IsNullOrWhiteSpace(RecordingPath) ||
                !TryGetRecordingDuration(out var durationSeconds))
            {
                return "";
            }

            return Gx12RuntimeService.BuildTrainerRecordCommandLine(
                _paths,
                SelectedProfile.FullPath,
                RecordingPath,
                durationSeconds,
                RecordingLiveReload,
                RecordingToggleKey,
                RecordingOverwrite,
                Gx12RuntimeService.GetRuntimeControlPath(_paths));
        }
    }

    public string PlaybackCommandLine
    {
        get
        {
            var singleBind = BuildSinglePlaybackBindCommand();
            if (SelectedProfile is null || singleBind is null)
            {
                return "";
            }

            return Gx12RuntimeService.BuildTrainerCommandLine(
                _paths,
                SelectedProfile.FullPath,
                RecordingPath,
                TryGetRecordingDuration(out var durationSeconds) ? durationSeconds : 0,
                RecordingLiveReload,
                RecordingToggleKey,
                PlaybackLoop,
                new[] { singleBind },
                RecordingOverwrite,
                Gx12RuntimeService.GetRuntimeControlPath(_paths));
        }
    }

    public string PlaybackBankCommandLine
    {
        get
        {
            var bindings = BuildPlaybackBindCommands();
            return bindings.Count == 0
                ? ""
                : Gx12RuntimeService.BuildTrainerPlaybackBankCommandLine(
                    _paths,
                    PlaybackPort,
                    PlaybackLoop,
                    bindings);
        }
    }

    public string RecordingInfoCommandLine => string.IsNullOrWhiteSpace(RecordingPath)
        ? ""
        : Gx12RuntimeService.BuildRecordingInfoCommandLine(_paths, RecordingPath);

    public string RecordingBufferStatusText
    {
        get
        {
            var target = string.IsNullOrWhiteSpace(RecordingPath)
                ? "selected file"
                : Path.GetFileName(RecordingPath.Trim());
            return $"Buffer: memory -> background CSV save ({target})";
        }
    }

    public string PlaybackChannelMask
    {
        get
        {
            var channels = new[]
                {
                    PlaybackAileron ? PlaybackRightChannel("ail") : "",
                    PlaybackElevator ? PlaybackRightChannel("ele") : "",
                    PlaybackThrottle ? PlaybackLeftChannel("thr") : "",
                    PlaybackRudder ? PlaybackLeftChannel("rud") : ""
                }
                .Where(channel => !string.IsNullOrWhiteSpace(channel));
            return string.Join(",", channels);
        }
    }

    public string RecordingConsoleScriptPath => Gx12RuntimeService.GetRecordingConsoleScriptPath(_paths);

    public string PlaybackConsoleScriptPath => Gx12RuntimeService.GetPlaybackConsoleScriptPath(_paths);

    public string PlaybackBankConsoleScriptPath => Gx12RuntimeService.GetPlaybackBankConsoleScriptPath(_paths);

    public string RecordingInfoButtonText => IsRecordingInfoBusy ? "Reading..." : "Info";

    public bool HasPlaybackBindings => PlaybackBindings.Count > 0;

    public bool CanAddPlaybackBinding => PlaybackBindings.Count < UiSettingsService.MaxPlaybackBindingSlots;

    public string PlaybackBindingLimitText =>
        $"{PlaybackBindings.Count}/{UiSettingsService.MaxPlaybackBindingSlots} binds";

    public bool AddPlaybackBindingToggle
    {
        get => false;
        set
        {
            if (value)
            {
                AddPlaybackBinding();
                OnPropertyChanged(nameof(AddPlaybackBindingToggle));
            }
        }
    }

    public ReleaseInfo ReleaseInfo
    {
        get => _releaseInfo;
        private set => SetProperty(ref _releaseInfo, value);
    }

    public string RepoRoot => _paths.RepoRoot;

    public string ProfileDirectory => _paths.ProfileDirectory;

    public string DefaultProfileFileName => _paths.DefaultProfileFileName;

    public string ExePath => _paths.ExePath;

    public string ExeStatus => File.Exists(_paths.ExePath) ? "gx12mouse.exe found" : "gx12mouse.exe missing";

    public string ConsoleScriptPath => Gx12RuntimeService.GetConsoleScriptPath(_paths);

    public string? AboveBarSpritePath
    {
        get
        {
            var spritePath = _uiSettingsService.GetAboveBarSpritePath(_paths);
            return File.Exists(spritePath) ? spritePath : null;
        }
    }

    public bool HasAboveBarSprite => AboveBarSpritePath is not null;

    public string RuntimeStateLabel
    {
        get
        {
            if (IsRuntimeBusy)
            {
                return "Working";
            }

            return IsRunActive ? "Run active" : "Ready";
        }
    }

    public string RuntimeStateKind
    {
        get
        {
            if (IsRuntimeBusy)
            {
                return "Warn";
            }

            return IsRunActive ? "Accent" : "Neutral";
        }
    }

    public string RuntimeStateDetail
    {
        get
        {
            if (IsRuntimeBusy)
            {
                return "Starting or stopping the managed trainer run.";
            }

            return IsRunActive
                ? "Recording toggles and playback binds stay inside this run."
                : "Ready for the selected profile, start/stop key, and recording hotkeys.";
        }
    }

    public string StartTrainerButtonText => IsRuntimeBusy
        ? "Working..."
        : IsRunActive
            ? "Restart composite trainer"
            : "Start composite trainer";

    public string StopRunButtonText => IsRunActive ? "Stop active run" : "Send stop signal";

    public string SearchText
    {
        get => _searchText;
        set
        {
            if (SetProperty(ref _searchText, value))
            {
                ProfilesView.Refresh();
                ClearSearchCommand.RaiseCanExecuteChanged();
                OnPropertyChanged(nameof(ProfileCountSummary));
            }
        }
    }

    public ProfileSummary? SelectedProfile
    {
        get => _selectedProfile;
        set
        {
            if (value is null && ShouldKeepCurrentProfileSelection())
            {
                OnPropertyChanged(nameof(SelectedProfile));
                return;
            }

            if (value is not null && IsSameProfile(_selectedProfile, value) && _editor is not null)
            {
                if (SetProperty(ref _selectedProfile, value))
                {
                    RaiseSelectedProfileChanged();
                }

                return;
            }

            if (SetProperty(ref _selectedProfile, value))
            {
                if (_editor is not null)
                {
                    _editor.ProfileSaved -= OnEditorProfileSaved;
                }

                Editor = value is null
                    ? null
                    : new ProfileEditorViewModel(
                        _repository,
                        value,
                        _uiSettingsService,
                        _tooltipImagePicker);
                if (_editor is not null)
                {
                    _editor.ProfileSaved += OnEditorProfileSaved;
                }

                RaiseSelectedProfileChanged();
            }
        }
    }

    public ProfileEditorViewModel? Editor
    {
        get => _editor;
        private set => SetProperty(ref _editor, value);
    }

    public string StatusText
    {
        get => _statusText;
        private set => SetProperty(ref _statusText, value);
    }

    public string ProfileCountSummary
    {
        get
        {
            var visibleCount = ProfilesView.Cast<object>().Count();
            return string.IsNullOrWhiteSpace(SearchText)
                ? $"{Profiles.Count} profiles"
                : $"{visibleCount} of {Profiles.Count} profiles";
        }
    }

    public string SelectedProfilePath => SelectedProfile?.FullPath ?? "";

    public string SelectedProfileHeader => SelectedProfile?.DisplayName ?? "No profile selected";

    public string SelectedProfileSubheader => SelectedProfile is null
        ? ""
        : $"{SelectedProfile.FileName} / {SelectedProfile.FrameRateHz} Hz / {SelectedProfile.SafetySummary}";

    public string TrainerCommandLine => SelectedProfile is null
        ? ""
        : Gx12RuntimeService.BuildTrainerCommandLine(
            _paths,
            SelectedProfile.FullPath,
            RecordingPath,
            TryGetRecordingDuration(out var durationSeconds) ? durationSeconds : 0,
            RecordingLiveReload,
            RecordingToggleKey,
            PlaybackLoop,
            BuildPlaybackBindCommands(),
            RecordingOverwrite,
            Gx12RuntimeService.GetRuntimeControlPath(_paths));

    public void RefreshProfiles()
    {
        RefreshProfiles(null);
    }

    private void RefreshProfiles(string? targetFileName)
    {
        var previousFileName = targetFileName ?? SelectedProfile?.FileName;
        _paths = _repository.Paths;
        ReleaseInfo = ReleaseInfoService.Create(_paths);

        Profiles.Clear();
        foreach (var profile in _repository.LoadProfiles(_paths))
        {
            Profiles.Add(profile);
        }

        ProfilesView.Refresh();
        SelectedProfile =
            Profiles.FirstOrDefault(profile => profile.FileName.Equals(previousFileName, StringComparison.OrdinalIgnoreCase)) ??
            Profiles.FirstOrDefault(profile => profile.IsDefault) ??
            Profiles.FirstOrDefault();

        OnPropertyChanged(nameof(RepoRoot));
        OnPropertyChanged(nameof(ProfileDirectory));
        OnPropertyChanged(nameof(DefaultProfileFileName));
        OnPropertyChanged(nameof(ExePath));
        OnPropertyChanged(nameof(ExeStatus));
        OnPropertyChanged(nameof(ConsoleScriptPath));
        OnPropertyChanged(nameof(RecordingConsoleScriptPath));
        OnPropertyChanged(nameof(PlaybackConsoleScriptPath));
        OnPropertyChanged(nameof(PlaybackBankConsoleScriptPath));
        OnPropertyChanged(nameof(AboveBarSpritePath));
        OnPropertyChanged(nameof(HasAboveBarSprite));
        OnPropertyChanged(nameof(ProfileCountSummary));
        CopyTrainerCommandLineCommand.RaiseCanExecuteChanged();
        StartCompositeTrainerCommand.RaiseCanExecuteChanged();
        RaiseRecordingCommandStates();

        StatusText = Directory.Exists(_paths.ProfileDirectory)
            ? $"Loaded {Profiles.Count} profile(s) from {Path.GetFileName(_paths.ProfileDirectory)}."
            : $"Profile folder not found: {_paths.ProfileDirectory}";
    }

    private bool FilterProfile(object item)
    {
        if (item is not ProfileSummary profile || string.IsNullOrWhiteSpace(SearchText))
        {
            return true;
        }

        return profile.SearchText.Contains(SearchText, StringComparison.OrdinalIgnoreCase);
    }

    private void CopySelectedProfilePath()
    {
        if (SelectedProfile is null)
        {
            return;
        }

        Clipboard.SetText(SelectedProfile.FullPath);
        StatusText = $"Copied {SelectedProfile.FileName} path.";
    }

    private void CloneSelectedProfile()
    {
        if (SelectedProfile is null)
        {
            return;
        }

        try
        {
            var sourceFileName = SelectedProfile.FileName;
            var newPath = _repository.CloneProfile(SelectedProfile);
            SearchText = string.Empty;
            RefreshProfiles(Path.GetFileName(newPath));
            StatusText = $"Created {Path.GetFileName(newPath)} from {sourceFileName}.";
        }
        catch (Exception exception)
        {
            StatusText = $"Clone failed: {exception.Message}";
        }
    }

    private void SetSelectedProfileAsDefault()
    {
        if (SelectedProfile is null)
        {
            return;
        }

        try
        {
            var selectedFileName = SelectedProfile.FileName;
            _repository.SetDefaultProfile(SelectedProfile);
            RefreshProfiles(selectedFileName);
            StatusText = $"Default profile is now {selectedFileName}.";
        }
        catch (Exception exception)
        {
            StatusText = $"Set default failed: {exception.Message}";
        }
    }

    private void ChangeProfileFolder()
    {
        var selected = _folderPicker.PickFolder(_paths.ProfileDirectory, _paths.RepoRoot);
        if (string.IsNullOrWhiteSpace(selected))
        {
            return;
        }

        try
        {
            _repository.ChangeProfileDirectory(selected);
            SearchText = string.Empty;
            RefreshProfiles(null);
            StatusText = $"Profile folder set to {_paths.ProfileDirectory}.";
        }
        catch (Exception exception)
        {
            StatusText = $"Change folder failed: {exception.Message}";
        }
    }

    private void CopyPublishCommandLine()
    {
        Clipboard.SetText(ReleaseInfo.PublishCommand);
        StatusText = "Copied release package command.";
    }

    private void CopyWpfLauncherPath()
    {
        Clipboard.SetText(ReleaseInfo.WpfLauncherPath);
        StatusText = $"Copied {ReleaseInfo.WpfLauncherFileName} release path.";
    }

    private void CopyReleaseSummary()
    {
        Clipboard.SetText(
            $"{ReleaseInfo.DisplayVersion}{Environment.NewLine}" +
            $"{ReleaseInfo.ShortStatus}{Environment.NewLine}" +
            $"{ReleaseInfo.ReadinessSummary}{Environment.NewLine}" +
            $"Primary launcher: {ReleaseInfo.PrimaryLauncherName}{Environment.NewLine}" +
            $"Release exe: {ReleaseInfo.WpfLauncherPath}{Environment.NewLine}" +
            $"Package: {ReleaseInfo.PublishCommand}{Environment.NewLine}" +
            string.Join(
                Environment.NewLine,
                ReleaseInfo.ReadinessChecks.Select(check => $"{check.Label}: {check.Status} - {check.Detail}")) +
            Environment.NewLine +
            ReleaseInfo.ParityGateStatus);
        StatusText = "Copied WPF release summary.";
    }

    private void ChooseRecordingPath()
    {
        var selected = _recordingFilePicker.PickRecordingOutput(RecordingPath, _paths.RepoRoot);
        if (string.IsNullOrWhiteSpace(selected))
        {
            return;
        }

        RecordingPath = selected;
        StatusText = $"Recording file set to {Path.GetFileName(selected)}.";
    }

    private void ChoosePlaybackPath()
    {
        var selected = _recordingFilePicker.PickPlaybackRecording(PlaybackRecordingPath, _paths.RepoRoot);
        if (string.IsNullOrWhiteSpace(selected))
        {
            return;
        }

        PlaybackRecordingPath = selected;
        StatusText = $"Playback file set to {Path.GetFileName(selected)}.";
    }

    private void ChoosePlaybackBindingPath(object? parameter)
    {
        if (parameter is not PlaybackBindingViewModel binding)
        {
            return;
        }

        var currentPath = string.IsNullOrWhiteSpace(binding.RecordingPath)
            ? PlaybackRecordingPath
            : binding.RecordingPath;
        var selected = _recordingFilePicker.PickPlaybackRecording(currentPath, _paths.RepoRoot);
        if (string.IsNullOrWhiteSpace(selected))
        {
            return;
        }

        binding.RecordingPath = selected;
        StatusText = $"Bind {binding.SlotNumber} playback file set to {Path.GetFileName(selected)}.";
    }

    private void CopyTrainerCommandLine()
    {
        if (SelectedProfile is null)
        {
            return;
        }

        Clipboard.SetText(TrainerCommandLine);
        StatusText = $"Copied trainer command for {SelectedProfile.FileName}.";
    }

    private void CopyConsoleScriptPath()
    {
        Clipboard.SetText(ConsoleScriptPath);
        StatusText = "Copied WPF trainer console script path.";
    }

    private void CopyRecordingCommandLine()
    {
        Clipboard.SetText(RecordingCommandLine);
        StatusText = "Copied recording command.";
    }

    private void CopyPlaybackCommandLine()
    {
        Clipboard.SetText(PlaybackCommandLine);
        StatusText = "Copied playback command.";
    }

    private void CopyPlaybackBankCommandLine()
    {
        Clipboard.SetText(PlaybackBankCommandLine);
        StatusText = "Copied playback bank command.";
    }

    private void CopyRecordingInfoCommandLine()
    {
        Clipboard.SetText(RecordingInfoCommandLine);
        StatusText = "Copied recording info command.";
    }

    private bool CanStartCompositeTrainer()
    {
        return !IsRuntimeBusy &&
               SelectedProfile is not null &&
               File.Exists(_paths.ExePath) &&
               File.Exists(SelectedProfile.FullPath);
    }

    private bool CanStopRun()
    {
        return !IsRuntimeBusy;
    }

    private bool CanStartTrainerRecording()
    {
        return !IsRuntimeBusy &&
               SelectedProfile is not null &&
               File.Exists(_paths.ExePath) &&
               File.Exists(SelectedProfile.FullPath) &&
               !string.IsNullOrWhiteSpace(RecordingPath) &&
               TryGetRecordingDuration(out _);
    }

    private bool CanStartTrainerPlayback()
    {
        return !IsRuntimeBusy &&
               SelectedProfile is not null &&
               File.Exists(_paths.ExePath) &&
               File.Exists(SelectedProfile.FullPath) &&
               BuildSinglePlaybackBindCommand() is not null;
    }

    private bool CanStartTrainerPlaybackBank()
    {
        return !IsRuntimeBusy &&
               File.Exists(_paths.ExePath) &&
               BuildPlaybackBindCommands().Count > 0;
    }

    private bool CanInspectRecording()
    {
        return !IsRecordingInfoBusy &&
               File.Exists(_paths.ExePath) &&
               !string.IsNullOrWhiteSpace(RecordingPath);
    }

    private bool CanCopyRecordingCommandLine()
    {
        return !string.IsNullOrWhiteSpace(RecordingCommandLine);
    }

    private bool CanCopyPlaybackCommandLine()
    {
        return !string.IsNullOrWhiteSpace(PlaybackCommandLine);
    }

    private bool CanCopyPlaybackBankCommandLine()
    {
        return !string.IsNullOrWhiteSpace(PlaybackBankCommandLine);
    }

    private bool CanCopyRecordingInfoCommandLine()
    {
        return !string.IsNullOrWhiteSpace(RecordingInfoCommandLine);
    }

    private async Task StartCompositeTrainerAsync()
    {
        var profile = SelectedProfile;
        if (profile is null)
        {
            return;
        }
        if (!string.IsNullOrWhiteSpace(RecordingPath) &&
            !TryGetRecordingDuration(out var durationSeconds))
        {
            RuntimeStatusText = "Recording duration must be a positive number of seconds.";
            RecordingStatusText = RuntimeStatusText;
            StatusText = RuntimeStatusText;
            return;
        }
        TryGetRecordingDuration(out durationSeconds);

        IsRuntimeBusy = true;
        RuntimeStatusText = $"Starting composite trainer with {profile.FileName}...";
        StatusText = RuntimeStatusText;

        try
        {
            var paths = _paths;
            var recordingPath = RecordingPath;
            var recordingLiveReload = RecordingLiveReload;
            var recordingToggleKey = RecordingToggleKey;
            var recordingOverwrite = RecordingOverwrite;
            var playbackLoop = PlaybackLoop;
            var playbackBindings = BuildPlaybackBindCommands();
            PublishRuntimeControlSettings();
            var result = await Task.Run(() => _runtimeService.StartCompositeTrainer(
                paths,
                profile.FullPath,
                recordingPath,
                durationSeconds,
                recordingLiveReload,
                recordingToggleKey,
                playbackLoop,
                playbackBindings,
                recordingOverwrite,
                Gx12RuntimeService.GetRuntimeControlPath(paths)));
            RuntimeStatusText = result.Message;
            StatusText = result.Message;
            RecordingStatusText = result.Message;
            IsRunActive = result.IsSuccess || result.ActiveRunRemaining;
        }
        catch (Exception exception)
        {
            RuntimeStatusText = $"Start failed: {exception.Message}";
            StatusText = RuntimeStatusText;
            RecordingStatusText = RuntimeStatusText;
            IsRunActive = false;
        }
        finally
        {
            IsRuntimeBusy = false;
        }
    }

    public async Task ToggleCompositeTrainerAsync()
    {
        if (IsRuntimeBusy)
        {
            return;
        }

        if (IsRunActive)
        {
            await StopRunAsync();
            return;
        }

        if (CanStartCompositeTrainer())
        {
            await StartCompositeTrainerAsync();
        }
    }

    private async Task StopRunAsync()
    {
        IsRuntimeBusy = true;
        RuntimeStatusText = "Stopping active gx12mouse run...";
        StatusText = RuntimeStatusText;

        try
        {
            var paths = _paths;
            var result = await Task.Run(() => _runtimeService.StopActiveRun(paths));
            RuntimeStatusText = result.Message;
            StatusText = result.Message;
            IsRunActive = result.ActiveRunRemaining;
        }
        catch (Exception exception)
        {
            RuntimeStatusText = $"Stop failed: {exception.Message}";
            StatusText = RuntimeStatusText;
            IsRunActive = true;
        }
        finally
        {
            IsRuntimeBusy = false;
        }
    }

    private async Task StartTrainerRecordingAsync()
    {
        var profile = SelectedProfile;
        if (profile is null || !TryGetRecordingDuration(out var durationSeconds))
        {
            return;
        }

        IsRuntimeBusy = true;
        RuntimeStatusText = $"Starting recording with {profile.FileName}...";
        RecordingStatusText = RuntimeStatusText;
        StatusText = RuntimeStatusText;

        try
        {
            var paths = _paths;
            var recordingPath = RecordingPath;
            var liveReload = RecordingLiveReload;
            var recordingToggleKey = RecordingToggleKey;
            var recordingOverwrite = RecordingOverwrite;
            PublishRuntimeControlSettings();
            var result = await Task.Run(() =>
                _runtimeService.StartTrainerRecording(
                    paths,
                    profile.FullPath,
                    recordingPath,
                    durationSeconds,
                    liveReload,
                    recordingToggleKey,
                    recordingOverwrite,
                    Gx12RuntimeService.GetRuntimeControlPath(paths)));
            RuntimeStatusText = result.Message;
            RecordingStatusText = result.Message;
            StatusText = result.Message;
            IsRunActive = result.IsSuccess || result.ActiveRunRemaining;
        }
        catch (Exception exception)
        {
            RuntimeStatusText = $"Recording start failed: {exception.Message}";
            RecordingStatusText = RuntimeStatusText;
            StatusText = RuntimeStatusText;
            IsRunActive = false;
        }
        finally
        {
            IsRuntimeBusy = false;
        }
    }

    private async Task StartTrainerPlaybackAsync()
    {
        var singleBind = BuildSinglePlaybackBindCommand();
        if (singleBind is null)
        {
            RecordingStatusText = "Inline playback needs a recording file, selected channels, and a hotkey trigger.";
            StatusText = RecordingStatusText;
            return;
        }

        PublishRuntimeControlSettings();
        RuntimeStatusText = IsRunActive
            ? $"Inline playback is ready on {singleBind.Trigger} in the active trainer run."
            : $"Starting composite trainer with inline playback bind {singleBind.Trigger}...";
        RecordingStatusText = RuntimeStatusText;
        StatusText = RuntimeStatusText;
        if (IsRunActive)
        {
            return;
        }

        await StartCompositeTrainerAsync();
    }

    private async Task StartTrainerPlaybackBankAsync()
    {
        var bindings = BuildPlaybackBindCommands();
        if (bindings.Count == 0)
        {
            RecordingStatusText = "Add at least one complete playback bind.";
            StatusText = RecordingStatusText;
            return;
        }

        IsRuntimeBusy = true;
        RuntimeStatusText = $"Starting playback bank with {bindings.Count} bind(s)...";
        RecordingStatusText = RuntimeStatusText;
        StatusText = RuntimeStatusText;

        try
        {
            var paths = _paths;
            var port = PlaybackPort;
            var loop = PlaybackLoop;
            var result = await Task.Run(() =>
                _runtimeService.StartTrainerPlaybackBank(paths, port, loop, bindings));
            RuntimeStatusText = result.Message;
            RecordingStatusText = result.Message;
            StatusText = result.Message;
            IsRunActive = result.IsSuccess || result.ActiveRunRemaining;
        }
        catch (Exception exception)
        {
            RuntimeStatusText = $"Playback bank start failed: {exception.Message}";
            RecordingStatusText = RuntimeStatusText;
            StatusText = RuntimeStatusText;
            IsRunActive = false;
        }
        finally
        {
            IsRuntimeBusy = false;
        }
    }

    private async Task InspectRecordingAsync()
    {
        if (string.IsNullOrWhiteSpace(RecordingPath))
        {
            return;
        }

        IsRecordingInfoBusy = true;
        RecordingStatusText = "Reading recording info...";
        StatusText = RecordingStatusText;

        try
        {
            var paths = _paths;
            var command = _diagnosticsService.BuildRecordingInfo(paths, RecordingPath);
            var result = await _diagnosticsService
                .RunAsync(paths, command, System.Threading.CancellationToken.None)
                .ConfigureAwait(true);
            var body = string.IsNullOrWhiteSpace(result.Output)
                ? result.Message
                : result.Output.TrimEnd();
            RecordingInfoText = $"{result.CommandLine}{Environment.NewLine}{Environment.NewLine}{body}";
            RecordingStatusText = result.Message;
            StatusText = result.Message;
        }
        catch (Exception exception)
        {
            RecordingInfoText = exception.Message;
            RecordingStatusText = $"Recording info failed: {exception.Message}";
            StatusText = RecordingStatusText;
        }
        finally
        {
            IsRecordingInfoBusy = false;
        }
    }

    public void StopRuntimeOnExit()
    {
        if (IsRunActive)
        {
            _runtimeService.StopActiveRunOnExit(_paths);
        }
    }

    private void LoadPlaybackBindingsFromSettings()
    {
        _suppressPlaybackBindingSave = true;
        try
        {
            foreach (var binding in _uiSettingsService.Load(_paths).PlaybackBindings)
            {
                AddPlaybackBindingSlot(
                    new PlaybackBindingViewModel(
                        PlaybackBindings.Count + 1,
                        binding.RecordingPath,
                        binding.Trigger,
                        binding.ChannelMask,
                        binding.BlockLiveInput),
                    save: false);
            }
        }
        finally
        {
            _suppressPlaybackBindingSave = false;
        }

        RaisePlaybackBindingStateChanged();
    }

    private void LoadRecordingPathFromSettings()
    {
        var settings = _uiSettingsService.Load(_paths);
        _recordingOverwrite = settings.RecordingOverwrite;
        OnPropertyChanged(nameof(RecordingOverwrite));
        var recordingPath = settings.RecordingPath;
        if (string.IsNullOrWhiteSpace(recordingPath))
        {
            return;
        }

        _suppressRecordingPathSave = true;
        try
        {
            SetRecordingPath(recordingPath, userEdited: true);
        }
        finally
        {
            _suppressRecordingPathSave = false;
        }
    }

    private void LoadPlaybackChannelsFromSettings()
    {
        var settings = _uiSettingsService.Load(_paths);
        _suppressPlaybackChannelSave = true;
        try
        {
            SetPlaybackChannel(ref _playbackAileron, settings.PlaybackAileron, nameof(PlaybackAileron));
            SetPlaybackChannel(ref _playbackElevator, settings.PlaybackElevator, nameof(PlaybackElevator));
            SetPlaybackChannel(ref _playbackThrottle, settings.PlaybackThrottle, nameof(PlaybackThrottle));
            SetPlaybackChannel(ref _playbackRudder, settings.PlaybackRudder, nameof(PlaybackRudder));
            SetPlaybackChannel(ref _playbackRadioRightGimbal, settings.PlaybackRadioRightGimbal, nameof(PlaybackRadioRightGimbal));
            SetPlaybackChannel(ref _playbackRecordedTrainerRight, settings.PlaybackRecordedTrainerRight, nameof(PlaybackRecordedTrainerRight));
            SetPlaybackChannel(ref _playbackRadioLeftGimbal, settings.PlaybackRadioLeftGimbal, nameof(PlaybackRadioLeftGimbal));
            SetPlaybackChannel(ref _playbackRecordedTrainerLeft, settings.PlaybackRecordedTrainerLeft, nameof(PlaybackRecordedTrainerLeft));
            SetPlaybackChannel(ref _playbackBlockLiveInput, settings.PlaybackBlockLiveInput, nameof(PlaybackBlockLiveInput));
        }
        finally
        {
            _suppressPlaybackChannelSave = false;
        }
    }

    private void AddPlaybackBinding()
    {
        if (!CanAddPlaybackBinding)
        {
            return;
        }

        var trigger = DefaultPlaybackBindingTrigger(PlaybackBindings.Count + 1);
        var channelMask = string.IsNullOrWhiteSpace(PlaybackChannelMask) ? "ail,ele" : PlaybackChannelMask;
        AddPlaybackBindingSlot(
            new PlaybackBindingViewModel(
                PlaybackBindings.Count + 1,
                string.IsNullOrWhiteSpace(PlaybackRecordingPath) ? RecordingPath : PlaybackRecordingPath,
                trigger,
                channelMask,
                PlaybackBlockLiveInput),
            save: true);
    }

    private void AddPlaybackBindingSlot(PlaybackBindingViewModel slot, bool save)
    {
        slot.SlotNumber = PlaybackBindings.Count + 1;
        slot.Changed += OnPlaybackBindingChanged;
        PlaybackBindings.Add(slot);
        if (save)
        {
            SavePlaybackBindingsToSettings();
        }

        RaisePlaybackBindingStateChanged();
    }

    private void RemovePlaybackBinding(PlaybackBindingViewModel slot)
    {
        slot.Changed -= OnPlaybackBindingChanged;
        PlaybackBindings.Remove(slot);
        for (var index = 0; index < PlaybackBindings.Count; index++)
        {
            PlaybackBindings[index].SlotNumber = index + 1;
        }

        SavePlaybackBindingsToSettings();
        RaisePlaybackBindingStateChanged();
    }

    private void OnPlaybackBindingChanged(object? sender, EventArgs e)
    {
        if (sender is not PlaybackBindingViewModel slot)
        {
            return;
        }

        if (!slot.IsEnabled)
        {
            RemovePlaybackBinding(slot);
            return;
        }

        SavePlaybackBindingsToSettings();
        RaisePlaybackBindingStateChanged();
    }

    private IReadOnlyList<PlaybackBindCommand> BuildPlaybackBindCommands()
    {
        var commands = new List<PlaybackBindCommand>();
        var singleBind = BuildSinglePlaybackBindCommand();
        if (singleBind is not null)
        {
            commands.Add(singleBind);
        }

        commands.AddRange(PlaybackBindings
            .Where(binding => binding.IsComplete)
            .Select(binding => new PlaybackBindCommand(
                binding.RecordingPath,
                binding.Trigger,
                binding.ChannelMask,
                binding.BlockLiveInput)));
        return commands;
    }

    private PlaybackBindCommand? BuildSinglePlaybackBindCommand()
    {
        var channelMask = PlaybackChannelMask;
        var trigger = (PlaybackTrigger ?? string.Empty).Trim();
        if (string.IsNullOrWhiteSpace(PlaybackRecordingPath) ||
            string.IsNullOrWhiteSpace(channelMask) ||
            IsImmediatePlaybackTrigger(trigger))
        {
            return null;
        }

        var playbackPath = PlaybackRecordingPath.Trim();
        return new PlaybackBindCommand(
            playbackPath,
            trigger,
            channelMask,
            PlaybackBlockLiveInput);
    }

    private static bool IsImmediatePlaybackTrigger(string trigger)
    {
        var normalized = trigger.Trim();
        return string.IsNullOrWhiteSpace(normalized) ||
               normalized.Equals("immediate", StringComparison.OrdinalIgnoreCase) ||
               normalized.Equals("off", StringComparison.OrdinalIgnoreCase) ||
               normalized.Equals("none", StringComparison.OrdinalIgnoreCase);
    }

    private void SavePlaybackBindingsToSettings()
    {
        if (_suppressPlaybackBindingSave)
        {
            return;
        }

        var settings = _uiSettingsService.Load(_paths);
        settings.PlaybackBindings = PlaybackBindings
            .Where(binding => binding.IsEnabled)
            .Select(binding => new PlaybackBindingSettings
            {
                Enabled = true,
                RecordingPath = binding.RecordingPath,
                Trigger = binding.Trigger,
                ChannelMask = binding.ChannelMask,
                BlockLiveInput = binding.BlockLiveInput
            })
            .ToList();
        _uiSettingsService.Save(_paths, settings);
        PublishRuntimeControlSettings();
    }

    private void SaveRecordingSettingsToSettings()
    {
        if (_suppressRecordingPathSave)
        {
            return;
        }

        var settings = _uiSettingsService.Load(_paths);
        settings.RecordingPath = _recordingPath;
        settings.RecordingOverwrite = _recordingOverwrite;
        _uiSettingsService.Save(_paths, settings);
        PublishRuntimeControlSettings();
    }

    private void SavePlaybackChannelsToSettings()
    {
        if (_suppressPlaybackChannelSave)
        {
            return;
        }

        var settings = _uiSettingsService.Load(_paths);
        settings.PlaybackAileron = _playbackAileron;
        settings.PlaybackElevator = _playbackElevator;
        settings.PlaybackThrottle = _playbackThrottle;
        settings.PlaybackRudder = _playbackRudder;
        settings.PlaybackRadioRightGimbal = _playbackRadioRightGimbal;
        settings.PlaybackRecordedTrainerRight = _playbackRecordedTrainerRight;
        settings.PlaybackRadioLeftGimbal = _playbackRadioLeftGimbal;
        settings.PlaybackRecordedTrainerLeft = _playbackRecordedTrainerLeft;
        settings.PlaybackBlockLiveInput = _playbackBlockLiveInput;
        _uiSettingsService.Save(_paths, settings);
        PublishRuntimeControlSettings();
    }

    private void PublishRuntimeControlSettings()
    {
        try
        {
            _runtimeService.WriteRuntimeControlFile(
                _paths,
                RecordingPath,
                TryGetRecordingDuration(out var durationSeconds) ? durationSeconds : 0,
                RecordingToggleKey,
                RecordingOverwrite,
                PlaybackLoop,
                BuildPlaybackBindCommands());
        }
        catch
        {
        }
    }

    private static string DefaultPlaybackBindingTrigger(int slotNumber)
    {
        return $"F{Math.Clamp(slotNumber + 4, 5, 24)}";
    }

    private void OnEditorProfileSaved(object? sender, EventArgs e)
    {
        StatusText = _editor?.SaveStateText ?? "Profile saved.";
        if (_editor is null)
        {
            return;
        }

        try
        {
            var refreshed = _repository.LoadProfile(_repository.Paths, _editor.FullPath);
            for (var index = 0; index < Profiles.Count; index++)
            {
                if (Profiles[index].FileName.Equals(refreshed.FileName, StringComparison.OrdinalIgnoreCase))
                {
                    Profiles[index] = refreshed;
                    break;
                }
            }

            if (SetProperty(ref _selectedProfile, refreshed))
            {
                RaiseSelectedProfileChanged();
            }

            ProfilesView.Refresh();
            OnPropertyChanged(nameof(ProfileCountSummary));
        }
        catch (Exception exception)
        {
            StatusText = $"Profile saved; summary refresh failed: {exception.Message}";
        }
    }

    private bool ShouldKeepCurrentProfileSelection()
    {
        return _selectedProfile is not null &&
               Profiles.Any(profile => IsSameProfile(profile, _selectedProfile));
    }

    private static bool IsSameProfile(ProfileSummary? left, ProfileSummary? right)
    {
        return left is not null &&
               right is not null &&
               left.FullPath.Equals(right.FullPath, StringComparison.OrdinalIgnoreCase);
    }

    private void SetRecordingPath(string value, bool userEdited)
    {
        if (userEdited)
        {
            _recordingPathEditedByUser = true;
        }

        if (SetProperty(ref _recordingPath, value ?? string.Empty, nameof(RecordingPath)))
        {
            if (!_playbackPathEditedByUser)
            {
                SetProperty(ref _playbackRecordingPath, _recordingPath, nameof(PlaybackRecordingPath));
            }

            if (userEdited)
            {
                SaveRecordingSettingsToSettings();
            }

            RaiseRecordingCommandStates();
        }
    }

    private void SetPlaybackRecordingPath(string value, bool userEdited)
    {
        if (userEdited)
        {
            _playbackPathEditedByUser = true;
        }

        if (SetProperty(ref _playbackRecordingPath, value ?? string.Empty, nameof(PlaybackRecordingPath)))
        {
            PublishRuntimeControlSettings();
            RaiseRecordingCommandStates();
        }
    }

    private void SetPlaybackChannel(ref bool field, bool value, string propertyName)
    {
        if (SetProperty(ref field, value, propertyName))
        {
            SavePlaybackChannelsToSettings();
            RaiseRecordingCommandStates();
        }
    }

    private void SetPlaybackRightSource(ref bool field, bool value, string propertyName)
    {
        if (!SetProperty(ref field, value, propertyName))
        {
            return;
        }

        if (value)
        {
            if (propertyName == nameof(PlaybackRadioRightGimbal))
            {
                SetProperty(ref _playbackRecordedTrainerRight, false, nameof(PlaybackRecordedTrainerRight));
            }
            else if (propertyName == nameof(PlaybackRecordedTrainerRight))
            {
                SetProperty(ref _playbackRadioRightGimbal, false, nameof(PlaybackRadioRightGimbal));
            }
        }

        SavePlaybackChannelsToSettings();
        RaiseRecordingCommandStates();
    }

    private void SetPlaybackLeftSource(ref bool field, bool value, string propertyName)
    {
        if (!SetProperty(ref field, value, propertyName))
        {
            return;
        }

        if (value)
        {
            if (propertyName == nameof(PlaybackRadioLeftGimbal))
            {
                SetProperty(ref _playbackRecordedTrainerLeft, false, nameof(PlaybackRecordedTrainerLeft));
            }
            else if (propertyName == nameof(PlaybackRecordedTrainerLeft))
            {
                SetProperty(ref _playbackRadioLeftGimbal, false, nameof(PlaybackRadioLeftGimbal));
            }
        }

        SavePlaybackChannelsToSettings();
        RaiseRecordingCommandStates();
    }

    private string PlaybackRightChannel(string channel)
    {
        if (PlaybackRecordedTrainerRight)
        {
            return channel == "ail" ? "trainer_ail" : "trainer_ele";
        }

        if (PlaybackRadioRightGimbal)
        {
            return channel == "ail" ? "radio_ail" : "radio_ele";
        }

        if (SelectedProfile is { MouseRightEnabled: false })
        {
            return channel == "ail" ? "radio_ail" : "radio_ele";
        }

        return channel;
    }

    private string PlaybackLeftChannel(string channel)
    {
        if (PlaybackRecordedTrainerLeft)
        {
            return channel == "thr" ? "trainer_thr" : "trainer_rud";
        }

        if (PlaybackRadioLeftGimbal)
        {
            return channel == "thr" ? "radio_thr" : "radio_rud";
        }

        if (SelectedProfile is not null &&
            !SelectedProfile.MouseLeftEnabled &&
            !SelectedProfile.RightMouseLeftEnabled)
        {
            return channel == "thr" ? "radio_thr" : "radio_rud";
        }

        return channel;
    }

    private bool TryGetRecordingDuration(out int durationSeconds)
    {
        return int.TryParse(
                   RecordingDurationSeconds,
                   NumberStyles.Integer,
                   CultureInfo.InvariantCulture,
                   out durationSeconds) &&
               durationSeconds is >= 1 and <= 3600;
    }

    private void UpdateDefaultRecordingPathForSelectedProfile()
    {
        if (_recordingPathEditedByUser || SelectedProfile is null)
        {
            return;
        }

        SetRecordingPath(BuildDefaultRecordingPath(SelectedProfile), userEdited: false);
    }

    private static string BuildDefaultRecordingPath(ProfileSummary profile)
    {
        var baseName = SanitizeFileName(Path.GetFileNameWithoutExtension(profile.FileName));
        var stamp = DateTime.Now.ToString("yyyyMMdd-HHmmss", CultureInfo.InvariantCulture);
        return Path.Combine("logs", $"{baseName}-{stamp}.gx12rec.csv");
    }

    private static string SanitizeFileName(string value)
    {
        var invalid = Path.GetInvalidFileNameChars();
        var safe = new string(value
            .Select(character => invalid.Contains(character) ? '-' : character)
            .ToArray());
        return string.IsNullOrWhiteSpace(safe) ? "recording" : safe;
    }

    private void RaiseSelectedProfileChanged()
    {
        UpdateDefaultRecordingPathForSelectedProfile();
        CopyPathCommand.RaiseCanExecuteChanged();
        CloneProfileCommand.RaiseCanExecuteChanged();
        SetDefaultCommand.RaiseCanExecuteChanged();
        CopyTrainerCommandLineCommand.RaiseCanExecuteChanged();
        StartCompositeTrainerCommand.RaiseCanExecuteChanged();
        OnPropertyChanged(nameof(SelectedProfilePath));
        OnPropertyChanged(nameof(TrainerCommandLine));
        OnPropertyChanged(nameof(SelectedProfileHeader));
        OnPropertyChanged(nameof(SelectedProfileSubheader));
        RaiseRecordingCommandStates();
    }

    private void RaiseRuntimeCommandStates()
    {
        StartCompositeTrainerCommand.RaiseCanExecuteChanged();
        StopRunCommand.RaiseCanExecuteChanged();
    }

    private void RaiseRecordingCommandStates()
    {
        StartTrainerRecordingCommand.RaiseCanExecuteChanged();
        StartTrainerPlaybackCommand.RaiseCanExecuteChanged();
        StartTrainerPlaybackBankCommand.RaiseCanExecuteChanged();
        InspectRecordingCommand.RaiseCanExecuteChanged();
        CopyRecordingCommandLineCommand.RaiseCanExecuteChanged();
        CopyPlaybackCommandLineCommand.RaiseCanExecuteChanged();
        CopyPlaybackBankCommandLineCommand.RaiseCanExecuteChanged();
        CopyRecordingInfoCommandLineCommand.RaiseCanExecuteChanged();
        OnPropertyChanged(nameof(RecordingCommandLine));
        OnPropertyChanged(nameof(PlaybackCommandLine));
        OnPropertyChanged(nameof(PlaybackBankCommandLine));
        OnPropertyChanged(nameof(TrainerCommandLine));
        OnPropertyChanged(nameof(RecordingInfoCommandLine));
        OnPropertyChanged(nameof(RecordingBufferStatusText));
        OnPropertyChanged(nameof(PlaybackChannelMask));
        PublishRuntimeControlSettings();
    }

    private void RaisePlaybackBindingStateChanged()
    {
        OnPropertyChanged(nameof(HasPlaybackBindings));
        OnPropertyChanged(nameof(CanAddPlaybackBinding));
        OnPropertyChanged(nameof(PlaybackBindingLimitText));
        OnPropertyChanged(nameof(AddPlaybackBindingToggle));
        RaiseRecordingCommandStates();
    }
}
