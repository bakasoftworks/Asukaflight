using System;
using System.Collections.Generic;
using System.Linq;

namespace Gx12.Launcher.Wpf.Services;

public sealed record OptionTooltipInfo(
    string Title,
    string Body,
    string Detail = "",
    string Risk = "",
    string Footer = "")
{
    public string Text => string.Join(
        $"{Environment.NewLine}{Environment.NewLine}",
        new[] { Title, Body, Detail, Risk, Footer }.Where(value => !string.IsNullOrWhiteSpace(value)));
}
