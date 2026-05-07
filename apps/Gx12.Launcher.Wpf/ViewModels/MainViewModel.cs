using System;
using System.Collections.ObjectModel;
using System.ComponentModel;
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
    private readonly Gx12RuntimeService _runtimeService;
    private AppPaths _paths;
    private ReleaseInfo _releaseInfo;
    private ProfileSummary? _selectedProfile;
    private ProfileEditorViewModel? _editor;
    private string _searchText = string.Empty;
    private string _statusText = string.Empty;
    private string _runtimeStatusText = "Runtime ready. Select a profile and start the composite trainer.";
    private bool _isRuntimeBusy;
    private bool _isRunActive;

    public MainViewModel(ProfileRepository repository, IProfileFolderPicker folderPicker)
        : this(repository, folderPicker, new UiSettingsService(), new TooltipImagePicker(), new Gx12RuntimeService())
    {
    }

    public MainViewModel(
        ProfileRepository repository,
        IProfileFolderPicker folderPicker,
        UiSettingsService uiSettingsService,
        ITooltipImagePicker tooltipImagePicker,
        Gx12RuntimeService runtimeService)
    {
        _repository = repository;
        _folderPicker = folderPicker;
        _uiSettingsService = uiSettingsService;
        _tooltipImagePicker = tooltipImagePicker;
        _runtimeService = runtimeService;
        _paths = repository.Paths;
        _releaseInfo = ReleaseInfoService.Create(_paths);
        TooltipSpriteService.Shared.Configure(_uiSettingsService.GetTooltipSpriteDirectory(_paths), ensureDirectory: true);
        Profiles = new ObservableCollection<ProfileSummary>();
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

        RefreshProfiles();
    }

    public ObservableCollection<ProfileSummary> Profiles { get; }

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
                ? "Starting another profile stops this run first."
                : "Ready for the selected profile and safety keys.";
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
        : Gx12RuntimeService.BuildTrainerCommandLine(_paths, SelectedProfile.FullPath);

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
        OnPropertyChanged(nameof(AboveBarSpritePath));
        OnPropertyChanged(nameof(HasAboveBarSprite));
        OnPropertyChanged(nameof(ProfileCountSummary));
        CopyTrainerCommandLineCommand.RaiseCanExecuteChanged();
        StartCompositeTrainerCommand.RaiseCanExecuteChanged();

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

    private async Task StartCompositeTrainerAsync()
    {
        var profile = SelectedProfile;
        if (profile is null)
        {
            return;
        }

        IsRuntimeBusy = true;
        RuntimeStatusText = $"Starting composite trainer with {profile.FileName}...";
        StatusText = RuntimeStatusText;

        try
        {
            var paths = _paths;
            var result = await Task.Run(() => _runtimeService.StartCompositeTrainer(paths, profile.FullPath));
            RuntimeStatusText = result.Message;
            StatusText = result.Message;
            IsRunActive = result.IsSuccess || result.ActiveRunRemaining;
        }
        catch (Exception exception)
        {
            RuntimeStatusText = $"Start failed: {exception.Message}";
            StatusText = RuntimeStatusText;
            IsRunActive = false;
        }
        finally
        {
            IsRuntimeBusy = false;
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

    public void StopRuntimeOnExit()
    {
        if (IsRunActive)
        {
            _runtimeService.StopActiveRunOnExit(_paths);
        }
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

    private void RaiseSelectedProfileChanged()
    {
        CopyPathCommand.RaiseCanExecuteChanged();
        CloneProfileCommand.RaiseCanExecuteChanged();
        SetDefaultCommand.RaiseCanExecuteChanged();
        CopyTrainerCommandLineCommand.RaiseCanExecuteChanged();
        StartCompositeTrainerCommand.RaiseCanExecuteChanged();
        OnPropertyChanged(nameof(SelectedProfilePath));
        OnPropertyChanged(nameof(TrainerCommandLine));
        OnPropertyChanged(nameof(SelectedProfileHeader));
        OnPropertyChanged(nameof(SelectedProfileSubheader));
    }

    private void RaiseRuntimeCommandStates()
    {
        StartCompositeTrainerCommand.RaiseCanExecuteChanged();
        StopRunCommand.RaiseCanExecuteChanged();
    }
}
