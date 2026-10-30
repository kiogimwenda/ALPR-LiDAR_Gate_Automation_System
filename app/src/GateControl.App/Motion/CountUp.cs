// CountUp.cs — numbers count to their value (spec A5): ease-out-cubic over
// 1.45 s (TimedProgress.CountUp), the first value from zero, later values
// from the number on screen (a refresh mid-count continues, no jump).
//
// Use m:CountUp.Text="{Binding Value}" on a Label instead of Text. Only
// the number inside the text animates ("1,234", "87%", "3.5 h"); text
// without a number is shown as is. The bundled Inter GC has tabular
// figures, so the label's width doesn't jitter while digits change.
// Reduce Motion: the value is shown directly.
//
// Item templates are rebuilt on every refresh, so a new Label would count
// from zero again; give it m:CountUp.Key (set BEFORE Text in XAML, e.g.
// the metric's title) and a new label with the same key continues from
// the last value shown under that key.

using GateControl.Core.Formatting;
using GateControl.Core.Motion;
using System.Runtime.CompilerServices;

namespace GateControl.Motion;

public static class CountUp
{
    public static readonly BindableProperty TextProperty = BindableProperty.CreateAttached(
        "Text", typeof(string), typeof(CountUp), null,
        propertyChanged: (b, _, n) => { if (b is Label l) Animate(l, n as string); });

    public static readonly BindableProperty KeyProperty = BindableProperty.CreateAttached(
        "Key", typeof(string), typeof(CountUp), null);

    public static string? GetKey(BindableObject b) => (string?)b.GetValue(KeyProperty);
    public static void SetKey(BindableObject b, string? v) => b.SetValue(KeyProperty, v);

    // Last value shown per key (UI thread only).
    private static readonly Dictionary<string, double> LastByKey = [];

    public static string? GetText(BindableObject b) => (string?)b.GetValue(TextProperty);
    public static void SetText(BindableObject b, string? v) => b.SetValue(TextProperty, v);

    private sealed class State
    {
        public TimedProgress Progress { get; } = TimedProgress.CountUp();
        public NumberText? Target { get; set; }
        public bool Running { get; set; }
    }

    private static readonly ConditionalWeakTable<Label, State> States = new();

    // While counting, the label keeps the size of its final text (digits are
    // tabular, so every intermediate value fits): a text change then can't
    // change its size, and MAUI doesn't re-lay the whole page out on every
    // frame (that cost 40–60 ms frames on the Dashboard).
    private static void Pin(Label label, string finalText)
    {
        var shown = label.Text;
        label.Text = finalText;
        var size = label.Measure(double.PositiveInfinity, double.PositiveInfinity);
        label.Text = shown;
        if (size.Width > 0 && size.Height > 0)
        {
            label.WidthRequest = Math.Ceiling(size.Width) + 1;
            label.HeightRequest = Math.Ceiling(size.Height);
        }
    }

    private static void Unpin(Label label)
    {
        label.WidthRequest = -1;
        label.HeightRequest = -1;
    }

    private static void Animate(Label label, string? text)
    {
        var target = NumberText.Parse(text);
        var key = GetKey(label);
        var fresh = !States.TryGetValue(label, out _);
        var state = States.GetOrCreateValue(label);
        if (fresh && key is not null && LastByKey.TryGetValue(key, out var last))
        {
            state.Progress.Snap(last);
        }
        if (key is not null && target is not null)
        {
            LastByKey[key] = target.Value;
        }
        if (target is null || MotionSettings.ReduceMotion)
        {
            state.Target = target;
            state.Progress.Snap(target?.Value ?? 0);
            label.Text = text;
            return;
        }
        state.Target = target;
        state.Progress.Retarget(target.Value);
        if (state.Progress.IsDone)
        {
            label.Text = text;
            return;
        }
        if (!state.Running)
        {
            Pin(label, text!);
        }
        label.Text = target.Format(state.Progress.Value);
        if (state.Running)
        {
            return;
        }
        state.Running = true;
        Spr.OnFrame(dt =>
        {
            state.Progress.Step(dt);
            if (state.Target is { } t)
            {
                var t0 = FrameTrace.Now();
                label.Text = state.Progress.IsDone ? GetText(label) : t.Format(state.Progress.Value);
                FrameTrace.Span("countup.text", t0);
            }
            state.Running = !state.Progress.IsDone;
            if (!state.Running)
            {
                Unpin(label);
            }
            return state.Running;
        }, label.Dispatcher);
    }
}
