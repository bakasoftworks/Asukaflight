using System;

namespace Gx12.Launcher.Wpf.ViewModels;

public sealed class PlaybackBindingViewModel : ObservableObject
{
    private int _slotNumber;
    private bool _isEnabled = true;
    private string _recordingPath;
    private string _trigger;
    private string _channelMask;
    private bool _blockLiveInput;

    public PlaybackBindingViewModel(
        int slotNumber,
        string recordingPath,
        string trigger,
        string channelMask,
        bool blockLiveInput = false)
    {
        _slotNumber = slotNumber;
        _recordingPath = recordingPath ?? string.Empty;
        _trigger = string.IsNullOrWhiteSpace(trigger) ? $"F{Math.Min(24, slotNumber + 4)}" : trigger;
        _channelMask = string.IsNullOrWhiteSpace(channelMask) ? "ail,ele" : channelMask;
        _blockLiveInput = blockLiveInput;
    }

    public event EventHandler? Changed;

    public int SlotNumber
    {
        get => _slotNumber;
        set => SetProperty(ref _slotNumber, value);
    }

    public bool IsEnabled
    {
        get => _isEnabled;
        set
        {
            if (SetProperty(ref _isEnabled, value))
            {
                Changed?.Invoke(this, EventArgs.Empty);
            }
        }
    }

    public string RecordingPath
    {
        get => _recordingPath;
        set
        {
            if (SetProperty(ref _recordingPath, value ?? string.Empty))
            {
                Changed?.Invoke(this, EventArgs.Empty);
            }
        }
    }

    public string Trigger
    {
        get => _trigger;
        set
        {
            if (SetProperty(ref _trigger, value ?? string.Empty))
            {
                Changed?.Invoke(this, EventArgs.Empty);
            }
        }
    }

    public string ChannelMask
    {
        get => _channelMask;
        set
        {
            if (SetProperty(ref _channelMask, value ?? string.Empty))
            {
                Changed?.Invoke(this, EventArgs.Empty);
            }
        }
    }

    public bool BlockLiveInput
    {
        get => _blockLiveInput;
        set
        {
            if (SetProperty(ref _blockLiveInput, value))
            {
                Changed?.Invoke(this, EventArgs.Empty);
            }
        }
    }

    public bool IsComplete =>
        IsEnabled &&
        !string.IsNullOrWhiteSpace(RecordingPath) &&
        !string.IsNullOrWhiteSpace(Trigger) &&
        !string.IsNullOrWhiteSpace(ChannelMask);
}
