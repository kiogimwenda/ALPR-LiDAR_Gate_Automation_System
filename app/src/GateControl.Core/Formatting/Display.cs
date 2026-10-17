// Display.cs — wire names → words people read.
//
// The backend speaks enum short names ("LOW_CONFIDENCE", "SUV",
// "CLASS_MISMATCH"); the app never shows those raw. Tone maps a state
// onto the semantic colour set (Theme.xaml: Positive / Negative /
// Caution / Neutral / Accent) so meaning, not hue, is decided here.

using System.Globalization;

namespace GateControl.Core.Formatting;

public enum Tone
{
    Neutral,
    Positive,
    Caution,
    Negative,
    Accent,
}

public static class Display
{
    public static string Verdict(string v) => v switch
    {
        "AUTHORIZED" => "Allowed",
        "DENIED" => "Denied",
        "LOW_CONFIDENCE" => "Unclear read",
        "MANUAL_REVIEW" => "Needs review",
        _ => "Unknown",
    };

    public static Tone VerdictTone(string v) => v switch
    {
        "AUTHORIZED" => Tone.Positive,
        "DENIED" => Tone.Negative,
        "LOW_CONFIDENCE" or "MANUAL_REVIEW" => Tone.Caution,
        _ => Tone.Neutral,
    };

    public static string Reason(string r) => r switch
    {
        "" or "NONE" => "",
        "BLOCKLISTED" => "Blocklisted plate",
        "NOT_ON_ALLOWLIST" => "Not registered",
        "OUTSIDE_WINDOW" => "Outside allowed hours",
        "FORCE_CLOSE" => "Gate in lockdown",
        "LOW_ALPR_CONF" => "Plate hard to read",
        "LOW_LIDAR_CONF" => "Vehicle not confirmed",
        "CLASS_MISMATCH" => "Different vehicle type",
        "NO_PLATE_FOUND" => "No plate found",
        "DUPLICATE_ENTRY" => "Tailgating",
        _ => Title(r),
    };

    public static string VehicleClass(string c) => c switch
    {
        "SEDAN" => "Car",
        "SUV" => "SUV",
        "PICKUP" => "Pickup",
        "VAN" => "Van",
        "TRUCK" => "Truck",
        "MOTORCYCLE" => "Motorcycle",
        "BICYCLE" => "Bicycle",
        "PEDESTRIAN" => "Pedestrian",
        "" or "UNKNOWN" => "Unknown",
        _ => Title(c),
    };

    // Vehicle classes an allowlist entry can be restricted to, in the
    // order the editor shows them.
    public static readonly IReadOnlyList<string> RestrictableClasses =
        ["SEDAN", "SUV", "PICKUP", "VAN", "TRUCK", "MOTORCYCLE"];

    public static string GateState(string s) => s switch
    {
        "CLOSED" => "Closed",
        "OPENING" => "Opening",
        "OPEN" => "Open",
        "CLOSING" => "Closing",
        "FAULT" => "Fault",
        "LOCKDOWN" => "Lockdown",
        _ => "Unknown",
    };

    public static Tone GateStateTone(string s) => s switch
    {
        "OPEN" => Tone.Positive,
        "OPENING" or "CLOSING" => Tone.Accent,
        "FAULT" => Tone.Negative,
        "LOCKDOWN" => Tone.Caution,
        _ => Tone.Neutral,
    };

    public static string Severity(string s) => s switch
    {
        "critical" => "Critical",
        "high" => "High",
        "medium" => "Medium",
        _ => "Low",
    };

    public static Tone SeverityTone(string s) => s switch
    {
        "critical" or "high" => Tone.Negative,
        "medium" => Tone.Caution,
        _ => Tone.Neutral,
    };

    public static string AlertKind(string k) => k switch
    {
        "blocklisted" => "Blocklisted plate",
        "class_mismatch" => "Vehicle mismatch",
        "fault" => "Gate fault",
        "command_failed" => "Command failed",
        _ => Title(k),
    };

    public static string Role(string r) => r switch
    {
        "admin" => "Administrator",
        "operator" => "Operator",
        _ => "Viewer",
    };

    public static string Direction(string d) => d switch
    {
        "entry" => "Entry",
        "exit" => "Exit",
        _ => "Entry & exit",
    };

    public static string Percent(double fraction) =>
        (fraction * 100).ToString("0", CultureInfo.CurrentCulture) + "%";

    public static string Count(long n) => n.ToString("N0", CultureInfo.CurrentCulture);

    public static string Minutes(double? minutes)
    {
        if (minutes is not { } m)
        {
            return "—";
        }
        if (m < 60)
        {
            return $"{Math.Round(m):0} min";
        }
        var h = (int)(m / 60);
        var rest = (int)Math.Round(m - h * 60);
        return rest == 0 ? $"{h} h" : $"{h} h {rest} min";
    }

    public static string Hour(int? hour) =>
        hour is { } h ? new DateTime(2000, 1, 1, h, 0, 0).ToString("t", CultureInfo.CurrentCulture) : "—";

    // "Just now", "4 min ago", "Yesterday 18:02", "12 Mar 09:15".
    public static string Relative(DateTimeOffset when, DateTimeOffset now)
    {
        var age = now - when;
        if (age < TimeSpan.FromSeconds(45))
        {
            return "Just now";
        }
        if (age < TimeSpan.FromMinutes(60))
        {
            return $"{Math.Max(1, (int)age.TotalMinutes)} min ago";
        }
        var local = when.ToLocalTime();
        var today = now.ToLocalTime().Date;
        var time = local.ToString("t", CultureInfo.CurrentCulture);
        if (local.Date == today)
        {
            return time;
        }
        if (local.Date == today.AddDays(-1))
        {
            return $"Yesterday {time}";
        }
        return local.ToString("d MMM", CultureInfo.CurrentCulture) + " " + time;
    }

    // Plates display with a thin gap between the prefix and the rest,
    // the way Kenyan plates are printed: "KDA 123A".
    public static string Plate(string p)
    {
        if (string.IsNullOrWhiteSpace(p))
        {
            return "No plate";
        }
        var s = p.Trim().ToUpperInvariant();
        if (s.Length >= 6 && s.Length <= 8 && s.StartsWith('K') && char.IsLetter(s[1]) && char.IsLetter(s[2]))
        {
            return s[..3] + " " + s[3..];
        }
        return s;
    }

    private static string Title(string raw)
    {
        if (string.IsNullOrEmpty(raw))
        {
            return "";
        }
        var words = raw.Replace('_', ' ').ToLowerInvariant();
        return char.ToUpperInvariant(words[0]) + words[1..];
    }
}
