using GateControl.Core.Motion;
using GateControl.Core.ViewModels;

namespace GateControl.Core.Tests;

public class TimingTests
{
    [Theory]
    [InlineData(Curve.OutCubic)]
    [InlineData(Curve.InQuad)]
    [InlineData(Curve.Linear)]
    public void CurvesStartAtZeroEndAtOneAndNeverLeaveTheRange(Curve curve)
    {
        Assert.Equal(0, Curves.Apply(curve, 0), 9);
        Assert.Equal(1, Curves.Apply(curve, 1), 9);
        var last = 0.0;
        for (var u = 0.0; u <= 1.0; u += 0.01)
        {
            var v = Curves.Apply(curve, u);
            Assert.InRange(v, 0, 1);
            Assert.True(v >= last - 1e-12, "monotonic");
            last = v;
        }
        Assert.Equal(1, Curves.Apply(curve, 3), 9);
        Assert.Equal(0, Curves.Apply(curve, -1), 9);
    }

    [Fact]
    public void OutCubicDeceleratesAndInQuadAccelerates()
    {
        Assert.True(Curves.Apply(Curve.OutCubic, 0.5) > 0.5);
        Assert.True(Curves.Apply(Curve.InQuad, 0.5) < 0.5);
    }

    // The reference video's hero count-up (spec §9, A5): value on screen at
    // frame N (30 fps), final 12,406. The model starts at frame 108.1.
    [Theory]
    [InlineData(114, 4360)]
    [InlineData(118, 6725)]
    [InlineData(124, 9255)]
    [InlineData(132, 11210)]
    [InlineData(135, 11756)]
    [InlineData(140, 12166)]
    [InlineData(145, 12371)]
    [InlineData(149, 12406)]
    public void CountUpMatchesTheReferenceVideo(int frame, int shown)
    {
        var p = TimedProgress.CountUp();
        p.Retarget(12406);
        p.Step((frame - 108.1) / 30.0);
        Assert.InRange(p.Value, shown - 12406 * 0.01, shown + 12406 * 0.01);
    }

    [Fact]
    public void CountUpLandsExactlyAndHolds()
    {
        var p = TimedProgress.CountUp();
        p.Retarget(12406);
        for (var i = 0; i < 200; i++)
        {
            p.Step(1.0 / 60);
        }
        Assert.True(p.IsDone);
        Assert.Equal(12406, p.Value);
        Assert.InRange(p.Duration, 1.40, 1.50);
    }

    [Fact]
    public void RetargetMidFlightRestartsFromTheValueOnScreen()
    {
        var p = TimedProgress.CountUp();
        p.Retarget(1000);
        p.Step(0.3);
        var shown = p.Value;
        Assert.InRange(shown, 1, 999);
        p.Retarget(500);
        Assert.Equal(shown, p.Value, 9);   // no jump
        Assert.Equal(shown, p.From, 9);
        Assert.False(p.IsDone);
        p.Step(10);
        Assert.Equal(500, p.Value);
    }

    [Fact]
    public void RetargetToTheSameValueIsAlreadyDone()
    {
        var p = TimedProgress.CountUp(42);
        p.Retarget(42);
        Assert.True(p.IsDone);
        Assert.Equal(42, p.Value);
    }

    [Fact]
    public void SnapJumps()
    {
        var p = TimedProgress.CountUp();
        p.Retarget(10);
        p.Snap(7);
        Assert.True(p.IsDone);
        Assert.Equal(7, p.Value);
    }

    // Spec §9, A11: radius ∝ (t / D)², D calibrated on covered area (§14).
    [Fact]
    public void ThemeRevealIsEaseInQuadOverAboutHalfASecond()
    {
        var p = TimedProgress.ThemeReveal();
        Assert.InRange(p.Duration, 0.55, 0.65);
        p.Retarget(1);
        p.Step(p.Duration / 2);
        Assert.InRange(p.Value, 0.24, 0.26);
        p.Step(p.Duration / 2);
        Assert.True(p.IsDone);
        Assert.Equal(1, p.Value);
    }

    [Fact]
    public void ZeroDurationIsRejected()
    {
        Assert.Throws<ArgumentOutOfRangeException>(() => new TimedProgress(0, 0, Curve.Linear));
    }

    [Fact]
    public void StaggerGrowsLinearlyAndIsCapped()
    {
        Assert.Equal(0, Stagger.Delay(0, Stagger.Rows));
        Assert.Equal(0.065 * 3, Stagger.Delay(3, Stagger.Rows), 9);
        Assert.Equal(Stagger.Delay(12, Stagger.Rows), Stagger.Delay(40, Stagger.Rows));
        Assert.Equal(0, Stagger.Delay(-2, Stagger.Rows));
        Assert.Equal(0.12, Stagger.Cards);
    }

