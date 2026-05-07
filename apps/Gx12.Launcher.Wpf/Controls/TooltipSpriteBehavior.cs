using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using Gx12.Launcher.Wpf.Services;

namespace Gx12.Launcher.Wpf.Controls;

public static class TooltipSpriteBehavior
{
    private static readonly DependencyProperty SpriteImageProperty = DependencyProperty.RegisterAttached(
        "SpriteImage",
        typeof(Image),
        typeof(TooltipSpriteBehavior),
        new PropertyMetadata(null));

    private static bool _registered;

    public static void Register()
    {
        if (_registered)
        {
            return;
        }

        EventManager.RegisterClassHandler(
            typeof(ToolTip),
            ToolTip.OpenedEvent,
            new RoutedEventHandler(OnToolTipOpened),
            handledEventsToo: true);
        _registered = true;
    }

    private static void OnToolTipOpened(object sender, RoutedEventArgs eventArgs)
    {
        if (sender is not ToolTip toolTip)
        {
            return;
        }

        var sprite = TooltipSpriteService.Shared.LoadRandomSprite();
        var image = EnsureSpriteHost(toolTip);
        image.Source = sprite;
        image.Visibility = sprite is null ? Visibility.Collapsed : Visibility.Visible;
    }

    private static Image EnsureSpriteHost(ToolTip toolTip)
    {
        if (toolTip.GetValue(SpriteImageProperty) is Image existing)
        {
            return existing;
        }

        var originalContent = toolTip.Content;
        toolTip.Content = null;

        var spriteImage = new Image
        {
            MaxWidth = 96,
            MaxHeight = 96,
            Stretch = Stretch.Uniform,
            SnapsToDevicePixels = true,
            VerticalAlignment = VerticalAlignment.Top,
            Margin = new Thickness(0, 0, 10, 0),
            Visibility = Visibility.Collapsed
        };
        RenderOptions.SetBitmapScalingMode(spriteImage, BitmapScalingMode.NearestNeighbor);

        var contentPresenter = new ContentPresenter
        {
            Content = originalContent,
            VerticalAlignment = VerticalAlignment.Top
        };

        var layout = new Grid
        {
            MaxWidth = 520
        };
        layout.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        layout.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        layout.Children.Add(spriteImage);
        Grid.SetColumn(contentPresenter, 1);
        layout.Children.Add(contentPresenter);

        toolTip.Content = layout;
        toolTip.SetValue(SpriteImageProperty, spriteImage);
        return spriteImage;
    }
}
