// Spring.cs — the motion model behind every animation in the app.
//
// The design brief (Apple's WWDC motion principles) in code:
//
//   * Springs, not curves: no fixed durations. A spring is described by
//     its damping ratio (1.0 = no overshoot) and response (seconds for
//     the motion to largely complete), exactly as Apple's frameworks do.
//   * Interruptible: Retarget() changes the destination mid-flight and
//     keeps the current position *and velocity*, so a change of mind
//     turns the motion around from where it is — no jump, no restart.
//   * Velocity handoff: a flick's speed (same units per second) seeds the
//     spring through Retarget(..., velocity:).
//   * Momentum: Project() predicts where a throw would come to rest
//     (UIScrollView's deceleration rate 0.998 per ms).
//   * Rubber-banding: RubberBand() is the classic 0.55 edge resistance.
//
// Pure math (no UI types) so it is unit-tested; Motion.cs in the app
// drives it frame by frame.

namespace GateControl.Core.Motion;

public readonly record struct SpringSpec(double DampingRatio, double Response)
{
    // Apple's defaults.
    public static readonly SpringSpec Default = new(1.0, 0.35);    // UI state changes
    public static readonly SpringSpec Move = new(1.0, 0.4);        // move / reposition
    public static readonly SpringSpec Sheet = new(0.8, 0.3);       // drawers and sheets
    public static readonly SpringSpec Flick = new(0.8, 0.4);       // after a thrown gesture
    public static readonly SpringSpec Press = new(1.0, 0.1);       // press-down feedback

    public double Stiffness => Math.Pow(2 * Math.PI / Response, 2);
    public double Damping => 4 * Math.PI * DampingRatio / Response;
}

public sealed class Spring
{
    private const double RestDistance = 0.001;
    private const double RestVelocity = 0.01;

    public Spring(double value, SpringSpec spec)
    {
        Value = value;
        Target = value;
        Spec = spec;
    }

    public double Value { get; private set; }
    public double Velocity { get; private set; }
    public double Target { get; private set; }
    public SpringSpec Spec { get; private set; }

    public bool IsSettled =>
        Math.Abs(Value - Target) < RestDistance * Math.Max(1, Math.Abs(Target)) &&
        Math.Abs(Velocity) < RestVelocity * Math.Max(1, Math.Abs(Target));

    // New destination; position and velocity carry over (interruptible).
    public void Retarget(double target, SpringSpec? spec = null, double? velocity = null)
    {
        Target = target;
        if (spec is { } s)
        {
            Spec = s;
        }
        if (velocity is { } v)
        {
            Velocity = v;
        }
    }

    // Jump without animating (Reduce Motion, initial layout).
    public void Snap(double value)
    {
        Value = value;
        Target = value;
        Velocity = 0;
    }

    // Advance by dt seconds using the closed-form solution of the damped
    // oscillator — exact for any frame time, so a dropped frame never
    // makes the spring overshoot or explode.
    public void Step(double dt)
    {
        if (dt <= 0)
        {
            return;
        }
        var k = Spec.Stiffness;
        var c = Spec.Damping;
        var x0 = Value - Target;
        var v0 = Velocity;
        var w0 = Math.Sqrt(k);
        var zeta = c / (2 * w0);
        double x, v;
        if (zeta < 1)
        {
            var wd = w0 * Math.Sqrt(1 - zeta * zeta);
            var e = Math.Exp(-zeta * w0 * dt);
            var a = x0;
            var b = (v0 + zeta * w0 * x0) / wd;
            var cos = Math.Cos(wd * dt);
            var sin = Math.Sin(wd * dt);
            x = e * (a * cos + b * sin);
            v = e * ((b * wd - zeta * w0 * a) * cos - (a * wd + zeta * w0 * b) * sin);
        }
        else if (Math.Abs(zeta - 1) < 1e-9)
        {
            var e = Math.Exp(-w0 * dt);
            var b = v0 + w0 * x0;
            x = e * (x0 + b * dt);
            v = e * (b - w0 * (x0 + b * dt));
        }
        else
        {
            var s = Math.Sqrt(zeta * zeta - 1);
            var r1 = -w0 * (zeta - s);
            var r2 = -w0 * (zeta + s);
            var c2 = (v0 - r1 * x0) / (r2 - r1);
            var c1 = x0 - c2;
            x = c1 * Math.Exp(r1 * dt) + c2 * Math.Exp(r2 * dt);
            v = c1 * r1 * Math.Exp(r1 * dt) + c2 * r2 * Math.Exp(r2 * dt);
        }
        Value = Target + x;
        Velocity = v;
        if (IsSettled)
        {
            Value = Target;
            Velocity = 0;
        }
    }
}

public static class Gesture
{
    // Points of travel before a press turns into a drag (Apple: ~10 pt).
    public const double DragThreshold = 10;

    // UIScrollView.DecelerationRate.normal, per millisecond.
    public const double Deceleration = 0.998;

    // Edge resistance: offset past the edge → visible offset, approaching
    // `dimension` asymptotically (constant 0.55, as iOS uses).
    public static double RubberBand(double offset, double dimension, double constant = 0.55)
    {
        if (dimension <= 0)
        {
            return 0;
        }
        var sign = Math.Sign(offset);
        var o = Math.Abs(offset);
        return sign * (1 - 1 / (o * constant / dimension + 1)) * dimension;
    }

    // Where a throw at `velocity` (points per second) would come to rest.
    public static double Project(double position, double velocity, double deceleration = Deceleration)
    {
        return position + velocity / 1000 * deceleration / (1 - deceleration);
    }
}
