// Timing.cs — the motion that is NOT a spring, plus stagger, reveal and
// blob geometry. Pure math, unit-tested; the app drives it frame by frame.
//
// Springs remain the rule (Spring.cs). The reference design has exactly
// two motions a spring cannot reproduce, both measured frame by frame
// from the reference video (app/docs/design-video-spec.md §9):
//
//   * Number count-up (A5): ease-out-cubic over 1.45 s — fits the video
//     15× better than any critically damped spring, and it lands exactly
//     on the value and holds (a spring creeps).
//   * Theme reveal (A11): ease-in-quad (0.56 s radius curve in the video;
//     0.62 s here, calibrated on covered area) — the wipe accelerates
//     until it covers the window; a spring always decelerates.
//
// Both are one-shot, short and interruptible from where they are: a new
// target restarts the curve from the value currently shown, so nothing
// jumps.

namespace GateControl.Core.Motion;

public enum Curve
{
    Linear,
    OutCubic,
    InQuad,
}

public static class Curves
{
    public static double Apply(Curve curve, double u)
    {
        u = Math.Clamp(u, 0, 1);
        return curve switch
        {
            Curve.OutCubic => 1 - Math.Pow(1 - u, 3),
            Curve.InQuad => u * u,
            _ => u,
        };
    }
}

// A value moving from `From` to `To` along a curve over a fixed duration.
public sealed class TimedProgress
{
    public TimedProgress(double value, double duration, Curve curve)
    {
        if (duration <= 0)
        {
            throw new ArgumentOutOfRangeException(nameof(duration));
        }
        From = To = value;
        Duration = duration;
        Curve = curve;
        Elapsed = duration;
    }

    // The two documented exceptions to "springs only".
    public static TimedProgress CountUp(double value = 0) => new(value, 1.45, Curve.OutCubic);
    // 0.56 s is the radius curve measured in the reference's geometry; in
    // our window (wider, origin in the rail) the same curve covers the area
    // faster, so the duration is calibrated on the area metric the review
    // uses (area 1 % → 98.5 % in 0.50 s, as in the video): 0.62 s.
    public static TimedProgress ThemeReveal() => new(0, 0.62, Curve.InQuad);

    public double From { get; private set; }
    public double To { get; private set; }
    public double Duration { get; }
    public Curve Curve { get; }
    public double Elapsed { get; private set; }

    public bool IsDone => Elapsed >= Duration;

    public double Value => IsDone ? To : From + (To - From) * Curves.Apply(Curve, Elapsed / Duration);

    // New destination: restarts the curve from the value on screen now.
    public void Retarget(double to)
    {
        From = Value;
        To = to;
        Elapsed = From == To ? Duration : 0;
    }

    public void Snap(double value)
    {
        From = To = value;
        Elapsed = Duration;
    }

    public void Step(double dt)
    {
        if (dt > 0 && !IsDone)
        {
            Elapsed = Math.Min(Duration, Elapsed + dt);
        }
    }
}

// Items entering one after another (A3 cards, A4/A10 rows, A2 words, A7 bars).
public static class Stagger
{
    public const double Cards = 0.12;  // A3: card to card, reading order
    public const double Rows = 0.065;  // A4/A7/A10: row to row, bar to bar
    public const double Words = 0.09;  // A2: title words

    // Delay of item `index`; long lists stop adding delay after `cap` items
    // so the last rows don't wait seconds for their turn.
    public static double Delay(int index, double step, int cap = 12) =>
        Math.Max(0, Math.Min(index, cap)) * step;
}

// Bars growing from the baseline (A7): each bar 65 ms after the one to its
// left, a stacked cap 0.2 s after its base, critically damped 0.45 s.
public static class ChartGrow
{
    public static readonly SpringSpec Spring = new(1.0, 0.45);
    public const double BarStep = 0.065;
    public const double CapDelay = 0.2;

    public static double Base(int index, double t) => Motion.Spring.StepResponse(Spring, t - index * BarStep);

    public static double Cap(int index, double t) => Motion.Spring.StepResponse(Spring, t - index * BarStep - CapDelay);

    // Seconds until every bar of `count` (with caps) has settled.
    public static double Duration(int count) => Math.Max(0, count - 1) * BarStep + CapDelay + 1.2;
}

// The page-enter reveal of one element (A2/A3/A4).
public readonly record struct RevealSpec(double FromY, double FromScale, double BlurRadius, double BlurDuration)
{
    public static readonly RevealSpec Card = new(8, 0.96, 8, 0.25);
    public static readonly RevealSpec Row = new(8, 1.0, 6, 0.25);
    public static readonly RevealSpec Word = new(6, 1.0, 8, 0.25);

    // Response of the opacity / rise / scale springs (measured A3: 1.0, 0.40).
    public static readonly SpringSpec Spring = SpringSpec.Move;

    // Blur radius `t` seconds after this item starts: falls to 0 over
    // BlurDuration with ease-out, alongside the opacity spring.
    public double BlurAt(double t) =>
        t >= BlurDuration ? 0 : BlurRadius * (1 - Curves.Apply(Curve.OutCubic, Math.Max(0, t) / BlurDuration));
}

// The wavy disc of the theme reveal (A11) and the accent-tile reveal (A6):
// r(θ) = r · (1 + a·sin(lobes·θ + φ)), as a closed polygon.
public static class Blob
{
    public const int Points = 48;
    public const double Amplitude = 0.06;
    public const int Lobes = 5;

    public static (double X, double Y)[] Outline(double cx, double cy, double radius, double phase,
                                                 int points = Points, double amplitude = Amplitude, int lobes = Lobes)
    {
        var result = new (double, double)[points];
        for (var i = 0; i < points; i++)
        {
            var theta = 2 * Math.PI * i / points;
            var r = Math.Max(0, radius * (1 + amplitude * Math.Sin(lobes * theta + phase)));
            result[i] = (cx + r * Math.Cos(theta), cy + r * Math.Sin(theta));
        }
        return result;
    }

    // Radius at which the blob (its innermost dips included) covers a
    // width × height rectangle from (cx, cy).
    public static double CoverRadius(double cx, double cy, double width, double height, double amplitude = Amplitude)
    {
        var far = new[]
        {
            double.Hypot(cx, cy), double.Hypot(width - cx, cy),
            double.Hypot(cx, height - cy), double.Hypot(width - cx, height - cy),
        }.Max();
        // A polygon's edge midpoints sit inside the curve through its
        // vertices by cos(π/48) ≈ 0.9979; 0.3 % covers that. No more margin:
        // the reveal must reach full cover at the very end of its curve,
        // as in the reference (spec A11).
        return far / (1 - amplitude) * 1.003;
    }

    // The edge drifts while it grows (the video's blob rotates ~1 lobe).
    public static double PhaseAt(double progress) => progress * Math.PI * 0.8;
}
