// TileGrid.cs — equal-width tiles that wrap into as many columns as fit
// (at least MinTileWidth each), recomputed from the width it is actually
// given at arrange time. Replaces nested FlexLayouts: inside a FlexLayout
// item, a wrapping FlexLayout was measured unconstrained and its tiles ran
// past the card's edge; and on phones a 340-dip basis left the cards short
// of the column's right edge.

using GateControl.Core.Formatting;
using Microsoft.Maui.Layouts;

namespace GateControl.Controls;

public sealed class TileGrid : Layout
{
    public static readonly BindableProperty MinTileWidthProperty = BindableProperty.Create(
        nameof(MinTileWidth), typeof(double), typeof(TileGrid), 150.0,
        propertyChanged: (b, _, _) => ((TileGrid)b).InvalidateMeasure());

    public static readonly BindableProperty SpacingProperty = BindableProperty.Create(
        nameof(Spacing), typeof(double), typeof(TileGrid), 8.0,
        propertyChanged: (b, _, _) => ((TileGrid)b).InvalidateMeasure());

    public static readonly BindableProperty MaxColumnsProperty = BindableProperty.Create(
        nameof(MaxColumns), typeof(int), typeof(TileGrid), 6,
        propertyChanged: (b, _, _) => ((TileGrid)b).InvalidateMeasure());

    public double MinTileWidth
    {
        get => (double)GetValue(MinTileWidthProperty);
        set => SetValue(MinTileWidthProperty, value);
    }

    public double Spacing
    {
        get => (double)GetValue(SpacingProperty);
        set => SetValue(SpacingProperty, value);
    }

    public int MaxColumns
    {
        get => (int)GetValue(MaxColumnsProperty);
        set => SetValue(MaxColumnsProperty, value);
    }

    protected override ILayoutManager CreateLayoutManager() => new Manager(this);

    // (A short last row is stretched across the width, so the grid never shows
    // an empty cell.)
    // Columns for a given content width (pure; unit-tested via TileMath).
    internal int Columns(double width, int count) =>
        TileMath.Columns(width, count, MinTileWidth, Spacing, MaxColumns);

    private sealed class Manager(TileGrid grid) : ILayoutManager
    {
        private List<IView> Visible => grid.Where(c => c.Visibility != Visibility.Collapsed).ToList();

        public Size Measure(double widthConstraint, double heightConstraint)
        {
            var t0 = Motion.FrameTrace.Now();
            try
            {
                return MeasureCore(widthConstraint, heightConstraint);
            }
            finally
            {
                Motion.FrameTrace.Span("tilegrid.measure", t0);
            }
        }

        private Size MeasureCore(double widthConstraint, double heightConstraint)
        {
            var pad = grid.Padding;
            var children = Visible;
            // Unconstrained (e.g. measured as a FlexLayout item): ask for one
            // column only. Asking for the full row made the parent Border
            // arrange us wider than itself; at arrange time the real width
            // decides the columns anyway.
            var width = double.IsFinite(widthConstraint)
                ? widthConstraint - pad.HorizontalThickness
                : grid.MinTileWidth;
            var cols = grid.Columns(width, children.Count);
            var tileW = TileMath.TileWidth(width, cols, grid.Spacing);
            double height = 0, row = 0;
            for (var i = 0; i < children.Count; i++)
            {
                var h = children[i].Measure(tileW, double.PositiveInfinity).Height;
                row = Math.Max(row, h);
                if ((i + 1) % cols == 0 || i == children.Count - 1)
                {
                    height += row + (i == children.Count - 1 ? 0 : grid.Spacing);
                    row = 0;
                }
            }
            return new Size(width + pad.HorizontalThickness, height + pad.VerticalThickness);
        }

        public Size ArrangeChildren(Rect bounds)
        {
            var t0 = Motion.FrameTrace.Now();
            try
            {
                return ArrangeCore(bounds);
            }
            finally
            {
                Motion.FrameTrace.Span("tilegrid.arrange", t0);
            }
        }

        private Size ArrangeCore(Rect bounds)
        {
            var pad = grid.Padding;
            var children = Visible;
            var width = bounds.Width - pad.HorizontalThickness;
            var cols = grid.Columns(width, children.Count);
            var tileW = TileMath.TileWidth(width, cols, grid.Spacing);
            double y = bounds.Top + pad.Top;
            for (var start = 0; start < children.Count; start += cols)
            {
                var rowItems = children.Skip(start).Take(cols).ToList();
                // A short last row shares the full width (no empty cells).
                var w = rowItems.Count < cols ? TileMath.TileWidth(width, rowItems.Count, grid.Spacing) : tileW;
                var rowH = rowItems.Max(c => c.Measure(w, double.PositiveInfinity).Height);
                for (var k = 0; k < rowItems.Count; k++)
                {
                    var x = bounds.Left + pad.Left + k * (w + grid.Spacing);
                    rowItems[k].Arrange(new Rect(x, y, w, rowH));
                }
                y += rowH + grid.Spacing;
            }
            return bounds.Size;
        }
    }
}
