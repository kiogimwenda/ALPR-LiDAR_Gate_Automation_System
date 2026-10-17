using GateControl.Core.Motion;

namespace GateControl.Core.Tests;

public class MotionTests
{
    private static int StepsToSettle(Spring s, double dt = 1.0 / 120, int max = 10_000)
    {
        var n = 0;
        while (!s.IsSettled && n < max)
        {
            s.Step(dt);
            n++;
        }
        return n;
    }

    [Fact]
    public void CriticallyDampedSpringArrivesWithoutOvershoot()
    {
        var s = new Spring(0, SpringSpec.Default);
        s.Retarget(100);
        var max = 0.0;
        var n = 0;
        while (!s.IsSettled && n++ < 1000)
        {
            s.Step(1.0 / 120);
            max = Math.Max(max, s.Value);
        }
        Assert.True(s.IsSettled);
        Assert.Equal(100, s.Value);
        Assert.True(max <= 100.0001, $"overshot to {max}");
        // "Response" is the time for the motion to largely complete.
        Assert.InRange(n / 120.0, 0.2, 1.2);
    }

    [Fact]
    public void UnderdampedSpringBouncesOnlyALittle()
    {
        var s = new Spring(0, SpringSpec.Sheet);  // damping 0.8
        s.Retarget(100);
        var max = 0.0;
        for (var i = 0; i < 240; i++)
        {
            s.Step(1.0 / 120);
            max = Math.Max(max, s.Value);
        }
        Assert.InRange(max, 100.1, 103);  // a small, visible bounce
    }

    [Fact]
    public void RetargetMidFlightKeepsPositionAndVelocity()
    {
        var s = new Spring(0, SpringSpec.Default);
        s.Retarget(100);
        for (var i = 0; i < 6; i++)
        {
            s.Step(1.0 / 60);
        }
        var x = s.Value;
        var v = s.Velocity;
        Assert.True(v > 0);
        s.Retarget(0);  // change of mind: turn around from where it is
        Assert.Equal(x, s.Value);
        Assert.Equal(v, s.Velocity);
        s.Step(1.0 / 60);
        Assert.True(Math.Abs(s.Value - x) < 20, "no jump");
        StepsToSettle(s);
        Assert.Equal(0, s.Value);
    }

    [Fact]
    public void StepIsFrameRateIndependent()
    {
        var a = new Spring(0, SpringSpec.Move);
        var b = new Spring(0, SpringSpec.Move);
        a.Retarget(50);
        b.Retarget(50);
        for (var i = 0; i < 12; i++)
        {
            a.Step(1.0 / 120);
        }
        b.Step(0.1);  // one long (dropped-frame) step covering the same time
        Assert.Equal(a.Value, b.Value, 6);
    }

    [Fact]
    public void FlickVelocityCarriesIntoTheSpring()
    {
        var s = new Spring(0, SpringSpec.Flick);
        s.Retarget(300, velocity: 2000);
        s.Step(1.0 / 120);
        var still = new Spring(0, SpringSpec.Flick);
        still.Retarget(300);
        still.Step(1.0 / 120);
        Assert.True(s.Value > still.Value);
    }

    [Fact]
    public void RubberBandResistsAndApproachesTheDimension()
    {
        Assert.Equal(0, Gesture.RubberBand(0, 400));
        var small = Gesture.RubberBand(40, 400);
        var big = Gesture.RubberBand(4000, 400);
        Assert.InRange(small, 0, 40);  // resists: moves less than the finger
        Assert.InRange(big, 300, 400);  // never passes the dimension
        Assert.Equal(-small, Gesture.RubberBand(-40, 400), 9);
    }

    [Fact]
    public void ProjectionPredictsAThrowsRestingPlace()
    {
        Assert.Equal(100, Gesture.Project(100, 0));
        var p = Gesture.Project(0, 1000);  // 1000 pt/s
        Assert.InRange(p, 490, 510);       // ≈ 499 with deceleration 0.998
    }

    [Fact]
    public void SnapJumpsWithoutMotion()
    {
        var s = new Spring(0, SpringSpec.Default);
        s.Retarget(10, velocity: 50);
        s.Snap(42);
        Assert.Equal(42, s.Value);
        Assert.Equal(0, s.Velocity);
        Assert.True(s.IsSettled);
    }
}
