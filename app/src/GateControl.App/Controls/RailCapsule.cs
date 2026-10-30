// RailCapsule.cs — a vertical capsule of icon buttons (spec §2 left rail).
// The selected icon sits on a filled ink circle that slides between items
// on the same spring as the nav pill (A13); SelectedIndex −1 hides it.
// Icons respond on press (Pressable).

using GateControl.Core.Motion;
using GateControl.Motion;
using Microsoft.Maui.Controls.Shapes;

namespace GateControl.Controls;

public sealed class RailCapsule : ContentView
{
    // `Pressed` runs on pointer-down (before the tap), e.g. to start the
    // theme-reveal snapshot early.
    public sealed record Item(string Glyph, string Name, Action Invoke, Action? Pressed = null);

    private const double Cell = 28;
    private const double Gap = 8;

    private readonly Grid _layer = new() { Padding = 4 };
    private readonly Border _dot;
    private readonly List<IconView> _icons = [];
    private int _selected = -1;

    public RailCapsule()
    {
        var res = Application.Current!.Resources;
        _dot = new Border
        {
            StrokeThickness = 0,
            StrokeShape = new Ellipse(),
            WidthRequest = Cell,
            HeightRequest = Cell,
            VerticalOptions = LayoutOptions.Start,
            HorizontalOptions = LayoutOptions.Center,
            InputTransparent = true,
            Opacity = 0,
        };
        _dot.SetAppThemeColor(BackgroundColorProperty, (Color)res["InkLight"], (Color)res["InkDark"]);
        _layer.Children.Add(_dot);
        Content = new Border { Style = (Style)res["Capsule"], Content = _layer, WidthRequest = Cell + 8 };
        Application.Current.RequestedThemeChanged += (_, _) => Paint();
    }

    public void SetItems(IReadOnlyList<Item> items)
    {
        foreach (var child in _layer.Children.Where(c => !ReferenceEquals(c, _dot)).ToList())
        {
            _layer.Children.Remove(child);
        }
        _icons.Clear();
        _layer.RowDefinitions.Clear();
        for (var i = 0; i < items.Count; i++)
        {
            _layer.RowDefinitions.Add(new RowDefinition(new GridLength(Cell)));
            var item = items[i];
            var icon = new IconView { Glyph = item.Glyph, Size = 16 };
            var cell = new Grid { WidthRequest = Cell, HeightRequest = Cell, BackgroundColor = Colors.Transparent, Children = { icon } };
            SemanticProperties.SetDescription(cell, item.Name);
            ToolTipProperties.SetText(cell, item.Name);
            Pressable.SetCommand(cell, new Command(item.Invoke));
            if (item.Pressed is { } pressed)
            {
                var down = new PointerGestureRecognizer();
                down.PointerPressed += (_, _) => pressed();
                cell.GestureRecognizers.Add(down);
            }
            Grid.SetRow(cell, i);
            _layer.Children.Add(cell);
            _icons.Add(icon);
        }
        _layer.RowSpacing = Gap;
        Grid.SetRowSpan(_dot, Math.Max(1, items.Count));
        Select(_selected, animated: false);
    }

    public void Select(int index, bool animated = true)
    {
        _selected = index;
        var reduce = MotionSettings.ReduceMotion || !animated;
        if (index < 0 || index >= _icons.Count)
        {
            if (reduce) Spr.Set(_dot, Prop.Opacity, 0); else Spr.To(_dot, Prop.Opacity, 0, SpringSpec.Default);
            Paint();
            return;
        }
        var y = index * (Cell + Gap);
        var wasHidden = _dot.Opacity < 0.01;
        if (reduce || wasHidden)
        {
            Spr.Set(_dot, Prop.TranslationY, y);
        }
        else
        {
            Spr.To(_dot, Prop.TranslationY, y, SpringSpec.Default);
        }
        if (reduce) Spr.Set(_dot, Prop.Opacity, 1); else Spr.To(_dot, Prop.Opacity, 1, SpringSpec.Default);
        Paint();
    }

    private void Paint()
    {
        var res = Application.Current!.Resources;
        var dark = Application.Current.RequestedTheme == AppTheme.Dark;
        for (var i = 0; i < _icons.Count; i++)
        {
            _icons[i].Tint = i == _selected
                ? (Color)res[dark ? "OnInkDark" : "OnInkLight"]
                : null;
        }
    }
}
