// ChartView.cs — bars, lines and donuts in the system's own visual language.
//
// Drawn with Microsoft.Maui.Graphics (no charting dependency). New data
// morphs from the values on screen to the new ones on a spring — so a
// refresh mid-animation continues from where it is (interruptible) —
// and under Reduce Motion it simply appears.

using GateControl.Core.Formatting;
using GateControl.Core.Motion;
using GateControl.Core.ViewModels;
using GateControl.Motion;

namespace GateControl.Controls;

public enum ChartKind
{
    Bars,
    Lines,
    Donut,
    HorizontalBars,
    // Points = base (ink), Secondary = cap (brand) stacked on it (spec A7).
    StackedBars,
}

public sealed class ChartView : GraphicsView, IDrawable
{
    public static readonly BindableProperty PointsProperty = BindableProperty.Create(
        nameof(Points), typeof(IEnumerable<ChartPoint>), typeof(ChartView), null,
        propertyChanged: (b, o, n) => ((ChartView)b).OnData(o, n));

    public static readonly BindableProperty SecondaryProperty = BindableProperty.Create(
        nameof(Secondary), typeof(IEnumerable<ChartPoint>), typeof(ChartView), null,
        propertyChanged: (b, o, n) => ((ChartView)b).OnData(o, n));

    public static readonly BindableProperty KindProperty = BindableProperty.Create(
        nameof(Kind), typeof(ChartKind), typeof(ChartView), ChartKind.Bars,
        propertyChanged: (b, _, _) => ((ChartView)b).Invalidate());

    // Seconds since the data last changed; every bar's progress is a pure
    // function of it (ChartGrow), so staggered bars share one clock.
    private double _t = 1e6;
    private bool _ticking;
    private double[] _from = [];
    private double[] _to = [];
    private double[] _from2 = [];
    private double[] _to2 = [];

    public ChartView()
    {
        Drawable = this;
        HeightRequest = 180;
        BackgroundColor = Colors.Transparent;
        Application.Current!.RequestedThemeChanged += (_, _) => Invalidate();
    }

    public IEnumerable<ChartPoint>? Points
    {
        get => (IEnumerable<ChartPoint>?)GetValue(PointsProperty);
        set => SetValue(PointsProperty, value);
    }

    // A second series (lines: e.g. exits next to entries).
    public IEnumerable<ChartPoint>? Secondary
    {
        get => (IEnumerable<ChartPoint>?)GetValue(SecondaryProperty);
        set => SetValue(SecondaryProperty, value);
    }

    public ChartKind Kind
    {
        get => (ChartKind)GetValue(KindProperty);
        set => SetValue(KindProperty, value);
    }

    private List<ChartPoint> Primary => Points?.ToList() ?? [];
    private List<ChartPoint> Second => Secondary?.ToList() ?? [];

    private void OnData(object? oldValue, object? newValue)
    {
        if (oldValue is System.Collections.Specialized.INotifyCollectionChanged oc)
        {
            oc.CollectionChanged -= OnCollectionChanged;
        }
        if (newValue is System.Collections.Specialized.INotifyCollectionChanged nc)
        {
            nc.CollectionChanged += OnCollectionChanged;
        }
        Retarget();
    }

    private void OnCollectionChanged(object? s, System.Collections.Specialized.NotifyCollectionChangedEventArgs e) =>
        Retarget();

    // Start from what is on screen now, head for the new values.
    private void Retarget()
    {
        _from = Displayed(_from, _to, cap: false);
        _from2 = Displayed(_from2, _to2, cap: Kind == ChartKind.StackedBars);
        _to = Primary.Select(x => x.Value).ToArray();
        _to2 = Second.Select(x => x.Value).ToArray();
        Restart();
    }

    private void Restart()
    {
        if (MotionSettings.ReduceMotion)
        {
            _t = 1e6;
            Invalidate();
            return;
        }
        _t = 0;
        if (_ticking)
        {
            return;
        }
        _ticking = true;
        // On the motion ticker (one step per displayed frame).
        Spr.OnFrame(dt =>
        {
            _t += dt;
            Invalidate();
            _ticking = _t <= ChartGrow.Duration(Math.Max(_to.Length, _to2.Length));
            return _ticking;
        }, Dispatcher);
    }

