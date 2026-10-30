// NumberText.cs — finds the number inside a display string ("1,234",
// "87%", "3.5 h", "R 12,480 pts") so a count-up (spec A5) can animate the
// number while keeping the rest of the text and its formatting.

using System.Globalization;
using System.Text.RegularExpressions;

namespace GateControl.Core.Formatting;

public sealed partial class NumberText
{
    private NumberText(string prefix, double value, int decimals, bool grouped, string suffix, CultureInfo culture)
    {
        Prefix = prefix;
        Value = value;
        Decimals = decimals;
        Grouped = grouped;
        Suffix = suffix;
        Culture = culture;
    }

    public string Prefix { get; }
    public double Value { get; }
    public int Decimals { get; }
    public bool Grouped { get; }
    public string Suffix { get; }
    public CultureInfo Culture { get; }

    // Null when the text holds no number (e.g. "—", "Never") or is a clock
    // time or date ("14:00", "17 Apr"), which must not count: show it as is.
    public static NumberText? Parse(string? text, CultureInfo? culture = null)
    {
        if (string.IsNullOrEmpty(text))
        {
            return null;
        }
        culture ??= CultureInfo.CurrentCulture;
        if (Regex.IsMatch(text, @"\d[:/]\d") || Regex.IsMatch(text, @"\b(?:AM|PM|am|pm)\b"))
        {
            return null;
        }
        var nf = culture.NumberFormat;
        var group = Regex.Escape(nf.NumberGroupSeparator);
        var dec = Regex.Escape(nf.NumberDecimalSeparator);
        var m = Regex.Match(text, $@"-?\d{{1,3}}(?:{group}\d{{3}})+(?:{dec}\d+)?|-?\d+(?:{dec}\d+)?");
        if (!m.Success)
        {
            return null;
        }
        var number = m.Value;
        if (!double.TryParse(number, NumberStyles.Number, culture, out var value))
        {
            return null;
        }
        var d = number.IndexOf(nf.NumberDecimalSeparator, StringComparison.Ordinal);
        var decimals = d < 0 ? 0 : number.Length - d - nf.NumberDecimalSeparator.Length;
        var grouped = number.Contains(nf.NumberGroupSeparator, StringComparison.Ordinal);
        return new NumberText(text[..m.Index], value, decimals, grouped, text[(m.Index + m.Length)..], culture);
    }

    // The same text with `value` in place of the number, same decimals and
    // grouping. Values are rounded, so a count-up never shows "1,233.7".
    public string Format(double value)
    {
        var rounded = Math.Round(value, Decimals, MidpointRounding.AwayFromZero);
        var f = (Grouped ? "N" : "F") + Decimals.ToString(CultureInfo.InvariantCulture);
        return Prefix + rounded.ToString(f, Culture) + Suffix;
    }
}
