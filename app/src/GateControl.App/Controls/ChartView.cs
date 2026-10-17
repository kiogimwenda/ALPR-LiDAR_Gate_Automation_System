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

    private readonly Spring _progress = new(1, SpringSpec.Move);
    private IDispatcherTimer? _timer;
    private double[] _from = [];
    private double[] _to = [];
    private double[] _from2 = [];
    private double[] _to2 = [];
    private long _last;

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
        var p = Math.Clamp(_progress.Value, 0, 1);
        _from = Blend(_from, _to, p);
        _from2 = Blend(_from2, _to2, p);
        _to = Primary.Select(x => x.Value).ToArray();
        _to2 = Second.Select(x => x.Value).ToArray();
        if (MotionSettings.ReduceMotion)
        {
            _progress.Snap(1);
            Invalidate();
            return;
        }
        _progress.Snap(0);
        _progress.Retarget(1);
        _last = Environment.TickCount64;
        if (_timer is null)
        {
            _timer = Dispatcher.CreateTimer();
            _timer.Interval = TimeSpan.FromMilliseconds(1000.0 / 120);
            _timer.Tick += (_, _) =>
            {
                var now = Environment.TickCount64;
                _progress.Step(Math.Min((now - _last) / 1000.0, 0.1));
                _last = now;
                Invalidate();
                if (_progress.IsSettled)
                {
                    _timer!.Stop();
                }
            };
        }
        _timer.Start();
    }

    private static double[] Blend(double[] from, double[] to, double p)
    {
        var n = Math.Max(from.Length, to.Length);
        var r = new double[to.Length];
        for (var i = 0; i < to.Length && i < n; i++)
        {
            var f = i < from.Length ? from[i] : 0;
            r[i] = f + (to[i] - f) * p;
        }
        return r;
    }

    private double[] Current(double[] from, double[] to) => Blend(from, to, Math.Max(0, _progress.Value));

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
        _ => Res("Accent"),
    };

    // Distinct, legible series colours for categorical data (donut slices).
    private static readonly string[] CategoryLight = ["#007AFF", "#34C759", "#FF9500", "#AF52DE", "#30B0C7", "#FF2D55", "#5856D6", "#8E8E93"];
    private static readonly string[] CategoryDark = ["#0A84FF", "#30D158", "#FF9F0A", "#BF5AF2", "#40C8E0", "#FF375F", "#5E5CE6", "#98989D"];

    public static Color CategoryColor(int i) =>
        Color.FromArgb((Dark ? CategoryDark : CategoryLight)[i % CategoryLight.Length]);

    // ---- drawing -----------------------------------------------------------------

    public void Draw(ICanvas canvas, RectF rect)
    {
        var points = Primary;
        if (points.Count == 0 || points.All(p => p.Value <= 0))
        {
            canvas.FontColor = Res("TertiaryLabel");
            canvas.FontSize = 15;
            canvas.DrawString("No data for this period", rect, HorizontalAlignment.Center, VerticalAlignment.Center);
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
            default:
                DrawBars(canvas, rect, points);
                break;
        }
    }

    private void Grid(ICanvas canvas, RectF plot, double max)
    {
        canvas.StrokeColor = Res("Separator");
        canvas.StrokeSize = 0.5f;
        canvas.FontColor = Res("SecondaryLabel");
        canvas.FontSize = 11;
        for (var i = 0; i <= 2; i++)
        {
            var y = plot.Bottom - plot.Height * i / 2f;
            canvas.DrawLine(plot.Left, y, plot.Right, y);
            var label = Display.Count((long)Math.Round(max * i / 2));
            canvas.DrawString(label, plot.Right + 4, y - 7, 40, 14, HorizontalAlignment.Left, VerticalAlignment.Center);
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
        canvas.FontColor = Res("SecondaryLabel");
        canvas.FontSize = 11;
        // Show at most ~6 labels so they never collide.
        var every = Math.Max(1, (int)Math.Ceiling(points.Count / 6.0));
        for (var i = 0; i < points.Count; i += every)
        {
            var cx = plot.Left + slot * (i + 0.5f);
            canvas.DrawString(points[i].Label, cx - 40, plot.Bottom + 4, 80, 16, HorizontalAlignment.Center,
                              VerticalAlignment.Top);
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
        canvas.FontSize = 13;
        for (var i = 0; i < points.Count; i++)
        {
            var y = rect.Top + row * i;
            canvas.FontColor = Res("Label");
            canvas.DrawString(points[i].Label, rect.Left, y, labelW - 8, row, HorizontalAlignment.Left, VerticalAlignment.Center);
            var w = (float)((rect.Width - labelW - 48) * Math.Max(0, i < values.Length ? values[i] : 0) / max);
            canvas.FillColor = ToneColor(points[i].Tone);
            canvas.FillRoundedRectangle(rect.Left + labelW, y + row * 0.25f, Math.Max(w, 2), row * 0.5f, 4);
            canvas.FontColor = Res("SecondaryLabel");
            canvas.DrawString(Display.Count((long)Math.Round(points[i].Value)), rect.Left + labelW + w + 6, y, 48, row,
                              HorizontalAlignment.Left, VerticalAlignment.Center);
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
        canvas.FontColor = Res("Label");
        canvas.FontSize = 22;
        canvas.Font = Microsoft.Maui.Graphics.Font.DefaultBold;
        canvas.DrawString(Display.Count((long)Math.Round(points.Sum(p => p.Value))), cx - 60, cy - 16, 120, 26,
                          HorizontalAlignment.Center, VerticalAlignment.Center);
        canvas.Font = Microsoft.Maui.Graphics.Font.Default;
        canvas.FontSize = 11;
        canvas.FontColor = Res("SecondaryLabel");
        canvas.DrawString("total", cx - 60, cy + 8, 120, 16, HorizontalAlignment.Center, VerticalAlignment.Center);
    }
}
