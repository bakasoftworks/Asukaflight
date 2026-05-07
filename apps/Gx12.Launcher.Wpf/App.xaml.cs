using System;
using System.Globalization;
using System.IO;
using System.Windows;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;
using Gx12.Launcher.Wpf.Controls;
using Gx12.Launcher.Wpf.Services;

namespace Gx12.Launcher.Wpf;

public partial class App : Application
{
    protected override void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);

        try
        {
            if (e.Args.Length > 0 && e.Args[0].Equals("--self-test", StringComparison.OrdinalIgnoreCase))
            {
                Shutdown(LauncherDiagnostics.RunSelfTest());
                return;
            }

            if (e.Args.Length > 0 && e.Args[0].Equals("--render-preview", StringComparison.OrdinalIgnoreCase))
            {
                Shutdown(RenderPreview(e.Args));
                return;
            }

            TooltipSpriteBehavior.Register();
            ShutdownMode = ShutdownMode.OnMainWindowClose;
            MainWindow = new MainWindow(CompositionRoot.CreateMainViewModel());
            MainWindow.Show();
        }
        catch (Exception exception)
        {
            TryWriteCrashLog(exception);
            Shutdown(1);
        }
    }

    private static int RenderPreview(string[] args)
    {
        var outputPath = args.Length > 1
            ? Path.GetFullPath(args[1])
            : Path.Combine(CompositionRoot.CreateMainViewModel().RepoRoot, "logs", "wpf-launcher-preview.png");
        var width = args.Length > 2 ? ParsePositiveInt(args[2], 1440) : 1440;
        var height = args.Length > 3 ? ParsePositiveInt(args[3], 850) : 850;
        var dpi = args.Length > 4 ? ParseDpi(args[4], 96) : 96;
        var pixelWidth = Math.Max(1, (int)Math.Round(width * dpi / 96.0));
        var pixelHeight = Math.Max(1, (int)Math.Round(height * dpi / 96.0));

        Directory.CreateDirectory(Path.GetDirectoryName(outputPath) ?? ".");

        var window = new MainWindow(CompositionRoot.CreateMainViewModel());
        window.Width = width;
        window.Height = height;
        window.Left = -32000;
        window.Top = -32000;
        window.WindowStartupLocation = WindowStartupLocation.Manual;
        window.WindowStyle = WindowStyle.None;
        window.ResizeMode = ResizeMode.NoResize;
        window.ShowInTaskbar = false;

        window.Show();
        if (args.Length > 5 && !string.IsNullOrWhiteSpace(args[5]))
        {
            SelectWorkspaceTab(window, args[5]);
        }

        window.Measure(new Size(width, height));
        window.Arrange(new Rect(0, 0, width, height));
        window.UpdateLayout();
        window.Dispatcher.Invoke(() => { }, DispatcherPriority.Render);

        var bitmap = new RenderTargetBitmap(pixelWidth, pixelHeight, dpi, dpi, PixelFormats.Pbgra32);
        bitmap.Render(window);

        var encoder = new PngBitmapEncoder();
        encoder.Frames.Add(BitmapFrame.Create(bitmap));
        using (var stream = File.Create(outputPath))
        {
            encoder.Save(stream);
        }

        window.Close();
        return 0;
    }

    private static void SelectWorkspaceTab(Window window, string header)
    {
        if (window is MainWindow mainWindow)
        {
            mainWindow.SelectWorkspaceTab(header);
        }
    }

    private static int ParsePositiveInt(string value, int fallback)
    {
        return int.TryParse(value, NumberStyles.Integer, CultureInfo.InvariantCulture, out var parsed) && parsed > 0
            ? parsed
            : fallback;
    }

    private static double ParseDpi(string value, double fallback)
    {
        var trimmed = value.Trim();
        if (trimmed.EndsWith("%", StringComparison.Ordinal))
        {
            var percentText = trimmed[..^1];
            return double.TryParse(percentText, NumberStyles.Float, CultureInfo.InvariantCulture, out var percent) && percent > 0
                ? 96.0 * percent / 100.0
                : fallback;
        }

        if (!double.TryParse(trimmed, NumberStyles.Float, CultureInfo.InvariantCulture, out var parsed) || parsed <= 0)
        {
            return fallback;
        }

        return parsed <= 4.0
            ? parsed * 96.0
            : parsed;
    }

    private static void TryWriteCrashLog(Exception exception)
    {
        try
        {
            var paths = new ProfileDirectoryService().Resolve();
            var logPath = Path.Combine(paths.RepoRoot, "logs", "wpf-launcher-crash.log");
            Directory.CreateDirectory(Path.GetDirectoryName(logPath) ?? ".");
            File.AppendAllText(logPath, $"{DateTimeOffset.Now:O} {exception}\r\n");
        }
        catch
        {
        }
    }
}