    // Grow the bars again from the baseline (verification of A7, and a
    // re-entrance once the page shows data it already had).
    public void Replay()
    {
        _from = new double[_to.Length];
        _from2 = new double[_to2.Length];
        Restart();
    }

    private bool Staggered => Kind is ChartKind.Bars or ChartKind.StackedBars;

    // Progress of item i: bars grow one after another (A7), caps after
    // their base; other charts morph together.
    // Bars stagger by their rank among the bars that have something to
    // show: empty hours would otherwise make the first real bar wait (with
    // data only at 19:00 it started 1.2 s late).
    private double Progress(int i, bool cap)
    {
        if (!Staggered)
        {
            return Spring.StepResponse(SpringSpec.Move, _t);
        }
        var rank = Rank(i);
        return cap ? ChartGrow.Cap(rank, _t) : ChartGrow.Base(rank, _t);
    }

    private int Rank(int i)
    {
        var rank = 0;
        for (var k = 0; k < i; k++)
        {
            var v = (k < _to.Length ? _to[k] : 0) + (k < _to2.Length ? _to2[k] : 0);
            if (v > 0)
            {
                rank++;
            }
        }
        return rank;
    }

    private double[] Displayed(double[] from, double[] to, bool cap)
    {
        var r = new double[to.Length];
        for (var i = 0; i < to.Length; i++)
        {
            var f = i < from.Length ? from[i] : 0;
            r[i] = f + (to[i] - f) * Progress(i, cap);
        }
        return r;
    }

    private double[] Current(double[] from, double[] to) => Displayed(from, to, cap: false);

    // ---- palette ---------------------------------------------------------------

    private static bool Dark => Application.Current?.RequestedTheme == AppTheme.Dark;

    private static Color Res(string key) =>
        (Color)Application.Current!.Resources[key + (Dark ? "Dark" : "Light")];

    public static Color ToneColor(Tone t) => t switch
    {
        Tone.Positive => Res("Positive"),
        Tone.Negative => Res("Negative"),
        Tone.Caution => Res("Caution"),
        Tone.Neutral => Res("Neutral"),
        // Data in the accent tone uses the brand orange: "Accent" is now
        // ink, which inverts with the theme, and converter results are not
        // re-evaluated on a theme switch (ink text would vanish in dark).
        _ => Res("Brand"),
    };

    // Distinct, legible series colours for categorical data (donut slices).
    private static readonly string[] CategoryLight = ["#151316", "#E15A41", "#1E9E5A", "#F5A524", "#8B888F", "#F2947F", "#6E6A73", "#C9C6CD"];
    private static readonly string[] CategoryDark = ["#F4F2F7", "#E15B43", "#3CCB7F", "#F5A524", "#8F8C94", "#F2947F", "#5C5961", "#3A363F"];

    public static Color CategoryColor(int i) =>
        Color.FromArgb((Dark ? CategoryDark : CategoryLight)[i % CategoryLight.Length]);

    // ---- drawing -----------------------------------------------------------------

    public void Draw(ICanvas canvas, RectF rect)
    {
        var t0 = FrameTrace.Now();
        DrawCore(canvas, rect);
        FrameTrace.Span("chart.draw", t0);
    }

    private void DrawCore(ICanvas canvas, RectF rect)
    {
        var points = Primary;
        if (points.Count == 0 || (points.All(p => p.Value <= 0) && Second.All(p => p.Value <= 0)))
        {
            Text(canvas, "No data for this period", rect.X, rect.Y, rect.Width, rect.Height, HorizontalAlignment.Center,
                 VerticalAlignment.Center, Res("TertiaryLabel"), 15);
            return;
        }
        switch (Kind)
        {
            case ChartKind.Donut:
                DrawDonut(canvas, rect, points);
                break;
            case ChartKind.Lines:
                DrawLines(canvas, rect, points);
                break;
            case ChartKind.HorizontalBars:
                DrawHorizontalBars(canvas, rect, points);
                break;
            case ChartKind.StackedBars:
                DrawStacked(canvas, rect, points);
                break;
            default:
                DrawBars(canvas, rect, points);
                break;
        }
    }

