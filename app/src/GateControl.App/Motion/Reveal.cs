// Reveal.cs — page entrance: staggered fade + rise (+ scale, + blur on
// Android 12+), spec A2/A3/A4/A9/A10.
//
//   * Tag elements with m:Reveal.Order="n" or an exact m:Reveal.Delay;
//     m:Reveal.Kind picks Card (rise 8, scale 0.96; n × 120 ms), Row
//     (rise 8; n × 65 ms) or Word (rise 6; n × 90 ms). Orders count per
//     kind, so cards 0,1,2 start 0 / 120 / 240 ms and rows inside them can
//     be tagged with their own delay.
//   * Untagged pages still get an entrance: the first layout with several
//     children has them revealed as cards, 120 ms apart, in order.
//   * Old content is never animated out (A9: the swap is a cut).
//   * Blur: Android 12+ blurs each element from 8 dip to 0 over its first
//     0.25 s (RenderEffect); elsewhere the rise + scale stand in for it.
//   * Reduce Motion: no movement, no stagger, no blur — one quick fade.

using GateControl.Core.Motion;

namespace GateControl.Motion;

public enum RevealKind
{
    Card,
    Row,
    Word,
    // The accent tile / hero (A6): revealed through a wavy disc growing from
    // its top-left corner; the content is never scaled.
    Accent,
}

public static class Reveal
{
    public static readonly BindableProperty OrderProperty = BindableProperty.CreateAttached(
        "Order", typeof(int), typeof(Reveal), -1);

    public static readonly BindableProperty DelayProperty = BindableProperty.CreateAttached(
        "Delay", typeof(double), typeof(Reveal), double.NaN);

    public static readonly BindableProperty KindProperty = BindableProperty.CreateAttached(
        "Kind", typeof(RevealKind), typeof(Reveal), RevealKind.Card);

    // On an Accent card: its glow (a separate view behind it, a pre-rendered
    // image), faded in by opacity once the blob reveal has covered the card.
    public static readonly BindableProperty GlowProperty = BindableProperty.CreateAttached(
        "Glow", typeof(VisualElement), typeof(Reveal), null);

    public static VisualElement? GetGlow(BindableObject b) => (VisualElement?)b.GetValue(GlowProperty);
    public static void SetGlow(BindableObject b, VisualElement? v) => b.SetValue(GlowProperty, v);

    // On a BindableLayout container: items that arrive while the page is
    // still entering (data loaded mid-reveal) join the stagger as rows
    // instead of snapping in. Items that arrive later (refreshes) appear
    // as they are.
    public static readonly BindableProperty ItemsProperty = BindableProperty.CreateAttached(
        "Items", typeof(bool), typeof(Reveal), false,
        propertyChanged: (b, _, n) =>
        {
            if ((bool)n && b is Layout layout)
            {
                layout.ChildAdded += OnItemAdded;
            }
        });

    public static bool GetItems(BindableObject b) => (bool)b.GetValue(ItemsProperty);
    public static void SetItems(BindableObject b, bool v) => b.SetValue(ItemsProperty, v);

    // How long after a page starts entering late items still join it.
    public const double JoinWindow = 1.0;

    // "Invisible" without being skipped by the compositor (1/255): hidden
    // items stay rasterised, so their first visible frame costs nothing.
    public const double Hidden = 1.0 / 255;

    // Cards taller than this rise and fade but don't scale (see Element).
    public const double MaxScaledHeight = 360;

    // Only the first rows join (the rest are below the fold; animating
    // dozens of rows at once cost frames).
    public const int JoinMaxRows = 12;

    private static double _pageStartedAt = double.NegativeInfinity;

    private static void OnItemAdded(object? sender, ElementEventArgs e)
    {
        if (sender is not Layout layout || e.Element is not VisualElement item ||
            Spr.Now - _pageStartedAt > JoinWindow || MotionSettings.ReduceMotion)
        {
            return;
        }
        var index = layout.Children.IndexOf(item);
        FrameTrace.Mark("item.join");
        if (index >= JoinMaxRows)
        {
            return;
        }
        // Rows already on screen keep their place; new ones follow 65 ms apart,
        // starting no earlier than the container's own card (120 ms in).
        var sinceStart = Spr.Now - _pageStartedAt;
        var delay = Math.Max(0, Stagger.Cards - sinceStart) + Stagger.Delay(index, Stagger.Rows, cap: 8);
        Element(item, delay, RevealSpec.Row);
    }

    public static int GetOrder(BindableObject b) => (int)b.GetValue(OrderProperty);
    public static void SetOrder(BindableObject b, int v) => b.SetValue(OrderProperty, v);
    public static double GetDelay(BindableObject b) => (double)b.GetValue(DelayProperty);
    public static void SetDelay(BindableObject b, double v) => b.SetValue(DelayProperty, v);
    public static RevealKind GetKind(BindableObject b) => (RevealKind)b.GetValue(KindProperty);
    public static void SetKind(BindableObject b, RevealKind v) => b.SetValue(KindProperty, v);

