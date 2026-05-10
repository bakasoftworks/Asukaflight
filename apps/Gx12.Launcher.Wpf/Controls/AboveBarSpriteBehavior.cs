using System;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Animation;
using System.Windows.Threading;

namespace Gx12.Launcher.Wpf.Controls;

public static class AboveBarSpriteBehavior
{
    private const int HiddenZIndex = 1;
    private const int RestingZIndex = 5;
    private const double HiddenOffset = 96;
    private static readonly TimeSpan HideDuration = TimeSpan.FromMilliseconds(200);
    private static readonly TimeSpan ReturnDuration = TimeSpan.FromSeconds(6);

    private static readonly DependencyProperty ReturnTimerProperty = DependencyProperty.RegisterAttached(
        "ReturnTimer",
        typeof(DispatcherTimer),
        typeof(AboveBarSpriteBehavior),
        new PropertyMetadata(null));

    public static readonly DependencyProperty IsEnabledProperty = DependencyProperty.RegisterAttached(
        "IsEnabled",
        typeof(bool),
        typeof(AboveBarSpriteBehavior),
        new PropertyMetadata(false, OnIsEnabledChanged));

    public static readonly DependencyProperty MinReturnDelaySecondsProperty = DependencyProperty.RegisterAttached(
        "MinReturnDelaySeconds",
        typeof(int),
        typeof(AboveBarSpriteBehavior),
        new PropertyMetadata(10));

    public static readonly DependencyProperty MaxReturnDelaySecondsProperty = DependencyProperty.RegisterAttached(
        "MaxReturnDelaySeconds",
        typeof(int),
        typeof(AboveBarSpriteBehavior),
        new PropertyMetadata(600));

    public static readonly DependencyProperty UseRandomReturnDelayProperty = DependencyProperty.RegisterAttached(
        "UseRandomReturnDelay",
        typeof(bool),
        typeof(AboveBarSpriteBehavior),
        new PropertyMetadata(true));

    public static readonly DependencyProperty FixedReturnDelaySecondsProperty = DependencyProperty.RegisterAttached(
        "FixedReturnDelaySeconds",
        typeof(int),
        typeof(AboveBarSpriteBehavior),
        new PropertyMetadata(60));

    public static void SetIsEnabled(DependencyObject element, bool value)
    {
        element.SetValue(IsEnabledProperty, value);
    }

    public static bool GetIsEnabled(DependencyObject element)
    {
        return (bool)element.GetValue(IsEnabledProperty);
    }

    public static void SetMinReturnDelaySeconds(DependencyObject element, int value)
    {
        element.SetValue(MinReturnDelaySecondsProperty, value);
    }

    public static int GetMinReturnDelaySeconds(DependencyObject element)
    {
        return (int)element.GetValue(MinReturnDelaySecondsProperty);
    }

    public static void SetMaxReturnDelaySeconds(DependencyObject element, int value)
    {
        element.SetValue(MaxReturnDelaySecondsProperty, value);
    }

    public static int GetMaxReturnDelaySeconds(DependencyObject element)
    {
        return (int)element.GetValue(MaxReturnDelaySecondsProperty);
    }

    public static void SetUseRandomReturnDelay(DependencyObject element, bool value)
    {
        element.SetValue(UseRandomReturnDelayProperty, value);
    }

    public static bool GetUseRandomReturnDelay(DependencyObject element)
    {
        return (bool)element.GetValue(UseRandomReturnDelayProperty);
    }

    public static void SetFixedReturnDelaySeconds(DependencyObject element, int value)
    {
        element.SetValue(FixedReturnDelaySecondsProperty, value);
    }

    public static int GetFixedReturnDelaySeconds(DependencyObject element)
    {
        return (int)element.GetValue(FixedReturnDelaySecondsProperty);
    }

    private static void OnIsEnabledChanged(DependencyObject element, DependencyPropertyChangedEventArgs eventArgs)
    {
        if (element is not Image image)
        {
            return;
        }

        if ((bool)eventArgs.NewValue)
        {
            image.MouseLeftButtonDown += OnMouseLeftButtonDown;
            image.Unloaded += OnUnloaded;
        }
        else
        {
            image.MouseLeftButtonDown -= OnMouseLeftButtonDown;
            image.Unloaded -= OnUnloaded;
            StopReturnTimer(image);
            StopOffsetAnimation(image);
        }
    }

