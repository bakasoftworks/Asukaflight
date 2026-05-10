using System.ComponentModel;
using System;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Interop;
using Gx12.Launcher.Wpf.Controls;
using Gx12.Launcher.Wpf.Services;
using Gx12.Launcher.Wpf.ViewModels;

namespace Gx12.Launcher.Wpf;

public partial class MainWindow : Window
{
    private const int StartStopHotkeyId = 0x5812;
    private const int WmHotkey = 0x0312;
    private const uint ModNoRepeat = 0x4000;

    private HwndSource? _source;
    private IntPtr _windowHandle;
    private bool _startStopHotkeyRegistered;
    private string _registeredStartStopHotkeyText = "";

    public MainWindow()
        : this(CompositionRoot.CreateMainViewModel())
    {
    }

    public MainWindow(MainViewModel viewModel)
    {
        InitializeComponent();
        DataContext = viewModel;
    }

    public void SelectWorkspaceTab(string header)
    {
        foreach (var item in WorkspaceTabs.Items)
        {
            if (item is TabItem tabItem &&
                string.Equals(tabItem.Header?.ToString(), header, System.StringComparison.OrdinalIgnoreCase))
            {
                WorkspaceTabs.SelectedItem = tabItem;
                return;
            }
        }
    }

    protected override void OnClosing(CancelEventArgs e)
    {
        if (DataContext is MainViewModel viewModel)
        {
            viewModel.StopRuntimeOnExit();
        }

        base.OnClosing(e);
    }

    protected override void OnSourceInitialized(EventArgs e)
    {
        base.OnSourceInitialized(e);

        _windowHandle = new WindowInteropHelper(this).Handle;
        _source = HwndSource.FromHwnd(_windowHandle);
        _source?.AddHook(WndProc);
        if (DataContext is INotifyPropertyChanged notifier)
        {
            notifier.PropertyChanged += OnViewModelPropertyChanged;
        }

        RegisterStartStopHotkey();
    }

    protected override void OnClosed(EventArgs e)
    {
        UnregisterStartStopHotkey();
        if (DataContext is INotifyPropertyChanged notifier)
        {
            notifier.PropertyChanged -= OnViewModelPropertyChanged;
        }

        _source?.RemoveHook(WndProc);
        _source = null;
        base.OnClosed(e);
    }

    private void OnViewModelPropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName is nameof(MainViewModel.SelectedProfile))
        {
            RegisterStartStopHotkey();
        }
    }

    private IntPtr WndProc(IntPtr hwnd, int msg, IntPtr wParam, IntPtr lParam, ref bool handled)
    {
        if (msg == WmHotkey && wParam.ToInt32() == StartStopHotkeyId)
        {
            if (!string.IsNullOrWhiteSpace(_registeredStartStopHotkeyText) &&
                HotkeyCapture.TryCommitFocusedHotkey(_registeredStartStopHotkeyText))
            {
                handled = true;
                return IntPtr.Zero;
            }

            if (DataContext is MainViewModel viewModel)
            {
                _ = viewModel.ToggleCompositeTrainerAsync();
            }

            handled = true;
            return IntPtr.Zero;
        }

        return IntPtr.Zero;
    }

    private void RegisterStartStopHotkey()
    {
        UnregisterStartStopHotkey();
        if (_windowHandle == IntPtr.Zero ||
            DataContext is not MainViewModel viewModel ||
            viewModel.SelectedProfile is null)
        {
            return;
        }

        var hotkeyText = viewModel.SelectedProfile.StopKey;
        if (!HotkeyCapture.TryParseKeyboardVirtualKey(hotkeyText, out var virtualKey))
        {
            return;
        }

        if (RegisterHotKey(_windowHandle, StartStopHotkeyId, ModNoRepeat, (uint)virtualKey))
        {
            _startStopHotkeyRegistered = true;
            _registeredStartStopHotkeyText = hotkeyText.Trim();
        }
    }

    private void UnregisterStartStopHotkey()
    {
        if (!_startStopHotkeyRegistered || _windowHandle == IntPtr.Zero)
        {
            _startStopHotkeyRegistered = false;
            _registeredStartStopHotkeyText = "";
            return;
        }

        UnregisterHotKey(_windowHandle, StartStopHotkeyId);
        _startStopHotkeyRegistered = false;
        _registeredStartStopHotkeyText = "";
    }

    [DllImport("user32.dll", SetLastError = true)]
    private static extern bool RegisterHotKey(IntPtr hWnd, int id, uint fsModifiers, uint vk);

    [DllImport("user32.dll", SetLastError = true)]
    private static extern bool UnregisterHotKey(IntPtr hWnd, int id);
}