    private void Grid(ICanvas canvas, RectF plot, double max)
    {
        canvas.StrokeColor = Res("Separator");
        canvas.StrokeSize = 0.5f;
        for (var i = 0; i <= 2; i++)
        {
            var y = plot.Bottom - plot.Height * i / 2f;
            canvas.DrawLine(plot.Left, y, plot.Right, y);
            var label = Display.Count((long)Math.Round(max * i / 2));
            Text(canvas, label, plot.Right + 4, y - 7, 40, 14, HorizontalAlignment.Left, VerticalAlignment.Center, Res("SecondaryLabel"), 11);
        }
    }

    private static double NiceMax(double v)
    {
        if (v <= 0)
        {
            return 1;
        }
        var mag = Math.Pow(10, Math.Floor(Math.Log10(v)));
        foreach (var step in new[] { 1, 2, 2.5, 5, 10 })
        {
            if (v <= step * mag)
            {
                return step * mag;
            }
        }
        return 10 * mag;
    }

    private void XLabels(ICanvas canvas, RectF plot, List<ChartPoint> points, float slot)
    {
        // Show at most ~6 labels so they never collide.
        var every = Math.Max(1, (int)Math.Ceiling(points.Count / 6.0));
        for (var i = 0; i < points.Count; i += every)
        {
            var cx = plot.Left + slot * (i + 0.5f);
            Text(canvas, points[i].Label, cx - 40, plot.Bottom + 4, 80, 16, HorizontalAlignment.Center,
                 VerticalAlignment.Top, Res("SecondaryLabel"), 11);
        }
    }

    private void DrawBars(ICanvas canvas, RectF rect, List<ChartPoint> points)
    {
        var plot = new RectF(rect.Left, rect.Top + 6, rect.Width - 44, rect.Height - 30);
        var values = Current(_from, _to);
        var max = NiceMax(Math.Max(_to.DefaultIfEmpty(0).Max(), values.DefaultIfEmpty(0).Max()));
        Grid(canvas, plot, max);
        var slot = plot.Width / points.Count;
        var bar = Math.Max(2, Math.Min(28, slot * 0.62f));
        for (var i = 0; i < points.Count; i++)
        {
            var v = i < values.Length ? Math.Max(0, values[i]) : 0;
            var h = (float)(plot.Height * v / max);
            if (h <= 0.5f)
            {
                continue;
            }
            var x = plot.Left + slot * i + (slot - bar) / 2;
            canvas.FillColor = ToneColor(points[i].Tone);
            canvas.FillRoundedRectangle(x, plot.Bottom - h, bar, h, Math.Min(bar / 2, 5), Math.Min(bar / 2, 5), 0, 0);
        }
        XLabels(canvas, plot, points, slot);
    }

    // Base (allowed, ink) with the cap (refused, brand) stacked on top; only
    // the top segment has rounded corners, as in the reference chart.
    private void DrawStacked(ICanvas canvas, RectF rect, List<ChartPoint> points)
    {
        var plot = new RectF(rect.Left, rect.Top + 6, rect.Width - 44, rect.Height - 30);
        var baseV = Displayed(_from, _to, cap: false);
        var capV = Displayed(_from2, _to2, cap: true);
        double total(int i) => (i < _to.Length ? _to[i] : 0) + (i < _to2.Length ? _to2[i] : 0);
        var max = NiceMax(Enumerable.Range(0, points.Count).Select(total).DefaultIfEmpty(0).Max());
        Grid(canvas, plot, max);
        var slot = plot.Width / points.Count;
        var bar = Math.Max(2, Math.Min(28, slot * 0.6f));
        var r = Math.Min(bar / 2, 6);
        var ink = Res("Label");
        var brand = Res("Brand");
        for (var i = 0; i < points.Count; i++)
        {
            var hb = (float)(plot.Height * Math.Max(0, i < baseV.Length ? baseV[i] : 0) / max);
            var hc = (float)(plot.Height * Math.Max(0, i < capV.Length ? capV[i] : 0) / max);
            var x = plot.Left + slot * i + (slot - bar) / 2;
            if (hb > 0.5f)
            {
                canvas.FillColor = ink;
                var top = hc > 0.5f ? 0 : r;
                canvas.FillRoundedRectangle(x, plot.Bottom - hb, bar, hb, top, top, 0, 0);
            }
            if (hc > 0.5f)
            {
                canvas.FillColor = brand;
                canvas.FillRoundedRectangle(x, plot.Bottom - hb - hc, bar, hc, r, r, 0, 0);
            }
        }
        XLabels(canvas, plot, points, slot);
    }

