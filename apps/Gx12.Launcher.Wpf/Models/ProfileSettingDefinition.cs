using System;
using System.Collections.Generic;
using System.Linq;

namespace Gx12.Launcher.Wpf.Models;

public sealed record ProfileSettingDefinition(
    string Id,
    string Section,
    string Key,
    string Label,
    SettingKind Kind,
    SettingTier Tier,
    string Group,
    string Page,
    string DefaultRawValue,
    string Help,
    string RangeText,
    IReadOnlyList<string> AllowedValues)
{
    public string TomlPath => $"{Section}.{Key}";

    public string TierLabel => Tier == SettingTier.Experimental
        ? "Tuning"
        : Tier.ToString();

    public string AllowedValuesText => AllowedValues.Count == 0
        ? ""
        : string.Join(", ", AllowedValues);

    public string SearchText => string.Join(
        " ",
        new[]
        {
            Id,
            Section,
            Key,
            Label,
            Group,
            Page,
            TierLabel,
            Kind.ToString(),
            Help,
            RangeText,
            AllowedValuesText,
            DetailText,
            RiskText
        }.Where(value => !string.IsNullOrWhiteSpace(value)));

    public string DetailText
    {
        get
        {
            if (!string.IsNullOrWhiteSpace(RangeText) && AllowedValues.Count > 0)
            {
                return $"Range: {RangeText}. Values: {AllowedValuesText}.";
            }

            if (!string.IsNullOrWhiteSpace(RangeText))
            {
                return $"Range: {RangeText}.";
            }

            if (AllowedValues.Count > 0)
            {
                return $"Values: {AllowedValuesText}.";
            }

            return "";
        }
    }

    public string RiskText
    {
        get
        {
            if (Group.Contains("Safety", StringComparison.OrdinalIgnoreCase))
            {
                return "Safety-visible setting. Recheck before live use.";
            }

            if (Group.Contains("Second Mouse", StringComparison.OrdinalIgnoreCase) ||
                Group.Contains("Keyboard", StringComparison.OrdinalIgnoreCase) ||
                Group.Contains("Devices", StringComparison.OrdinalIgnoreCase))
            {
                return "Input-routing setting. Avoid enabling conflicting left-stick sources.";
            }

            if (Tier == SettingTier.Experimental)
            {
                return "Tuning setting. Verify major changes in VelociDrone before real flight.";
            }

            return "";
        }
    }

    public bool MatchesSearch(string searchText)
    {
        return string.IsNullOrWhiteSpace(searchText) ||
               SearchText.Contains(searchText, StringComparison.OrdinalIgnoreCase);
    }
}
