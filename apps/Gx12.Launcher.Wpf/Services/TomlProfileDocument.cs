using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;

namespace Gx12.Launcher.Wpf.Services;

public sealed class TomlProfileDocument
{
    private static readonly UTF8Encoding Utf8NoBom = new(false);

    private readonly List<string> _lines;
    private Dictionary<string, Dictionary<string, string>> _values;
    private Dictionary<string, Dictionary<string, TomlValueLocation>> _locations;
    private Dictionary<string, TomlSectionLocation> _sections;

    private TomlProfileDocument(List<string> lines)
    {
        _lines = lines;
        _values = new Dictionary<string, Dictionary<string, string>>(StringComparer.OrdinalIgnoreCase);
        _locations = new Dictionary<string, Dictionary<string, TomlValueLocation>>(StringComparer.OrdinalIgnoreCase);
        _sections = new Dictionary<string, TomlSectionLocation>(StringComparer.OrdinalIgnoreCase);
        Reparse();
    }

    public static TomlProfileDocument Load(string path)
    {
        return LoadText(File.ReadAllText(path));
    }

    public static TomlProfileDocument LoadText(string text)
    {
        return new TomlProfileDocument(SplitLines(text));
    }

    public string GetRaw(string section, string key, string defaultValue = "")
    {
        return _values.TryGetValue(section, out var sectionValues) &&
               sectionValues.TryGetValue(key, out var value)
            ? value
            : defaultValue;
    }

    public bool Contains(string section, string key)
    {
        return _values.TryGetValue(section, out var sectionValues) &&
               sectionValues.ContainsKey(key);
    }

    public IReadOnlyList<string> GetValuePaths()
    {
        return _values
            .SelectMany(section => section.Value.Keys.Select(key => $"{section.Key}.{key}"))
            .OrderBy(path => path, StringComparer.OrdinalIgnoreCase)
            .ToList();
    }

    public string GetString(string section, string key, string defaultValue = "")
    {
        return Unquote(GetRaw(section, key, defaultValue));
    }

    public bool GetBool(string section, string key, bool defaultValue = false)
    {
        var raw = GetRaw(section, key, defaultValue ? "true" : "false");
        return raw.Equals("true", StringComparison.OrdinalIgnoreCase);
    }

    public bool SetRaw(string section, string key, string rawValue)
    {
        ValidateSectionAndKey(section, key);
        if (rawValue.IndexOfAny(new[] { '\r', '\n' }) >= 0)
        {
            throw new ArgumentException("TOML value must be a single-line raw value.", nameof(rawValue));
        }

        if (_locations.TryGetValue(section, out var sectionLocations) &&
            sectionLocations.TryGetValue(key, out var location))
        {
            var line = _lines[location.LineIndex];
            var contentEnd = GetContentEnd(line);
            var content = line[..contentEnd];
            var ending = line[contentEnd..];
            var currentValue = content[location.ValueStart..location.ValueEnd];

            if (currentValue.Equals(rawValue, StringComparison.Ordinal))
            {
                return false;
            }

            _lines[location.LineIndex] =
                content[..location.ValueStart] +
                rawValue +
                content[location.ValueEnd..] +
                ending;
            Reparse();
            return true;
        }

        InsertValue(section, key, rawValue);
        Reparse();
        return true;
    }

    public bool SetString(string section, string key, string value)
    {
        return SetRaw(section, key, QuoteString(value));
    }

    public bool SetBool(string section, string key, bool value)
    {
        return SetRaw(section, key, value ? "true" : "false");
    }

    public string ToText()
    {
        return string.Concat(_lines);
    }

    public void Save(string path)
    {
        File.WriteAllText(path, ToText(), Utf8NoBom);
    }

    public static string QuoteString(string value)
    {
        return "\"" +
               value
                   .Replace("\\", "\\\\", StringComparison.Ordinal)
                   .Replace("\"", "\\\"", StringComparison.Ordinal) +
               "\"";
    }

    private void InsertValue(string section, string key, string rawValue)
    {
        var newline = DetectNewline();
        var newLine = $"{key} = {rawValue}{newline}";

        if (_sections.TryGetValue(section, out var sectionLocation))
        {
            _lines.Insert(sectionLocation.EndLineIndex, newLine);
            return;
        }

        EnsureTrailingNewline(newline);
        if (_lines.Count > 0 && !string.IsNullOrWhiteSpace(WithoutLineEnding(_lines[^1])))
        {
            _lines.Add(newline);
        }

        _lines.Add($"[{section}]{newline}");
        _lines.Add(newLine);
    }

    private void EnsureTrailingNewline(string newline)
    {
        if (_lines.Count == 0)
        {
            return;
        }

        var last = _lines[^1];
        if (!EndsWithLineEnding(last))
        {
            _lines[^1] = last + newline;
        }
    }