    private void DrawLines(ICanvas canvas, RectF rect, List<ChartPoint> points)
    {
        var plot = new RectF(rect.Left + 2, rect.Top + 6, rect.Width - 46, rect.Height - 30);
        var a = Current(_from, _to);
        var b = Current(_from2, _to2);
        var max = NiceMax(Math.Max(_to.DefaultIfEmpty(0).Max(), _to2.DefaultIfEmpty(0).Max()));
        Grid(canvas, plot, max);
        var slot = plot.Width / points.Count;
        void Series(double[] values, Color color)
        {
            if (values.Length < 2)
            {
                return;
            }
            var path = new PathF();
            for (var i = 0; i < values.Length; i++)
            {
                var x = plot.Left + slot * (i + 0.5f);
                var y = plot.Bottom - (float)(plot.Height * Math.Max(0, values[i]) / max);
                if (i == 0)
                {
                    path.MoveTo(x, y);
                }
                else
                {
                    path.LineTo(x, y);
                }
            }
            canvas.StrokeColor = color;
            canvas.StrokeSize = 2.5f;
            canvas.StrokeLineJoin = LineJoin.Round;
            canvas.StrokeLineCap = LineCap.Round;
            canvas.DrawPath(path);
        }
        Series(a, ToneColor(points[0].Tone));
        var second = Second;
        if (second.Count > 0)
        {
            Series(b, ToneColor(second[0].Tone));
        }
        XLabels(canvas, plot, points, slot);
    }

    private void DrawHorizontalBars(ICanvas canvas, RectF rect, List<ChartPoint> points)
    {
        var values = Current(_from, _to);
        var max = Math.Max(1, _to.DefaultIfEmpty(0).Max());
        var row = Math.Min(30, rect.Height / Math.Max(1, points.Count));
        var labelW = Math.Min(140, rect.Width * 0.4f);
        for (var i = 0; i < points.Count; i++)
        {
            var y = rect.Top + row * i;
            Text(canvas, points[i].Label, rect.Left, y, labelW - 8, row, HorizontalAlignment.Left, VerticalAlignment.Center, Res("Label"), 13);
            var w = (float)((rect.Width - labelW - 48) * Math.Max(0, i < values.Length ? values[i] : 0) / max);
            canvas.FillColor = ToneColor(points[i].Tone);
            canvas.FillRoundedRectangle(rect.Left + labelW, y + row * 0.25f, Math.Max(w, 2), row * 0.5f, 4);
            Text(canvas, Display.Count((long)Math.Round(points[i].Value)), rect.Left + labelW + w + 6, y, 48, row,
                 HorizontalAlignment.Left, VerticalAlignment.Center, Res("SecondaryLabel"), 13);
        }
    }

    private void DrawDonut(ICanvas canvas, RectF rect, List<ChartPoint> points)
    {
        var values = Current(_from, _to);
        var total = values.Sum(v => Math.Max(0, v));
        if (total <= 0)
        {
            return;
        }
        var size = Math.Min(rect.Width, rect.Height) - 8;
        var cx = rect.Center.X;
        var cy = rect.Center.Y;
        var thickness = size * 0.16f;
        var radius = (size - thickness) / 2;
        var start = 90f;
        canvas.StrokeSize = thickness;
        canvas.StrokeLineCap = LineCap.Butt;
        for (var i = 0; i < points.Count && i < values.Length; i++)
        {
            var sweep = (float)(360 * Math.Max(0, values[i]) / total);
            if (sweep < 0.5f)
            {
                continue;
            }
            canvas.StrokeColor = points[i].Tone == Tone.Accent ? CategoryColor(i) : ToneColor(points[i].Tone);
            // A hairline gap between slices.
            var gap = Math.Min(2f, sweep / 4);
            canvas.DrawArc(cx - radius, cy - radius, radius * 2, radius * 2, start - gap / 2, start - sweep + gap / 2,
                           clockwise: true, closed: false);
            start -= sweep;
        }
        Text(canvas, Display.Count((long)Math.Round(points.Sum(p => p.Value))), cx - 60, cy - 16, 120, 26,
             HorizontalAlignment.Center, VerticalAlignment.Center, Res("Label"), 22, bold: true);
        Text(canvas, "total", cx - 60, cy + 8, 120, 16, HorizontalAlignment.Center, VerticalAlignment.Center, Res("SecondaryLabel"), 11);
    }

