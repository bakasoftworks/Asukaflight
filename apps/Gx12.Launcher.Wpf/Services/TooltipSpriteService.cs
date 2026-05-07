using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Windows.Media.Imaging;

namespace Gx12.Launcher.Wpf.Services;

public sealed class TooltipSpriteService
{
    private readonly object _sync = new();
    private readonly Random _random = new();
    private string _spriteDirectory = "";

    public static TooltipSpriteService Shared { get; } = new();

    public string SpriteDirectory
    {
        get
        {
            lock (_sync)
            {
                return _spriteDirectory;
            }
        }
    }

    public void Configure(string spriteDirectory, bool ensureDirectory = false)
    {
        var resolved = string.IsNullOrWhiteSpace(spriteDirectory)
            ? ""
            : Path.GetFullPath(spriteDirectory);

        lock (_sync)
        {
            _spriteDirectory = resolved;
        }

        if (ensureDirectory && !string.IsNullOrWhiteSpace(resolved))
        {
            Directory.CreateDirectory(resolved);
        }
    }

    public int CountSprites()
    {
        return GetSpritePaths().Count;
    }

    public IReadOnlyList<string> GetSpritePaths()
    {
        return EnumerateSpritePaths(SpriteDirectory);
    }

    public BitmapImage? LoadRandomSprite()
    {
        var spritePath = ChooseRandomSpritePath();
        if (string.IsNullOrWhiteSpace(spritePath))
        {
            return null;
        }

        try
        {
            var bitmap = new BitmapImage();
            bitmap.BeginInit();
            bitmap.CacheOption = BitmapCacheOption.OnLoad;
            bitmap.CreateOptions = BitmapCreateOptions.IgnoreImageCache;
            bitmap.UriSource = new Uri(spritePath, UriKind.Absolute);
            bitmap.EndInit();
            bitmap.Freeze();
            return bitmap;
        }
        catch
        {
            return null;
        }
    }

    public static IReadOnlyList<string> EnumerateSpritePaths(string spriteDirectory)
    {
        if (string.IsNullOrWhiteSpace(spriteDirectory) || !Directory.Exists(spriteDirectory))
        {
            return Array.Empty<string>();
        }

        try
        {
            return Directory
                .EnumerateFiles(spriteDirectory, "*.png", SearchOption.TopDirectoryOnly)
                .OrderBy(Path.GetFileName, StringComparer.OrdinalIgnoreCase)
                .ToList();
        }
        catch
        {
            return Array.Empty<string>();
        }
    }

    private string? ChooseRandomSpritePath()
    {
        var sprites = GetSpritePaths();
        if (sprites.Count == 0)
        {
            return null;
        }

        lock (_sync)
        {
            return sprites[_random.Next(sprites.Count)];
        }
    }
}
