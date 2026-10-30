// BlobClip.cs — reveal by a growing wavy disc (spec A6 accent tile, A11
// theme reveal). The outline is Core's Blob (48 points, r·(1 + 0.06·
// sin(5θ + φ))); this file turns it into clip geometry and drives it.
//
// Animating a clip is the documented exception to "transform and opacity
// only" (spec §11): the clip is a 48-point polygon, recomputed per frame
// on one element, never on list items.

using GateControl.Core.Motion;
using Microsoft.Maui.Controls.Shapes;

namespace GateControl.Motion;

public static class BlobClip
{
    // Spec A6: ~0.2 s to cover, critically damped.
    public static readonly SpringSpec Grow = new(1.0, 0.3);

    // The disc alone (A6: what is inside is visible).
    public static PathGeometry Disc(Point centre, double radius, double phase) =>
        new() { Figures = { Figure(centre, radius, phase) } };

    // The window minus the disc (A11: the old-theme snapshot with a hole
    // through which the new theme shows).
    public static PathGeometry RectMinusDisc(Size size, Point centre, double radius, double phase)
    {
        var rect = new PathFigure { StartPoint = new Point(0, 0), IsClosed = true };
        rect.Segments.Add(new PolyLineSegment
        {
            Points = [new Point(size.Width, 0), new Point(size.Width, size.Height), new Point(0, size.Height)],
        });
        // The disc winds the other way round, so the hole is cut under both
        // fill rules (WinUI's composition clip ignores even-odd).
        return new PathGeometry { FillRule = FillRule.EvenOdd, Figures = { rect, Figure(centre, radius, phase, reverse: true) } };
    }

    private static PathFigure Figure(Point centre, double radius, double phase, bool reverse = false)
    {
        var pts = Blob.Outline(centre.X, centre.Y, radius, phase);
        if (reverse)
        {
            Array.Reverse(pts);
        }
        var figure = new PathFigure { StartPoint = new Point(pts[0].X, pts[0].Y), IsClosed = true };
        var line = new PolyLineSegment();
        for (var i = 1; i < pts.Length; i++)
        {
            line.Points.Add(new Point(pts[i].X, pts[i].Y));
        }
        figure.Segments.Add(line);
        return figure;
    }

#if WINDOWS
    // A11 on Windows: `view` (the theme snapshot) clipped to everything
    // outside a wavy disc at `origin`, as one composition path built once;
    // the returned setter moves the disc (radius, phase) by the clip's scale
    // and rotation only — RectMinusDisc set per frame rebuilt MAUI and WinUI
    // geometry every frame (gen-0/1 collections of 25–40 ms mid-wipe).
    // Null when the view has no native visual yet.
    public static Action<double, double>? NativeHole(VisualElement view, Point origin) =>
        NativeClip.Hole(view, origin);
#endif

    // Whether `view` is still (partly) clipped by a blob: a MAUI clip, or
    // on Windows the native one. (The harness reads it to find when an
    // accent card has settled.)
    public static bool IsClipped(VisualElement view) =>
#if WINDOWS
        NativeClip.Active.Contains(view) ||
#endif
        view.Clip is not null;

    // Hidden behind a zero-size disc until GrowIn starts.
    public static void Prewarm()
    {
#if WINDOWS
        NativeClip.Prewarm();
#endif
    }

    public static void Hide(VisualElement view)
    {
#if WINDOWS
        if (NativeClip.TryHide(view))
        {
            return;
        }
#endif
        view.Clip = Disc(new Point(0, 0), 0, 0);
    }

