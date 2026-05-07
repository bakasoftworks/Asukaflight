using System;
using System.Globalization;
using Gx12.Launcher.Wpf.Services;

namespace Gx12.Launcher.Wpf.Models;

public sealed class SettingCatalogRow
{
    public SettingCatalogRow(
        ProfileSettingDefinition definition,
        TomlProfileDocument document,
        TooltipImageInfo? tooltipImage = null)
    {
        Definition = definition;
        IsPresentInProfile = document.Contains(definition.Section, definition.Key);
        RawValue = document.GetRaw(definition.Section, definition.Key, definition.DefaultRawValue).Trim();
        DisplayValue = FormatValue(definition, RawValue);
        DefaultDisplayValue = FormatValue(definition, definition.DefaultRawValue);
        IsChangedFromDefault = !ValuesEquivalent(definition, RawValue, definition.DefaultRawValue);
        IsInvalid = !IsRawValueValid(definition, RawValue);
        SearchText = $"{definition.SearchText} {RawValue} {DisplayValue} {DefaultDisplayValue}";
        TooltipImageRelativePath = tooltipImage?.RelativePath ?? "";
        TooltipImageFullPath = tooltipImage?.FullPath ?? "";
        TooltipImagePath = tooltipImage is { Exists: true } ? tooltipImage.FullPath : "";
        IsTooltipImageMissing = tooltipImage is { Exists: false };
    }

    public ProfileSettingDefinition Definition { get; }

    public string RawValue { get; }

    public string DisplayValue { get; }

    public string DefaultDisplayValue { get; }

    public bool IsPresentInProfile { get; }

    public bool IsChangedFromDefault { get; }

    public bool IsInvalid { get; }

    public string SearchText { get; }

    public string TooltipImageRelativePath { get; }

    public string TooltipImageFullPath { get; }

    public string TooltipImagePath { get; }

    public bool HasTooltipImage => !string.IsNullOrWhiteSpace(TooltipImagePath);

    public bool HasTooltipImageMapping => !string.IsNullOrWhiteSpace(TooltipImageRelativePath);

    public bool IsTooltipImageMissing { get; }

    public string Label => Definition.Label;

    public string TomlPath => Definition.TomlPath;

    public string Group => Definition.Group;

    public string Page => Definition.Page;

    public string TierLabel => Definition.TierLabel;

    public string KindLabel => Definition.Kind.ToString();

    public string Help => Definition.Help;

    public string RangeText => Definition.RangeText;

    public string DetailText => Definition.DetailText;

    public string RiskText => Definition.RiskText;

    public bool HasDetailText => !string.IsNullOrWhiteSpace(DetailText);

    public bool HasRiskText => !string.IsNullOrWhiteSpace(RiskText);

    public string AllowedValuesText => Definition.AllowedValuesText;

    public string TooltipImageBadge
    {
        get
        {
            if (HasTooltipImage)
            {
                return "Image";
            }

            return IsTooltipImageMissing ? "Missing" : "-";
        }
    }

    public string TooltipImageStatus
    {
        get
        {
            if (HasTooltipImage)
            {
                return $"Image: {TooltipImageRelativePath}";
            }

            return IsTooltipImageMissing
                ? $"Mapped image is missing: {TooltipImageRelativePath}"
                : "No tooltip image attached.";
        }
    }

    public string StatusLabel
    {
        get
        {
            if (IsInvalid)
            {
                return "Invalid";
            }

            if (IsChangedFromDefault)
            {
                return "Changed";
            }

            return IsPresentInProfile ? "Default" : "Implicit";
        }
    }

    public bool MatchesSearch(string searchText)
    {
        return string.IsNullOrWhiteSpace(searchText) ||
               SearchText.Contains(searchText, StringComparison.OrdinalIgnoreCase);
    }

    private static string FormatValue(ProfileSettingDefinition definition, string rawValue)
    {
        var value = rawValue.Trim();
        return definition.Kind is SettingKind.String or SettingKind.Enum
            ? Unquote(value)
            : value;
    }

    private static bool ValuesEquivalent(ProfileSettingDefinition definition, string left, string right)
    {
        if (definition.Kind is SettingKind.Number or SettingKind.Integer)
        {
            return double.TryParse(left, NumberStyles.Float, CultureInfo.InvariantCulture, out var leftNumber) &&
                   double.TryParse(right, NumberStyles.Float, CultureInfo.InvariantCulture, out var rightNumber) &&
                   Math.Abs(leftNumber - rightNumber) < 0.0000001;
        }

        if (definition.Kind == SettingKind.Boolean)
        {
            return NormalizeBool(left).Equals(NormalizeBool(right), StringComparison.Ordinal);
        }

        if (definition.Kind is SettingKind.String or SettingKind.Enum)
        {
            return Unquote(left).Equals(Unquote(right), StringComparison.OrdinalIgnoreCase);
        }

        return left.Trim().Equals(right.Trim(), StringComparison.Ordinal);
    }

    private static bool IsRawValueValid(ProfileSettingDefinition definition, string rawValue)
    {
        var value = rawValue.Trim();
        switch (definition.Kind)
        {
            case SettingKind.Boolean:
                return NormalizeBool(value) is "true" or "false";
            case SettingKind.Integer:
                return int.TryParse(value, NumberStyles.Integer, CultureInfo.InvariantCulture, out _);
            case SettingKind.Number:
                return double.TryParse(value, NumberStyles.Float, CultureInfo.InvariantCulture, out _);
            case SettingKind.Enum:
                var unquoted = Unquote(value);
                foreach (var allowedValue in definition.AllowedValues)
                {
                    if (unquoted.Equals(allowedValue, StringComparison.OrdinalIgnoreCase))
                    {
                        return true;
                    }
                }

                return definition.AllowedValues.Count == 0;
            case SettingKind.Array:
                return value.StartsWith("[", StringComparison.Ordinal) &&
                       value.EndsWith("]", StringComparison.Ordinal);
            default:
                return true;
        }
    }

    private static string NormalizeBool(string value)
    {
        return Unquote(value).Trim().ToLowerInvariant();
    }

    private static string Unquote(string value)
    {
        var trimmed = value.Trim();
        if (trimmed.Length >= 2 &&
            trimmed.StartsWith("\"", StringComparison.Ordinal) &&
            trimmed.EndsWith("\"", StringComparison.Ordinal))
        {
            return trimmed[1..^1]
                .Replace("\\\"", "\"", StringComparison.Ordinal)
                .Replace("\\\\", "\\", StringComparison.Ordinal);
        }

        return trimmed;
    }
}
