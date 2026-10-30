// TwoPane.cs — the reference's Messages layout: list and detail side by
// side when the tab is wide enough, the list alone (detail pushed) when not.

namespace GateControl.Pages;

internal static class TwoPane
{
    public static void Apply(Grid panes, View detail, double minWidth)
    {
        var wide = panes.Width >= minWidth;
        if (wide == detail.IsVisible && panes.ColumnDefinitions.Count == (wide ? 2 : 1))
        {
            return;
        }
        panes.ColumnDefinitions.Clear();
        panes.ColumnDefinitions.Add(new ColumnDefinition(new GridLength(wide ? 2 : 1, GridUnitType.Star)));
        if (wide)
        {
            panes.ColumnDefinitions.Add(new ColumnDefinition(new GridLength(3, GridUnitType.Star)));
        }
        detail.IsVisible = wide;
    }
}
