using System;
using System.Globalization;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Input;

namespace Gx12.Launcher.Wpf.Controls;

public static class HotkeyCapture
{
    public static readonly DependencyProperty IsEnabledProperty =
        DependencyProperty.RegisterAttached(
            "IsEnabled",
            typeof(bool),
            typeof(HotkeyCapture),
            new PropertyMetadata(false, OnIsEnabledChanged));

    public static readonly DependencyProperty ClearValueProperty =
        DependencyProperty.RegisterAttached(
            "ClearValue",
            typeof(string),
            typeof(HotkeyCapture),
            new PropertyMetadata(""));

    public static bool GetIsEnabled(DependencyObject element)
    {
        return element.GetValue(IsEnabledProperty) is true;
    }

    public static void SetIsEnabled(DependencyObject element, bool value)
    {
        element.SetValue(IsEnabledProperty, value);
    }

    public static string GetClearValue(DependencyObject element)
    {
        return element.GetValue(ClearValueProperty) as string ?? "";
    }

    public static void SetClearValue(DependencyObject element, string value)
    {
        element.SetValue(ClearValueProperty, value ?? "");
    }

    public static bool TryCommitFocusedHotkey(string value)
    {
        if (Keyboard.FocusedElement is not TextBox textBox || !GetIsEnabled(textBox))
        {
            return false;
        }

        CommitHotkey(textBox, value);
        return true;
    }

    public static void CommitHotkey(TextBox textBox, string value)
    {
        textBox.SetCurrentValue(TextBox.TextProperty, value ?? "");
        textBox.GetBindingExpression(TextBox.TextProperty)?.UpdateSource();
        textBox.CaretIndex = textBox.Text.Length;
        textBox.SelectAll();
    }

    public static string? FormatKey(Key key)
    {
        if (key is Key.None or Key.ImeProcessed or Key.System)
        {
            return null;
        }

        if (key >= Key.A && key <= Key.Z)
        {
            return key.ToString().ToUpperInvariant();
        }

        if (key >= Key.D0 && key <= Key.D9)
        {
            return ((int)(key - Key.D0)).ToString(CultureInfo.InvariantCulture);
        }

        if (key >= Key.F1 && key <= Key.F24)
        {
            return key.ToString();
        }

        return key switch
        {
            Key.Space => "Space",
            Key.Escape => "Esc",
            Key.Tab => "Tab",
            Key.Return => "Enter",
            Key.Back => "Backspace",
            Key.Up => "Up",
            Key.Down => "Down",
            Key.Left => "Left",
            Key.Right => "Right",
            Key.LeftShift or Key.RightShift => "Shift",
            Key.LeftCtrl or Key.RightCtrl => "Ctrl",
            Key.LeftAlt or Key.RightAlt => "Alt",
            _ => FormatVirtualKey(KeyInterop.VirtualKeyFromKey(key))
        };
    }

    public static string? FormatMouseButton(MouseButton button)
    {
        return button switch
        {
            MouseButton.Left => "Mouse1",
            MouseButton.Right => "Mouse2",
            MouseButton.Middle => "Mouse3",
            MouseButton.XButton1 => "Mouse4",
            MouseButton.XButton2 => "Mouse5",
            _ => null
        };
    }