    [Fact]
    public void RevealBlurFallsToZeroWithinItsDuration()
    {
        var s = RevealSpec.Card;
        Assert.Equal(s.BlurRadius, s.BlurAt(0), 9);
        Assert.Equal(s.BlurRadius, s.BlurAt(-1), 9);
        Assert.True(s.BlurAt(0.1) < s.BlurRadius);
        Assert.Equal(0, s.BlurAt(s.BlurDuration));
        Assert.Equal(0, s.BlurAt(5));
        Assert.Equal(0.96, s.FromScale);
        Assert.Equal(new SpringSpec(1.0, 0.4), RevealSpec.Spring);
    }

    [Fact]
    public void BlobOutlineIsAClosedWavyRing()
    {
        var pts = Blob.Outline(100, 50, 40, 0);
        Assert.Equal(48, pts.Length);
        var radii = pts.Select(p => double.Hypot(p.X - 100, p.Y - 50)).ToArray();
        Assert.InRange(radii.Min(), 40 * 0.94 - 1e-9, 40 * 0.95);
        Assert.InRange(radii.Max(), 40 * 1.05, 40 * 1.06 + 1e-9);
        // Five lobes: count the local maxima around the ring.
        var maxima = Enumerable.Range(0, radii.Length)
            .Count(i => radii[i] > radii[(i + radii.Length - 1) % radii.Length] &&
                        radii[i] >= radii[(i + 1) % radii.Length]);
        Assert.Equal(5, maxima);
    }

    [Fact]
    public void BlobOfZeroRadiusCollapsesToItsCentre()
    {
        Assert.All(Blob.Outline(3, 4, 0, 1.2), p =>
        {
            Assert.Equal(3, p.X, 9);
            Assert.Equal(4, p.Y, 9);
        });
    }

    [Theory]
    [InlineData(0, 0)]
    [InlineData(40, 60)]
    [InlineData(800, 600)]
    [InlineData(400, 300)]
    public void CoverRadiusCoversEveryCornerEvenAtTheDips(double cx, double cy)
    {
        const double w = 800, h = 600;
        var r = Blob.CoverRadius(cx, cy, w, h);
        foreach (var phase in new[] { 0.0, 0.7, 2.0 })
        {
            var pts = Blob.Outline(cx, cy, r, phase);
            // Every corner lies inside the polygon (its inscribed radius
            // at the corner's angle is beyond the corner's distance).
            foreach (var (x, y) in new[] { (0.0, 0.0), (w, 0.0), (0.0, h), (w, h) })
            {
                Assert.True(Inside(pts, x, y), $"corner {x},{y} uncovered at phase {phase}");
            }
        }
    }

    [Fact]
    public void BlobPhaseDriftsWithProgress()
    {
        Assert.Equal(0, Blob.PhaseAt(0));
        Assert.True(Blob.PhaseAt(1) > Blob.PhaseAt(0.5));
    }

    private static bool Inside((double X, double Y)[] poly, double x, double y)
    {
        var inside = false;
        for (int i = 0, j = poly.Length - 1; i < poly.Length; j = i++)
        {
            if ((poly[i].Y > y) != (poly[j].Y > y) &&
                x < (poly[j].X - poly[i].X) * (y - poly[i].Y) / (poly[j].Y - poly[i].Y) + poly[i].X)
            {
                inside = !inside;
            }
        }
        return inside;
    }

    [Fact]
    public void StepResponseStartsAtZeroAndSettlesAtOne()
    {
        Assert.Equal(0, Spring.StepResponse(SpringSpec.Default, -1));
        Assert.Equal(0, Spring.StepResponse(SpringSpec.Default, 0));
        Assert.InRange(Spring.StepResponse(SpringSpec.Default, 0.2), 0.3, 0.95);
        Assert.Equal(1, Spring.StepResponse(SpringSpec.Default, 3), 3);
    }

    [Fact]
    public void ChartBarsStaggerAndCapsFollowTheirBase()
    {
        var t = 0.1;
        Assert.True(ChartGrow.Base(0, t) > ChartGrow.Base(1, t));
        Assert.Equal(0, ChartGrow.Base(2, 2 * ChartGrow.BarStep));
        Assert.Equal(0, ChartGrow.Cap(0, ChartGrow.CapDelay));
        Assert.True(ChartGrow.Cap(0, 0.5) < ChartGrow.Base(0, 0.5));
        Assert.Equal(1, ChartGrow.Base(23, ChartGrow.Duration(24)), 2);
        Assert.Equal(1, ChartGrow.Cap(23, ChartGrow.Duration(24)), 2);
    }
}