    private void Reparse()
    {
        var values = new Dictionary<string, Dictionary<string, string>>(StringComparer.OrdinalIgnoreCase);
        var locations = new Dictionary<string, Dictionary<string, TomlValueLocation>>(StringComparer.OrdinalIgnoreCase);
        var sectionStarts = new List<TomlSectionStart>();
        var section = string.Empty;

        for (var lineIndex = 0; lineIndex < _lines.Count; lineIndex++)
        {
            var line = WithoutLineEnding(_lines[lineIndex]);
            var stripped = StripInlineComment(line).Trim();
            if (stripped.Length == 0)
            {
                continue;
            }

            if (stripped.StartsWith("[", StringComparison.Ordinal) &&
                stripped.EndsWith("]", StringComparison.Ordinal) &&
                stripped.Length > 2)
            {
                section = stripped[1..^1].Trim();
                if (!values.ContainsKey(section))
                {
                    values[section] = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
                    locations[section] = new Dictionary<string, TomlValueLocation>(StringComparer.OrdinalIgnoreCase);
                }

                sectionStarts.Add(new TomlSectionStart(section, lineIndex));
                continue;
            }

            var equalsIndex = FindEquals(line);
            if (equalsIndex <= 0)
            {
                continue;
            }

            var key = line[..equalsIndex].Trim();
            if (key.Length == 0)
            {
                continue;
            }

            var valueStart = equalsIndex + 1;
            while (valueStart < line.Length && IsHorizontalWhitespace(line[valueStart]))
            {
                valueStart++;
            }

            var valueEndLimit = FindInlineCommentStart(line);
            if (valueEndLimit < 0)
            {
                valueEndLimit = line.Length;
            }

            var valueEnd = valueEndLimit;
            while (valueEnd > valueStart && IsHorizontalWhitespace(line[valueEnd - 1]))
            {
                valueEnd--;
            }

            if (!values.TryGetValue(section, out var sectionValues))
            {
                sectionValues = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
                values[section] = sectionValues;
            }

            if (!locations.TryGetValue(section, out var sectionLocations))
            {
                sectionLocations = new Dictionary<string, TomlValueLocation>(StringComparer.OrdinalIgnoreCase);
                locations[section] = sectionLocations;
            }

            sectionValues[key] = line[valueStart..valueEnd];
            sectionLocations[key] = new TomlValueLocation(lineIndex, valueStart, valueEnd);
        }

        var sections = new Dictionary<string, TomlSectionLocation>(StringComparer.OrdinalIgnoreCase);
        for (var index = 0; index < sectionStarts.Count; index++)
        {
            var start = sectionStarts[index];
            var endLineIndex = index + 1 < sectionStarts.Count
                ? sectionStarts[index + 1].LineIndex
                : _lines.Count;
            sections[start.Name] = new TomlSectionLocation(start.LineIndex, endLineIndex);
        }

        _values = values;
        _locations = locations;
        _sections = sections;
    }

    private string DetectNewline()
    {
        foreach (var line in _lines)
        {
            if (line.EndsWith("\r\n", StringComparison.Ordinal))
            {
                return "\r\n";
            }

            if (line.EndsWith("\n", StringComparison.Ordinal))
            {
                return "\n";
            }

            if (line.EndsWith("\r", StringComparison.Ordinal))
            {
                return "\r";
            }
        }

        return Environment.NewLine;
    }

    private static List<string> SplitLines(string text)
    {
        var lines = new List<string>();
        var start = 0;

        for (var index = 0; index < text.Length; index++)
        {
            if (text[index] == '\r')
            {
                var end = index + 1;
                if (end < text.Length && text[end] == '\n')
                {
                    end++;
                }

                lines.Add(text[start..end]);
                start = end;
                index = end - 1;
            }
            else if (text[index] == '\n')
            {
                var end = index + 1;
                lines.Add(text[start..end]);
                start = end;
            }
        }

        if (start < text.Length)
        {
            lines.Add(text[start..]);
        }

        return lines;
    }

    private static string WithoutLineEnding(string line)
    {
        var contentEnd = GetContentEnd(line);
        return line[..contentEnd];
    }

    private static int GetContentEnd(string line)
    {
        if (line.EndsWith("\r\n", StringComparison.Ordinal))
        {
            return line.Length - 2;
        }

        if (line.EndsWith("\n", StringComparison.Ordinal) ||
            line.EndsWith("\r", StringComparison.Ordinal))
        {
            return line.Length - 1;
        }

        return line.Length;
    }

    private static bool EndsWithLineEnding(string line)
    {
        return line.EndsWith("\r\n", StringComparison.Ordinal) ||
               line.EndsWith("\n", StringComparison.Ordinal) ||
               line.EndsWith("\r", StringComparison.Ordinal);
    }

    private static void ValidateSectionAndKey(string section, string key)
    {
        if (string.IsNullOrWhiteSpace(section))
        {
            throw new ArgumentException("TOML section is required.", nameof(section));
        }

        if (string.IsNullOrWhiteSpace(key))
        {
            throw new ArgumentException("TOML key is required.", nameof(key));
        }
    }

    private static int FindEquals(string line)
    {
        return FindOutsideString(line, '=');
    }

    private static int FindInlineCommentStart(string line)
    {
        return FindOutsideString(line, '#');
    }

    private static int FindOutsideString(string line, char needle)
    {
        var inString = false;
        var escaped = false;

        for (var index = 0; index < line.Length; index++)
        {
            var current = line[index];
            if (current == '"' && !escaped)
            {
                inString = !inString;
            }

            if (current == needle && !inString)
            {
                return index;
            }

            escaped = inString && current == '\\' && !escaped;
            if (current != '\\')
            {
                escaped = false;
            }
        }

        return -1;
    }

    private static bool IsHorizontalWhitespace(char value)
    {
        return value is ' ' or '\t';
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

    private static string StripInlineComment(string line)
    {
        var commentIndex = FindInlineCommentStart(line);
        return commentIndex >= 0 ? line[..commentIndex] : line;
    }

    private sealed record TomlValueLocation(int LineIndex, int ValueStart, int ValueEnd);

    private sealed record TomlSectionStart(string Name, int LineIndex);

    private sealed record TomlSectionLocation(int LineIndex, int EndLineIndex);
}
