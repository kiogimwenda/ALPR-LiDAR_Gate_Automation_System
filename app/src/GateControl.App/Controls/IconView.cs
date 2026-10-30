// IconView.cs — a Lucide outline icon (Icons.cs) as a stroked path: crisp
// at any size, and its colour follows the theme (Label colour by default).
//
// Drawn as the stroke of a Border whose shape is the glyph, so each
// platform's native shape renderer draws it (WinUI XAML Path, Android
// drawable). MAUI's Path control draws through Win2D on Windows, which
// threw "Element not found" (D2D 0x88990011) while clipping by some glyphs
// (Car, Activity) and took the app down.

using Microsoft.Maui.Controls.Shapes;

namespace GateControl.Controls;

public sealed class IconView : ContentView
{
    public static readonly BindableProperty GlyphProperty = BindableProperty.Create(
        nameof(Glyph), typeof(string), typeof(IconView), null,
        propertyChanged: (b, _, _) => ((IconView)b).Build());

    public static readonly BindableProperty SizeProperty = BindableProperty.Create(
        nameof(Size), typeof(double), typeof(IconView), 16.0,
        propertyChanged: (b, _, _) => ((IconView)b).Build());

    public static readonly BindableProperty TintProperty = BindableProperty.Create(
        nameof(Tint), typeof(Color), typeof(IconView), null,
        propertyChanged: (b, _, _) => ((IconView)b).Paint());

    private static readonly PathGeometryConverter Converter = new();

    private readonly Border _path = new()
    {
        StrokeLineCap = PenLineCap.Round,
        StrokeLineJoin = PenLineJoin.Round,
        Background = Brush.Transparent,
        Padding = 0,
        InputTransparent = true,
    };

    public IconView()
    {
        Content = _path;
        HorizontalOptions = LayoutOptions.Center;
        VerticalOptions = LayoutOptions.Center;
        InputTransparent = true;
        Build();
        Paint();
    }

    public string? Glyph
    {
        get => (string?)GetValue(GlyphProperty);
        set => SetValue(GlyphProperty, value);
    }

    public double Size
    {
        get => (double)GetValue(SizeProperty);
        set => SetValue(SizeProperty, value);
    }

    // Null: the theme's label colour.
    public Color? Tint
    {
        get => (Color?)GetValue(TintProperty);
        set => SetValue(TintProperty, value);
    }

    private void Build()
    {
        WidthRequest = Size;
        HeightRequest = Size;
        var scale = Size / 24;
        // Lucide draws a 2-unit stroke on its 24-unit grid.
        _path.StrokeThickness = Math.Max(1.25, 2 * scale);
        // The geometry itself is scaled to the icon size (a RenderTransform
        // on the path clipped icons whose glyph changed after layout).
        _path.StrokeShape = string.IsNullOrEmpty(Glyph)
            ? null
            : new Microsoft.Maui.Controls.Shapes.Path { Data = Scaled((Geometry)Converter.ConvertFromInvariantString(Glyph)!, scale) };
    }

    private static Geometry Scaled(Geometry g, double k)
    {
        if (g is not PathGeometry path)
        {
            return g;
        }
        Point P(Point p) => new(p.X * k, p.Y * k);
        var result = new PathGeometry { FillRule = path.FillRule };
        foreach (var f in path.Figures)
        {
            var figure = new PathFigure { StartPoint = P(f.StartPoint), IsClosed = f.IsClosed, IsFilled = f.IsFilled };
            foreach (var seg in f.Segments)
            {
                figure.Segments.Add(seg switch
                {
                    LineSegment l => new LineSegment(P(l.Point)),
                    PolyLineSegment pl => new PolyLineSegment(new PointCollection(pl.Points.Select(P).ToArray())),
                    BezierSegment b => new BezierSegment(P(b.Point1), P(b.Point2), P(b.Point3)),
                    PolyBezierSegment pb => new PolyBezierSegment(new PointCollection(pb.Points.Select(P).ToArray())),
                    QuadraticBezierSegment q => new QuadraticBezierSegment(P(q.Point1), P(q.Point2)),
                    PolyQuadraticBezierSegment pq => new PolyQuadraticBezierSegment(new PointCollection(pq.Points.Select(P).ToArray())),
                    ArcSegment a => new ArcSegment(P(a.Point), new Size(a.Size.Width * k, a.Size.Height * k),
                                                   a.RotationAngle, a.SweepDirection, a.IsLargeArc),
                    _ => seg,
                });
            }
            result.Figures.Add(figure);
        }
        return result;
    }

    private void Paint()
    {
        if (Tint is { } tint)
        {
            _path.Stroke = new SolidColorBrush(tint);
            return;
        }
        var res = Application.Current!.Resources;
        _path.SetAppTheme<Brush>(Border.StrokeProperty,
            new SolidColorBrush((Color)res["LabelLight"]), new SolidColorBrush((Color)res["LabelDark"]));
    }
}
