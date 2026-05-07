using System;
using System.Collections.Generic;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Data;
using System.Windows.Media;
using System.Windows.Media.Media3D;
using Gx12.Launcher.Wpf.Services;

namespace Gx12.Launcher.Wpf.Controls;

public static class OptionToolTips
{
    private delegate bool TooltipResolver(string? bindingPath, out OptionTooltipInfo tooltip);

    public static readonly DependencyProperty IsEnabledProperty = DependencyProperty.RegisterAttached(
        "IsEnabled",
        typeof(bool),
        typeof(OptionToolTips),
        new PropertyMetadata(false, OnIsEnabledChanged));

    public static bool GetIsEnabled(DependencyObject element)
    {
        return (bool)element.GetValue(IsEnabledProperty);
    }

    public static void SetIsEnabled(DependencyObject element, bool value)
    {
        element.SetValue(IsEnabledProperty, value);
    }

    private static void OnIsEnabledChanged(DependencyObject dependencyObject, DependencyPropertyChangedEventArgs eventArgs)
    {
        if (dependencyObject is not FrameworkElement element)
        {
            return;
        }

        if (eventArgs.NewValue is true)
        {
            element.Loaded -= ApplyOnLoaded;
            element.Loaded += ApplyOnLoaded;
            if (element.IsLoaded)
            {
                ApplyToTree(element);
            }
        }
        else
        {
            element.Loaded -= ApplyOnLoaded;
        }
    }

    private static void ApplyOnLoaded(object sender, RoutedEventArgs eventArgs)
    {
        if (sender is DependencyObject root)
        {
            ApplyToTree(root);
        }
    }

    private static void ApplyToTree(DependencyObject root)
    {
        var visited = new HashSet<DependencyObject>();
        foreach (var element in EnumerateSelfAndDescendants(root, visited))
        {
            if (element is FrameworkElement frameworkElement)
            {
                ApplyToElement(frameworkElement);
            }
        }
    }

    private static void ApplyToElement(FrameworkElement element)
    {
        if (TryResolveElementTooltip(element, out var tooltip) ||
            TryResolveLabelTooltip(element, out tooltip))
        {
            element.ToolTip = CreateToolTip(tooltip);
            ToolTipService.SetShowOnDisabled(element, true);
            ToolTipService.SetShowDuration(element, 30000);
        }
    }

    private static bool TryResolveElementTooltip(FrameworkElement element, out OptionTooltipInfo tooltip)
    {
        switch (element)
        {
            case TextBox textBox when TryResolveBoundTooltip(
                textBox,
                TextBox.TextProperty,
                OptionTooltipCatalog.TryGetTooltipForBindingPath,
                out tooltip):
                return true;
            case ComboBox comboBox when TryResolveBoundTooltip(
                comboBox,
                Selector.SelectedValueProperty,
                OptionTooltipCatalog.TryGetTooltipForBindingPath,
                out tooltip):
                return true;
            case CheckBox checkBox when TryResolveBoundTooltip(
                checkBox,
                ToggleButton.IsCheckedProperty,
                OptionTooltipCatalog.TryGetTooltipForBindingPath,
                out tooltip):
                return true;
            case StickShapeEditor editor when TryResolveBoundTooltip(
                editor,
                StickShapeEditor.NodesTextProperty,
                OptionTooltipCatalog.TryGetTooltipForBindingPath,
                out tooltip):
                return true;
            case ButtonBase button when TryResolveBoundTooltip(
                button,
                ButtonBase.CommandProperty,
                OptionTooltipCatalog.TryGetTooltipForCommandPath,
                out tooltip):
                return true;
            default:
                tooltip = default!;
                return false;
        }
    }

    private static bool TryResolveLabelTooltip(FrameworkElement element, out OptionTooltipInfo tooltip)
    {
        tooltip = default!;
        if (element is not TextBlock || LogicalTreeHelper.GetParent(element) is not Grid grid)
        {
            return false;
        }

        var row = Grid.GetRow(element);
        var column = Grid.GetColumn(element);
        foreach (var sibling in grid.Children)
        {
            if (sibling is not FrameworkElement siblingElement ||
                ReferenceEquals(siblingElement, element) ||
                Grid.GetRow(siblingElement) != row ||
                Grid.GetColumn(siblingElement) <= column)
            {
                continue;
            }

            if (TryResolveElementTooltip(siblingElement, out tooltip))
            {
                return true;
            }
        }

        return false;
    }

    private static bool TryResolveBoundTooltip(
        FrameworkElement element,
        DependencyProperty property,
        TooltipResolver resolver,
        out OptionTooltipInfo tooltip)
    {
        var binding = BindingOperations.GetBinding(element, property);
        return resolver(binding?.Path?.Path, out tooltip);
    }

    private static ToolTip CreateToolTip(OptionTooltipInfo tooltip)
    {
        var panel = new StackPanel
        {
            MaxWidth = 390
        };

        panel.Children.Add(new TextBlock
        {
            Text = tooltip.Title,
            FontWeight = FontWeights.SemiBold,
            FontSize = 13,
            Margin = new Thickness(0, 0, 0, 6),
            TextWrapping = TextWrapping.Wrap
        });

        AddText(panel, tooltip.Body, "TextBrush", 0, FontWeights.Normal);
        AddText(panel, tooltip.Detail, "TextMutedBrush", 7, FontWeights.Normal);
        AddText(panel, tooltip.Risk, "WarnBrush", 7, FontWeights.SemiBold);
        AddText(panel, tooltip.Footer, "InfoBrush", 7, FontWeights.Normal, "Consolas", 11);

        return new ToolTip
        {
            Content = panel
        };
    }

    private static void AddText(
        Panel panel,
        string text,
        string brushResource,
        double topMargin,
        FontWeight fontWeight,
        string? fontFamily = null,
        double fontSize = 12)
    {
        if (string.IsNullOrWhiteSpace(text))
        {
            return;
        }

        var block = new TextBlock
        {
            Text = text,
            Foreground = FindBrush(brushResource),
            FontWeight = fontWeight,
            FontSize = fontSize,
            Margin = new Thickness(0, topMargin, 0, 0),
            TextWrapping = TextWrapping.Wrap
        };

        if (!string.IsNullOrWhiteSpace(fontFamily))
        {
            block.FontFamily = new FontFamily(fontFamily);
        }

        panel.Children.Add(block);
    }

    private static Brush FindBrush(string resourceKey)
    {
        return Application.Current?.TryFindResource(resourceKey) as Brush ?? Brushes.White;
    }

    private static IEnumerable<DependencyObject> EnumerateSelfAndDescendants(
        DependencyObject root,
        ISet<DependencyObject> visited)
    {
        if (!visited.Add(root))
        {
            yield break;
        }

        yield return root;

        foreach (var logicalChild in LogicalTreeHelper.GetChildren(root))
        {
            if (logicalChild is DependencyObject logicalDependencyObject)
            {
                foreach (var descendant in EnumerateSelfAndDescendants(logicalDependencyObject, visited))
                {
                    yield return descendant;
                }
            }
        }

        var visualChildrenCount = root is Visual or Visual3D
            ? VisualTreeHelper.GetChildrenCount(root)
            : 0;
        for (var index = 0; index < visualChildrenCount; index++)
        {
            var visualChild = VisualTreeHelper.GetChild(root, index);
            foreach (var descendant in EnumerateSelfAndDescendants(visualChild, visited))
            {
                yield return descendant;
            }
        }
    }
}