    private static void OnMouseLeftButtonDown(object sender, MouseButtonEventArgs eventArgs)
    {
        if (sender is not Image image)
        {
            return;
        }

        eventArgs.Handled = true;
        try
        {
            StopReturnTimer(image);
            Panel.SetZIndex(image, HiddenZIndex);
            AnimateOffset(image, HiddenOffset, HideDuration, 0.1, 0.35, () => StartReturnTimer(image));
        }
        catch
        {
            StopReturnTimer(image);
            StopOffsetAnimation(image);
            Panel.SetZIndex(image, RestingZIndex);
        }
    }

    private static void OnUnloaded(object sender, RoutedEventArgs eventArgs)
    {
        if (sender is Image image)
        {
            StopReturnTimer(image);
            StopOffsetAnimation(image);
        }
    }

    private static int ChooseReturnDelaySeconds(Image image)
    {
        if (!GetUseRandomReturnDelay(image))
        {
            return Math.Max(0, GetFixedReturnDelaySeconds(image));
        }

        var min = Math.Max(0, GetMinReturnDelaySeconds(image));
        var max = Math.Max(min, GetMaxReturnDelaySeconds(image));
        return Random.Shared.Next(min, max + 1);
    }

    private static void StartReturnTimer(Image image)
    {
        StopReturnTimer(image);

        var delaySeconds = ChooseReturnDelaySeconds(image);
        var timer = new DispatcherTimer(DispatcherPriority.Normal, image.Dispatcher)
        {
            Interval = TimeSpan.FromSeconds(delaySeconds)
        };

        timer.Tick += (_, _) =>
        {
            try
            {
                timer.Stop();
                if (ReferenceEquals(image.GetValue(ReturnTimerProperty), timer))
                {
                    image.ClearValue(ReturnTimerProperty);
                }

                AnimateOffset(image, 0, ReturnDuration, 0.35, 0.2, () => Panel.SetZIndex(image, RestingZIndex));
            }
            catch
            {
                StopReturnTimer(image);
                StopOffsetAnimation(image);
                Panel.SetZIndex(image, RestingZIndex);
            }
        };

        image.SetValue(ReturnTimerProperty, timer);
        timer.Start();
    }

    private static void StopReturnTimer(Image image)
    {
        if (image.GetValue(ReturnTimerProperty) is DispatcherTimer timer)
        {
            timer.Stop();
            image.ClearValue(ReturnTimerProperty);
        }
    }

    private static void AnimateOffset(
        Image image,
        double target,
        TimeSpan duration,
        double accelerationRatio,
        double decelerationRatio,
        Action? completed)
    {
        var transform = EnsureTranslateTransform(image);
        var animation = new DoubleAnimation
        {
            To = target,
            Duration = new Duration(duration),
            AccelerationRatio = accelerationRatio,
            DecelerationRatio = decelerationRatio,
            FillBehavior = FillBehavior.HoldEnd
        };

        animation.Completed += (_, _) =>
        {
            try
            {
                transform.BeginAnimation(TranslateTransform.YProperty, null);
                transform.Y = target;
                completed?.Invoke();
            }
            catch
            {
                StopReturnTimer(image);
                StopOffsetAnimation(image);
                Panel.SetZIndex(image, RestingZIndex);
            }
        };

        transform.BeginAnimation(TranslateTransform.YProperty, animation, HandoffBehavior.SnapshotAndReplace);
    }

    private static void StopOffsetAnimation(Image image)
    {
        var transform = EnsureTranslateTransform(image);
        var current = transform.Y;
        transform.BeginAnimation(TranslateTransform.YProperty, null);
        transform.Y = current;
    }

    private static TranslateTransform EnsureTranslateTransform(Image image)
    {
        if (image.RenderTransform is TranslateTransform { IsFrozen: false } translateTransform)
        {
            return translateTransform;
        }

        var currentX = image.RenderTransform is TranslateTransform existingX
            ? existingX.X
            : 0;
        var currentY = image.RenderTransform is TranslateTransform existingY
            ? existingY.Y
            : 0;
        translateTransform = new TranslateTransform
        {
            X = currentX,
            Y = currentY
        };
        image.RenderTransform = translateTransform;
        return translateTransform;
    }
}