    public static RevealSpec SpecFor(RevealKind kind) => kind switch
    {
        RevealKind.Row => RevealSpec.Row,
        RevealKind.Word => RevealSpec.Word,
        _ => RevealSpec.Card,
    };

#if DEBUG || GC_HARNESS
    // Verification (Platform/Automation.cs "trace"): every element the
    // entrance animates, with its start delay and kind.
    internal static Action<VisualElement, double, string>? Traced;
#endif

    // One element: hidden now, in after `delay` seconds.
    public static void Element(VisualElement view, double delay, RevealSpec spec)
    {
#if DEBUG || GC_HARNESS
        Traced?.Invoke(view, delay, spec == RevealSpec.Card ? "card" : spec == RevealSpec.Word ? "word" : "row");
#endif
        if (MotionSettings.ReduceMotion)
        {
            Spr.Set(view, Prop.Opacity, 1);
            Spr.Set(view, Prop.TranslationY, 0);
            Spr.Set(view, Prop.Scale, 1);
            return;
        }
        // Scale only small cards: scaling a tall card (a long results list)
        // re-renders all its text every frame (30–55 ms frames on Windows).
        if (spec.FromScale != 1 && (view.Height > MaxScaledHeight || ContainsCanvas(view)))
        {
            spec = spec with { FromScale = 1 };
        }
        Spr.Set(view, Prop.Opacity, Hidden);
        Spr.Set(view, Prop.TranslationY, spec.FromY);
        Spr.Set(view, Prop.Scale, spec.FromScale);
        Spr.To(view, Prop.Opacity, 1, RevealSpec.Spring, delay: delay);
        Spr.To(view, Prop.TranslationY, 0, RevealSpec.Spring, delay: delay);
        if (spec.FromScale != 1)
        {
            Spr.To(view, Prop.Scale, 1, RevealSpec.Spring, delay: delay);
        }
        if (PlatformBlur.Supported && spec.BlurRadius > 0)
        {
            var start = Spr.Now + delay;
            PlatformBlur.Set(view, spec.BlurRadius);
            Spr.OnFrame(_ =>
            {
                var r = spec.BlurAt(Spr.Now - start);
                PlatformBlur.Set(view, r);
                return r > 0;
            }, view.Dispatcher);
        }
    }

    // A whole page (or tab) entering. Everything is hidden at once; the
    // motion starts on the next presented frame, so a slow swap frame (the
    // tab appearing) shows nothing in motion and no item skips ahead.
    public static void Page(VisualElement root) => Page(root, startNextFrame: true);

    public static void Page(VisualElement root, bool startNextFrame)
    {
        FrameTrace.Mark("reveal.page");
        _pageStartedAt = Spr.Now;
        if (MotionSettings.ReduceMotion)
        {
            Spr.Set(root, Prop.Opacity, 0);
            Spr.To(root, Prop.Opacity, 1, SpringSpec.Press);
            return;
        }
        Spr.Set(root, Prop.Opacity, 1);
        var tagged = new List<VisualElement>();
        Collect(root, tagged);
        var plan = new List<(VisualElement View, double Delay, RevealKind Kind)>();
        if (tagged.Count > 0)
        {
            foreach (var v in tagged)
            {
                var delay = GetDelay(v);
                if (double.IsNaN(delay))
                {
                    var kind = GetKind(v);
                    var step = kind switch { RevealKind.Row => Stagger.Rows, RevealKind.Word => Stagger.Words, _ => Stagger.Cards };
                    delay = Stagger.Delay(GetOrder(v), step, cap: 30);
                }
                plan.Add((v, delay, GetKind(v)));
            }
        }
        else
        {
            var items = AutoItems(root);
            for (var i = 0; i < items.Count; i++)
            {
                plan.Add((items[i], Stagger.Delay(i, Stagger.Cards), RevealKind.Card));
            }
        }
        // Hidden now (no flash of the content before its reveal)...
        foreach (var (v, _, kind) in plan)
        {
            if (kind == RevealKind.Accent)
            {
                BlobClip.Hide(v);
                if (GetGlow(v) is { } glow)
                {
                    Spr.Set(glow, Prop.Opacity, 0);
                }
            }
            else
            {
                Spr.Set(v, Prop.Opacity, Hidden);
            }
        }
        // ...in motion from the first frame the new content is on screen.
        void Start()
        {
            _pageStartedAt = Spr.Now;
            foreach (var (v, delay, kind) in plan)
            {
                if (kind == RevealKind.Accent)
                {
                    Spr.Set(v, Prop.Opacity, 1);
                    Spr.Set(v, Prop.TranslationY, 0);
                    Spr.Set(v, Prop.Scale, 1);
                    var glow = GetGlow(v);
                    BlobClip.GrowIn(v, new Point(0, 0), delay, glow is null ? null : () => Spr.To(glow, Prop.Opacity, 1, SpringSpec.Default));
#if DEBUG || GC_HARNESS
                    Traced?.Invoke(v, delay, "accent");
#endif
                    continue;
                }
                Element(v, delay, SpecFor(kind));
                ParkTail(v, delay);
            }
        }
        if (startNextFrame)
        {
            Spr.OnFrame(_ => { Start(); return false; }, root.Dispatcher);
        }
        else
        {
            Start();
        }
    }

