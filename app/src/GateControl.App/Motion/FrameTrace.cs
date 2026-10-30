// FrameTrace.cs — verification only: what ran in each frame of a traced
// entrance (Platform/Automation.cs "trace"), so every long frame can be
// attributed (layout, item creation, chart draws, data refresh, …).
// Calls compile away outside Debug and the -p:GcHarness=true build.

using System.Diagnostics;
using System.Text;

namespace GateControl.Motion;

public static class FrameTrace
{
    private static StringBuilder? _events;
    private static Stopwatch? _clock;

    internal static bool On => _events is not null;

    internal static void Begin(Stopwatch clock, StringBuilder events)
    {
        _clock = clock;
        _events = events;
    }

    internal static void End()
    {
        _events = null;
        _clock = null;
    }

    // A point event.
    [Conditional("DEBUG"), Conditional("GC_HARNESS")]
    public static void Mark(string what)
    {
        if (_events is { } e && _clock is { } c)
        {
            e.Append(c.Elapsed.TotalMilliseconds.ToString("F2", System.Globalization.CultureInfo.InvariantCulture))
             .Append(",0,").Append(what).Append('\n');
        }
    }

    // A timed span: FrameTrace.Span("chart.draw", start) after the work.
    [Conditional("DEBUG"), Conditional("GC_HARNESS")]
    public static void Span(string what, long startTicks)
    {
        if (_events is { } e && _clock is { } c)
        {
            var ms = (Stopwatch.GetTimestamp() - startTicks) * 1000.0 / Stopwatch.Frequency;
            e.Append(c.Elapsed.TotalMilliseconds.ToString("F2", System.Globalization.CultureInfo.InvariantCulture))
             .Append(',').Append(ms.ToString("F3", System.Globalization.CultureInfo.InvariantCulture))
             .Append(',').Append(what).Append('\n');
        }
    }

    public static long Now() => Stopwatch.GetTimestamp();
}