    // A6: `view` appears through a disc growing from `origin` (view
    // coordinates, e.g. its top-left corner). Reduce Motion: a fade.
    public static void GrowIn(VisualElement view, Point origin, double delay = 0, Action? done = null)
    {
        if (MotionSettings.ReduceMotion)
        {
            view.Clip = null;
#if WINDOWS
            NativeClip.Clear(view);
#endif
            Spr.Set(view, Prop.Opacity, 0);
            Spr.To(view, Prop.Opacity, 1, SpringSpec.Press, delay: delay);
            done?.Invoke();
            return;
        }
        var spring = new Spring(0, Grow);
        spring.Retarget(1);
        var start = Spr.Now + delay;
#if WINDOWS
        if (NativeClip.TryGrow(view, origin, spring, start, done))
        {
            return;
        }
#endif
        view.Clip = Disc(origin, 0, 0);
        Spr.OnFrame(dt =>
        {
            if (Spr.Now < start)
            {
                return true;
            }
            spring.Step(Math.Min(dt, Spr.Now - start));
            if (spring.IsSettled || view.Width <= 0)
            {
                view.Clip = null;
                if (spring.IsSettled)
                {
                    done?.Invoke();
                }
                return !spring.IsSettled;
            }
            var cover = Blob.CoverRadius(origin.X, origin.Y, view.Width, view.Height);
            var t0 = FrameTrace.Now();
            view.Clip = Disc(origin, cover * spring.Value, Blob.PhaseAt(spring.Value));
            FrameTrace.Span("blob.clip", t0);
            return true;
        }, view.Dispatcher);
    }

#if WINDOWS
    // Windows: the blob is one composition path built once (Win2D), and each
    // frame only moves its clip's scale and rotation. A wave phase φ is the
    // same outline turned by −φ / lobes, so this is the same shape as
    // Disc(origin, r, φ). Setting a MAUI Clip per frame instead rebuilt the
    // geometry in WrapperView and created WinUI objects every frame; WinUI's
    // reference tracker then asked .NET for a full blocking collection
    // (EventPipe: GCTriggered Induced, IReferenceTrackerHost.
    // DisconnectUnusedReferenceSources) during the first Dashboard entrance.
    private static class NativeClip
    {
        private const float Unit = 100;
        internal static readonly HashSet<VisualElement> Active = [];

        public static void Clear(VisualElement view)
        {
            if (Active.Remove(view) && VisualOf(view) is { } visual)
            {
                visual.Clip = null;
            }
        }

        private static Microsoft.UI.Composition.Visual? VisualOf(VisualElement view) =>
            (view.Handler?.ContainerView ?? view.Handler?.PlatformView) is Microsoft.UI.Xaml.UIElement element
                ? Microsoft.UI.Xaml.Hosting.ElementCompositionPreview.GetElementVisual(element)
                : null;

        private static readonly System.Runtime.CompilerServices.ConditionalWeakTable<Microsoft.UI.Composition.Visual, Microsoft.UI.Composition.CompositionGeometricClip> Clips = new();
        private static Microsoft.Graphics.Canvas.Geometry.CanvasGeometry? _sharedGeometry;
        private static Microsoft.UI.Composition.CompositionPath? _sharedPath;

        public static void Prewarm()
        {
            try
            {
                EnsureSharedPath();
            }
            catch (Exception e)
            {
                // Not fatal: the path is built on first use instead.
                Microsoft.Extensions.Logging.LoggerExtensions.LogWarning(GateControl.Platform.AppLog.For("BlobClip"), e, "blob path prewarm failed");
            }
        }

        private static Microsoft.UI.Composition.CompositionPath EnsureSharedPath()
        {
            if (_sharedPath is not null)
            {
                return _sharedPath;
            }
            var pts = Blob.Outline(0, 0, Unit, 0);
            using var builder = new Microsoft.Graphics.Canvas.Geometry.CanvasPathBuilder(Microsoft.Graphics.Canvas.CanvasDevice.GetSharedDevice());
            builder.BeginFigure((float)pts[0].X, (float)pts[0].Y);
            for (var i = 1; i < pts.Length; i++)
            {
                builder.AddLine((float)pts[i].X, (float)pts[i].Y);
            }
            builder.EndFigure(Microsoft.Graphics.Canvas.Geometry.CanvasFigureLoop.Closed);
            _sharedGeometry = Microsoft.Graphics.Canvas.Geometry.CanvasGeometry.CreatePath(builder);
            _sharedPath = new Microsoft.UI.Composition.CompositionPath(_sharedGeometry);
            return _sharedPath;
        }