    // A Win2D canvas (charts) re-renders on every scale change; cards that
    // hold one rise and fade but don't scale.
    // A card holding a long list (Reports' results: 50–100 rows) fades as
    // one layer, and every row inside it is composited each frame of the
    // fade (25 ms frames on Windows). The rows past the first JoinMaxRows are
    // below the fold, so they sit at opacity 0 (skipped by the compositor)
    // until the card has settled, then return a few per frame, out of sight.
    private const int TailRowsPerFrame = 6;

    private static void ParkTail(VisualElement card, double delay)
    {
        var tail = new List<VisualElement>();
        foreach (var layout in ItemLayouts(card))
        {
            for (var i = JoinMaxRows; i < layout.Children.Count; i++)
            {
                if (layout.Children[i] is VisualElement row && row.Opacity > 0)
                {
                    tail.Add(row);
                }
            }
        }
        if (tail.Count == 0)
        {
            return;
        }
        foreach (var row in tail)
        {
            row.Opacity = 0;
        }
        var start = Spr.Now;
        Spr.OnFrame(_ =>
        {
            if (Spr.Now - start < delay + 0.05 || Spr.IsMoving(card, Prop.Opacity) || Spr.IsMoving(card, Prop.TranslationY))
            {
                return true;
            }
            // A few rows a frame, nearest the fold first: drawing a row for the
            // first time costs (all ~80 at once: a 40 ms frame).
            for (var n = 0; n < TailRowsPerFrame && tail.Count > 0; n++)
            {
                tail[0].Opacity = 1;
                tail.RemoveAt(0);
            }
            FrameTrace.Mark("reveal.tail");
            return tail.Count > 0;
        }, card.Dispatcher);
    }

    private static IEnumerable<Layout> ItemLayouts(IVisualTreeElement e)
    {
        foreach (var child in e.GetVisualChildren())
        {
            if (child is Layout l && l is BindableObject b && GetItems(b))
            {
                yield return l;
            }
            else
            {
                foreach (var inner in ItemLayouts(child))
                {
                    yield return inner;
                }
            }
        }
    }

    private static bool ContainsCanvas(IVisualTreeElement e) =>
        e is GraphicsView || e.GetVisualChildren().Any(ContainsCanvas);

    // Called while a tab is parked and idle: everything its entrance will
    // animate gets its native transform now (Windows, WinTransform.Ensure),
    // not in the first frames of the entrance.
    public static void Warm(VisualElement root)
    {
#if WINDOWS
        var tagged = new List<VisualElement>();
        Collect(root, tagged);
        if (tagged.Count == 0)
        {
            tagged.AddRange(AutoItems(root));
        }
        foreach (var v in tagged)
        {
            GateControl.Platform.WinTransform.Ensure(v);
            foreach (var layout in ItemLayouts(v))
            {
                for (var i = 0; i < Math.Min(JoinMaxRows, layout.Children.Count); i++)
                {
                    if (layout.Children[i] is VisualElement row)
                    {
                        GateControl.Platform.WinTransform.Ensure(row);
                    }
                }
            }
        }
#endif
    }

    private static void Collect(IVisualTreeElement e, List<VisualElement> into)
    {
        if (e is VisualElement v && (GetOrder(v) >= 0 || !double.IsNaN(GetDelay(v))))
        {
            into.Add(v);
        }
        foreach (var child in e.GetVisualChildren())
        {
            Collect(child, into);
        }
    }

    // Untagged: descend through single-child wrappers (refresh, scroll,
    // a grid holding one visible layer) to the first layout with several
    // visible children; those are the "cards".
    internal static List<VisualElement> AutoItems(VisualElement root)
    {
        IVisualTreeElement current = root;
        for (var depth = 0; depth < 8; depth++)
        {
            var children = current.GetVisualChildren().OfType<VisualElement>().Where(c => c.IsVisible).ToList();
            if (children.Count == 0)
            {
                break;
            }
            if (children.Count > 1 && current is Layout)
            {
                return children;
            }
            current = children[0];
        }
        return current is VisualElement only && !ReferenceEquals(only, root) ? [only] : [root];
    }
}