    // All chart text. On Windows it is drawn straight onto the Win2D session
    // with a text format made once per size: MAUI's DrawString builds a new
    // WinUI FontFamily for every call, i.e. per label per frame while the
    // bars grow, and those objects drove the GC (EventPipe: GCTriggered
    // InducedNotForced from W2DCanvas.DrawString → FontFamily, and XAML's
    // reference tracker inducing a blocking gen-2 1 s into the first
    // Dashboard entrance).
    private static void Text(ICanvas canvas, string text, float x, float y, float w, float h,
                             HorizontalAlignment ha, VerticalAlignment va, Color color, float size, bool bold = false)
    {
#if WINDOWS
        if (canvas is Microsoft.Maui.Graphics.Win2D.W2DCanvas { Session: { } session })
        {
            session.DrawText(text, new global::Windows.Foundation.Rect(x, y, w, h),
                global::Windows.UI.Color.FromArgb((byte)(color.Alpha * 255), (byte)(color.Red * 255),
                                                  (byte)(color.Green * 255), (byte)(color.Blue * 255)),
                TextFormats.Get(size, bold, ha, va));
            return;
        }
#endif
        canvas.FontColor = color;
        canvas.FontSize = size;
        canvas.Font = bold ? Microsoft.Maui.Graphics.Font.DefaultBold : Microsoft.Maui.Graphics.Font.Default;
        canvas.DrawString(text, x, y, w, h, ha, va);
    }

#if WINDOWS
    private static class TextFormats
    {
        private static readonly Dictionary<(float, bool, HorizontalAlignment, VerticalAlignment), Microsoft.Graphics.Canvas.Text.CanvasTextFormat> Cache = [];
        private static string? _family;

        public static Microsoft.Graphics.Canvas.Text.CanvasTextFormat Get(float size, bool bold, HorizontalAlignment ha, VerticalAlignment va)
        {
            if (Cache.TryGetValue((size, bold, ha, va), out var f))
            {
                return f;
            }
            // The family MAUI's DrawString uses for Font.Default (WinUI's
            // XamlAutoFontFamily), read once.
            _family ??= Microsoft.UI.Xaml.Media.FontFamily.XamlAutoFontFamily.Source;
            f = new Microsoft.Graphics.Canvas.Text.CanvasTextFormat
            {
                FontFamily = _family,
                FontSize = size,
                FontWeight = bold ? Microsoft.UI.Text.FontWeights.Bold : Microsoft.UI.Text.FontWeights.Normal,
            };
            f.HorizontalAlignment = ha switch
            {
                HorizontalAlignment.Center => Microsoft.Graphics.Canvas.Text.CanvasHorizontalAlignment.Center,
                HorizontalAlignment.Right => Microsoft.Graphics.Canvas.Text.CanvasHorizontalAlignment.Right,
                _ => Microsoft.Graphics.Canvas.Text.CanvasHorizontalAlignment.Left,
            };
            f.VerticalAlignment = va switch
            {
                VerticalAlignment.Center => Microsoft.Graphics.Canvas.Text.CanvasVerticalAlignment.Center,
                VerticalAlignment.Bottom => Microsoft.Graphics.Canvas.Text.CanvasVerticalAlignment.Bottom,
                _ => Microsoft.Graphics.Canvas.Text.CanvasVerticalAlignment.Top,
            };
            f.WordWrapping = Microsoft.Graphics.Canvas.Text.CanvasWordWrapping.NoWrap;
            f.Options = Microsoft.Graphics.Canvas.Text.CanvasDrawTextOptions.Clip;
            Cache[(size, bold, ha, va)] = f;
            return f;
        }
    }
#endif
}
