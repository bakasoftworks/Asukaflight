using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using Gx12.Launcher.Wpf.Models;

namespace Gx12.Launcher.Wpf.Services;

public static class StickShapeCurve
{
    public const double MinWidth = 0.05;
    public const double MaxWidth = 1.0;
    public const double DefaultWidth = 0.25;

    private static readonly Regex NodeRegex = new(
        @"\[\s*(?<x>[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?)\s*,\s*(?<y>[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?)\s*,\s*(?<w>[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?)\s*\]",
        RegexOptions.CultureInvariant | RegexOptions.Compiled);

    public static IReadOnlyList<StickShapeNode> ParseNodes(string? text)
    {
        if (string.IsNullOrWhiteSpace(text))
        {
            return Array.Empty<StickShapeNode>();
        }

        var nodes = new List<StickShapeNode>();
        foreach (Match match in NodeRegex.Matches(text))
        {
            if (!TryParse(match.Groups["x"].Value, out var x) ||
                !TryParse(match.Groups["y"].Value, out var y) ||
                !TryParse(match.Groups["w"].Value, out var width))
            {
                continue;
            }

            nodes.Add(new StickShapeNode(Clamp01(x), Clamp01(y), ClampWidth(width)));
        }

        return nodes;
    }

    public static string FormatNodes(IEnumerable<StickShapeNode> nodes)
    {
        var list = nodes.ToList();
        if (list.Count == 0)
        {
            return "[]";
        }

        var builder = new StringBuilder();
        builder.Append('[');
        for (var index = 0; index < list.Count; index++)
        {
            if (index > 0)
            {
                builder.Append(", ");
            }

            var node = list[index];
            builder.AppendFormat(
                CultureInfo.InvariantCulture,
                "[{0:0.###},{1:0.###},{2:0.###}]",
                Clamp01(node.X),
                Clamp01(node.Y),
                ClampWidth(node.Width));
        }

        builder.Append(']');
        return builder.ToString();
    }

    public static string DescribeNodes(string? text)
    {
        var nodes = ParseNodes(text);
        if (nodes.Count == 0)
        {
            return "linear";
        }

        return $"{nodes.Count} node{(nodes.Count == 1 ? "" : "s")} / {FormatNodes(nodes)}";
    }

    public static double Evaluate(double t, IReadOnlyList<StickShapeNode> nodes)
    {
        t = Clamp01(t);
        if (nodes.Count == 0)
        {
            return t;
        }

        var sumK = 0.0;
        var sumKy = 0.0;
        var maxK = 0.0;
        foreach (var node in nodes)
        {
            var width = ClampWidth(node.Width);
            var dx = Math.Abs(t - Clamp01(node.X));
            if (dx >= width)
            {
                continue;
            }

            var k = 0.5 * (1.0 + Math.Cos((Math.PI * dx) / width));
            var y = Clamp01(node.Y);
            sumK += k;
            sumKy += k * y;
            maxK = Math.Max(maxK, k);
        }

        if (sumK <= 0.0)
        {
            return t;
        }

        var weightedY = sumKy / sumK;
        var blend = Clamp01(maxK);
        return Clamp01((blend * weightedY) + ((1.0 - blend) * t));
    }

    public static double Clamp01(double value)
    {
        return Math.Max(0.0, Math.Min(1.0, value));
    }

    public static double ClampWidth(double value)
    {
        return Math.Max(MinWidth, Math.Min(MaxWidth, value));
    }

    private static bool TryParse(string value, out double parsed)
    {
        return double.TryParse(value, NumberStyles.Float, CultureInfo.InvariantCulture, out parsed);
    }
}
