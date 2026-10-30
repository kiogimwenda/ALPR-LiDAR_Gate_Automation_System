// ScrollFit.cs — keeps a vertical ScrollView's content exactly as wide as
// the ScrollView. On Windows a wrapping FlexLayout (or a negative margin)
// inside the content made the content desire more width than the
// viewport, and the ScrollView arranged it wider: cards ran past the right
// edge and were clipped. A RefreshView did the same to its ScrollView, so
// its content is pinned to the RefreshView's width too.

namespace GateControl.Controls;

public static class ScrollFit
{
    // Hooks every vertical ScrollView under `root`.
    public static void Apply(IVisualTreeElement root)
    {
        if (root is ScrollView scroll && scroll.Orientation == ScrollOrientation.Vertical)
        {
            Hook(scroll);
        }
        else if (root is RefreshView refresh)
        {
            void FitRefresh()
            {
                if (refresh.Content is View content && refresh.Width > 0 && Math.Abs(content.WidthRequest - refresh.Width) > 0.5)
                {
                    content.WidthRequest = refresh.Width;
                }
            }
            refresh.SizeChanged += (_, _) => FitRefresh();
            FitRefresh();
        }
        foreach (var child in root.GetVisualChildren())
        {
            Apply(child);
        }
    }

    private static void Hook(ScrollView scroll)
    {
        void Fit()
        {
            if (scroll.Content is View content && scroll.Width > 0)
            {
                var w = scroll.Width - scroll.Padding.HorizontalThickness - content.Margin.HorizontalThickness;
                if (Math.Abs(content.WidthRequest - w) > 0.5)
                {
                    content.WidthRequest = w;
                }
            }
        }
        scroll.SizeChanged += (_, _) => Fit();
        Fit();
    }
}