    public static bool TryParseKeyboardVirtualKey(string? value, out int virtualKey)
    {
        virtualKey = 0;
        var key = (value ?? "").Trim().ToLowerInvariant();
        if (key.Length == 0 ||
            key.Equals("none", StringComparison.Ordinal) ||
            key.Equals("off", StringComparison.Ordinal) ||
            key.Equals("immediate", StringComparison.Ordinal))
        {
            return false;
        }

        if (key.StartsWith("mouse", StringComparison.Ordinal) ||
            key.Equals("lbutton", StringComparison.Ordinal) ||
            key.Equals("rbutton", StringComparison.Ordinal) ||
            key.Equals("mbutton", StringComparison.Ordinal) ||
            key.StartsWith("xbutton", StringComparison.Ordinal))
        {
            return false;
        }

        if (key.Length == 1)
        {
            var ch = key[0];
            if (ch is >= 'a' and <= 'z')
            {
                virtualKey = char.ToUpperInvariant(ch);
                return true;
            }

            if (ch is >= '0' and <= '9')
            {
                virtualKey = ch;
                return true;
            }
        }

        if (key.Length >= 2 && key[0] == 'f' &&
            int.TryParse(key[1..], NumberStyles.None, CultureInfo.InvariantCulture, out var functionKey) &&
            functionKey is >= 1 and <= 24)
        {
            virtualKey = 0x70 + functionKey - 1;
            return true;
        }

        if (key.StartsWith("vk", StringComparison.Ordinal) &&
            int.TryParse(key[2..], NumberStyles.None, CultureInfo.InvariantCulture, out var rawVirtualKey) &&
            rawVirtualKey is >= 1 and <= 255)
        {
            virtualKey = rawVirtualKey;
            return true;
        }

        virtualKey = key switch
        {
            "space" or "spacebar" => 0x20,
            "esc" or "escape" => 0x1B,
            "shift" => 0x10,
            "ctrl" or "control" => 0x11,
            "alt" => 0x12,
            "tab" => 0x09,
            "enter" or "return" => 0x0D,
            "backspace" => 0x08,
            "up" => 0x26,
            "down" => 0x28,
            "left" => 0x25,
            "right" => 0x27,
            _ => 0
        };

        return virtualKey > 0;
    }

    private static void OnIsEnabledChanged(DependencyObject element, DependencyPropertyChangedEventArgs e)
    {
        if (element is not TextBox textBox)
        {
            return;
        }

        if (e.OldValue is true)
        {
            textBox.GotKeyboardFocus -= OnGotKeyboardFocus;
            textBox.PreviewKeyDown -= OnPreviewKeyDown;
            textBox.PreviewMouseDown -= OnPreviewMouseDown;
            textBox.PreviewTextInput -= OnPreviewTextInput;
            DataObject.RemovePastingHandler(textBox, OnPaste);
        }

        if (e.NewValue is true)
        {
            textBox.GotKeyboardFocus += OnGotKeyboardFocus;
            textBox.PreviewKeyDown += OnPreviewKeyDown;
            textBox.PreviewMouseDown += OnPreviewMouseDown;
            textBox.PreviewTextInput += OnPreviewTextInput;
            DataObject.AddPastingHandler(textBox, OnPaste);
        }
    }

    private static void OnGotKeyboardFocus(object sender, KeyboardFocusChangedEventArgs e)
    {
        if (sender is TextBox textBox)
        {
            textBox.SelectAll();
        }
    }

    private static void OnPreviewKeyDown(object sender, KeyEventArgs e)
    {
        if (sender is not TextBox textBox)
        {
            return;
        }

        if (e.Key == Key.Delete)
        {
            CommitHotkey(textBox, GetClearValue(textBox));
            e.Handled = true;
            return;
        }

        var key = e.Key == Key.System
            ? e.SystemKey
            : e.Key == Key.ImeProcessed
                ? e.ImeProcessedKey
                : e.Key;
        var formatted = FormatKey(key);
        if (formatted is null)
        {
            return;
        }

        CommitHotkey(textBox, formatted);
        e.Handled = true;
    }

    private static void OnPreviewMouseDown(object sender, MouseButtonEventArgs e)
    {
        if (sender is not TextBox textBox)
        {
            return;
        }

        if (!textBox.IsKeyboardFocusWithin)
        {
            textBox.Focus();
            textBox.SelectAll();
            e.Handled = true;
            return;
        }

        var formatted = FormatMouseButton(e.ChangedButton);
        if (formatted is null)
        {
            return;
        }

        CommitHotkey(textBox, formatted);
        e.Handled = true;
    }

    private static void OnPreviewTextInput(object sender, TextCompositionEventArgs e)
    {
        e.Handled = true;
    }

    private static void OnPaste(object sender, DataObjectPastingEventArgs e)
    {
        e.CancelCommand();
    }

    private static string? FormatVirtualKey(int virtualKey)
    {
        return virtualKey is >= 1 and <= 255
            ? $"Vk{virtualKey.ToString(CultureInfo.InvariantCulture)}"
            : null;
    }
}