        private static Microsoft.UI.Composition.CompositionGeometricClip ClipFor(Microsoft.UI.Composition.Visual visual)
        {
            if (Clips.TryGetValue(visual, out var clip))
            {
                return clip;
            }
            var path = EnsureSharedPath();
            var compositor = visual.Compositor;
            clip = compositor.CreateGeometricClip(compositor.CreatePathGeometry(path));
            Clips.Add(visual, clip);
            return clip;
        }

        private static Microsoft.UI.Composition.CompositionPath? _holePath;

        public static Action<double, double>? Hole(VisualElement view, Point origin)
        {
            if (VisualOf(view) is not { } visual)
            {
                return null;
            }
            if (_holePath is null)
            {
                // A square far larger than any window at the smallest scale
                // (22 dip / Unit), minus the unit blob wound the other way.
                const float big = 100_000;
                var pts = Blob.Outline(0, 0, Unit, 0);
                using var builder = new Microsoft.Graphics.Canvas.Geometry.CanvasPathBuilder(Microsoft.Graphics.Canvas.CanvasDevice.GetSharedDevice());
                builder.SetFilledRegionDetermination(Microsoft.Graphics.Canvas.Geometry.CanvasFilledRegionDetermination.Alternate);
                builder.BeginFigure(-big, -big);
                builder.AddLine(big, -big);
                builder.AddLine(big, big);
                builder.AddLine(-big, big);
                builder.EndFigure(Microsoft.Graphics.Canvas.Geometry.CanvasFigureLoop.Closed);
                builder.BeginFigure((float)pts[^1].X, (float)pts[^1].Y);
                for (var i = pts.Length - 2; i >= 0; i--)
                {
                    builder.AddLine((float)pts[i].X, (float)pts[i].Y);
                }
                builder.EndFigure(Microsoft.Graphics.Canvas.Geometry.CanvasFigureLoop.Closed);
                _holePath = new Microsoft.UI.Composition.CompositionPath(Microsoft.Graphics.Canvas.Geometry.CanvasGeometry.CreatePath(builder));
            }
            var compositor = visual.Compositor;
            var clip = compositor.CreateGeometricClip(compositor.CreatePathGeometry(_holePath));
            clip.Offset = new System.Numerics.Vector2((float)origin.X, (float)origin.Y);
            visual.Clip = clip;
            return (radius, phase) =>
            {
                var k = (float)(radius / Unit);
                clip.Scale = new System.Numerics.Vector2(k, k);
                clip.RotationAngleInDegrees = (float)(-phase / Blob.Lobes * 180 / Math.PI);
            };
        }

        public static bool TryHide(VisualElement view)
        {
            if (VisualOf(view) is not { } visual)
            {
                return false;
            }
            var clip = ClipFor(visual);
            clip.Scale = System.Numerics.Vector2.Zero;
            visual.Clip = clip;
            Active.Add(view);
            return true;
        }

        public static bool TryGrow(VisualElement view, Point origin, Spring spring, double start, Action? done)
        {
            if (VisualOf(view) is not { } visual)
            {
                return false;
            }
            view.Clip = null;
            var clip = ClipFor(visual);
            clip.Offset = new System.Numerics.Vector2((float)origin.X, (float)origin.Y);
            clip.Scale = System.Numerics.Vector2.Zero;
            visual.Clip = clip;
            Active.Add(view);
            Spr.OnFrame(dt =>
            {
                if (Spr.Now < start)
                {
                    return true;
                }
                spring.Step(Math.Min(dt, Spr.Now - start));
                if (spring.IsSettled)
                {
                    visual.Clip = null;
                    Active.Remove(view);
                    done?.Invoke();
                    return false;
                }
                if (view.Width <= 0)
                {
                    return true;  // not laid out yet: stays hidden
                }
                var t0 = FrameTrace.Now();
                var r = (float)(Blob.CoverRadius(origin.X, origin.Y, view.Width, view.Height) * spring.Value / Unit);
                clip.Scale = new System.Numerics.Vector2(r, r);
                clip.RotationAngleInDegrees = (float)(-Blob.PhaseAt(spring.Value) / Blob.Lobes * 180 / Math.PI);
                FrameTrace.Span("blob.clip", t0);
                return true;
            }, view.Dispatcher);
            return true;
        }
    }
#endif
}
