// NavCapsule.cs — the top navigation (spec §8 "Nav capsule with sliding
// pill", animation A8): a white capsule of text items with one ink pill
// behind the selected one. One long-lived control in the persistent root,
// so the pill is one continuous motion across tab switches.
//
//   * Items are content-width; the pill follows both position and width.
//     Width is never animated as layout: the pill is three parts — two
//     round end caps moved by TranslationX and a middle bar stretched by
//     ScaleX — all driven by the same two springs (x, width), critically
//     damped, response 0.35 s (measured 0.36 s, spec A8).
//   * Interruptible: a new selection mid-slide retargets both springs from
//     where the pill is, keeping its velocity.
//   * Labels cross-fade ink ↔ on-ink (two stacked labels, opacity only).
//   * SelectedIndex −1 (a page that isn't in the nav): the pill fades out.
//   * Reduce Motion: the pill jumps; labels swap at once.

using GateControl.Core.Motion;
using GateControl.Motion;
using Microsoft.Maui.Controls.Shapes;

namespace GateControl.Controls;

public sealed class NavCapsule : ContentView
{
    public static readonly BindableProperty SelectedIndexProperty = BindableProperty.Create(
        nameof(SelectedIndex), typeof(int), typeof(NavCapsule), -1, BindingMode.TwoWay,
        propertyChanged: (b, _, _) => ((NavCapsule)b).Update(animated: true));

    private const double ItemHeight = 32;
    private const double MidBase = 100;

    private readonly HorizontalStackLayout _items = new() { Spacing = 0 };
    private readonly Grid _pill = new() { InputTransparent = true, HorizontalOptions = LayoutOptions.Start };
    private readonly Border _left;
    private readonly Border _right;
    private readonly BoxView _middle;
    private readonly List<(Grid Cell, Label Ink, Label OnInk)> _cells = [];
    private readonly Spring _x = new(0, SpringSpec.Default);
    private readonly Spring _w = new(0, SpringSpec.Default);
    private bool _ticking;
    private bool _placed;

    public NavCapsule()
    {
        _left = Cap();
        _right = Cap();
        _middle = new BoxView
        {
            WidthRequest = MidBase,
            HeightRequest = ItemHeight,
            AnchorX = 0,
            HorizontalOptions = LayoutOptions.Start,
            VerticalOptions = LayoutOptions.Center,
        };
        Ink(_middle, BoxView.ColorProperty);
        _pill.Children.Add(_middle);
        _pill.Children.Add(_left);
        _pill.Children.Add(_right);
        _pill.Opacity = 0;

        var layer = new Grid { Children = { _pill, _items } };
        var capsule = new Border { Style = (Style)Application.Current!.Resources["Capsule"], Padding = 4, Content = layer };
        Content = capsule;
        _items.SizeChanged += (_, _) => Update(animated: _placed);
    }

    public event EventHandler<int>? ItemTapped;

    public int SelectedIndex
    {
        get => (int)GetValue(SelectedIndexProperty);
        set => SetValue(SelectedIndexProperty, value);
    }

    // Compact (phone, bottom): smaller type and padding.
    public bool Compact { get; set; }

    public void SetItems(IEnumerable<string> titles)
    {
        _items.Children.Clear();
        _cells.Clear();
        var i = 0;
        foreach (var title in titles)
        {
            var index = i++;
            var ink = ItemLabel(title, onInk: false);
            var onInk = ItemLabel(title, onInk: true);
            onInk.Opacity = 0;
            var cell = new Grid
            {
                HeightRequest = ItemHeight,
                Padding = new Thickness(Compact ? 11 : 16, 0),
                BackgroundColor = Colors.Transparent,
                Children = { ink, onInk },
            };
            SemanticProperties.SetDescription(cell, title);
            Pressable.SetCommand(cell, new Command(() =>
            {
                ItemTapped?.Invoke(this, index);
                SelectedIndex = index;
            }));
            cell.SizeChanged += (_, _) => Update(animated: _placed);
            _items.Children.Add(cell);
            _cells.Add((cell, ink, onInk));
        }
        _placed = false;
        Update(animated: false);
    }

    // For verification: where the pill is now (left edge, width).
    public (double X, double Width) PillGeometry => (_x.Value, _w.Value);

    private Label ItemLabel(string text, bool onInk)
    {
        var res = Application.Current!.Resources;
        var label = new Label
        {
            Text = text,
            FontFamily = "InterMedium",
            FontSize = Compact ? 12 : 13,
            VerticalTextAlignment = TextAlignment.Center,
            HorizontalTextAlignment = TextAlignment.Center,
            InputTransparent = true,
        };
        if (onInk)
        {
            label.SetAppThemeColor(Label.TextColorProperty, (Color)res["OnInkLight"], (Color)res["OnInkDark"]);
        }
        else
        {
            label.SetAppThemeColor(Label.TextColorProperty, (Color)res["LabelLight"], (Color)res["LabelDark"]);
        }
        return label;
    }

    private static Border Cap()
    {
        var cap = new Border
        {
            StrokeThickness = 0,
            StrokeShape = new RoundRectangle { CornerRadius = ItemHeight / 2 },
            WidthRequest = ItemHeight,
            HeightRequest = ItemHeight,
            HorizontalOptions = LayoutOptions.Start,
            VerticalOptions = LayoutOptions.Center,
        };
        Ink(cap, BackgroundColorProperty);
        return cap;
    }

    private static void Ink(BindableObject view, BindableProperty property)
    {
        var res = Application.Current!.Resources;
        view.SetAppThemeColor(property, (Color)res["InkLight"], (Color)res["InkDark"]);
    }

    private void Update(bool animated)
    {
        var index = SelectedIndex;
        var reduce = MotionSettings.ReduceMotion;
        for (var i = 0; i < _cells.Count; i++)
        {
            var on = i == index;
            if (animated && !reduce)
            {
                Spr.To(_cells[i].OnInk, Prop.Opacity, on ? 1 : 0, SpringSpec.Default);
                Spr.To(_cells[i].Ink, Prop.Opacity, on ? 0 : 1, SpringSpec.Default);
            }
            else
            {
                Spr.Set(_cells[i].OnInk, Prop.Opacity, on ? 1 : 0);
                Spr.Set(_cells[i].Ink, Prop.Opacity, on ? 0 : 1);
            }
        }
        if (index < 0 || index >= _cells.Count)
        {
            if (animated && !reduce)
            {
                Spr.To(_pill, Prop.Opacity, 0, SpringSpec.Default);
            }
            else
            {
                Spr.Set(_pill, Prop.Opacity, 0);
            }
            return;
        }
        var cell = _cells[index].Cell;
        if (cell.Width <= 0)
        {
            return;  // not laid out yet; SizeChanged calls back
        }
        var x = cell.X;
        var w = Math.Max(cell.Width, ItemHeight);
        var wasHidden = _pill.Opacity < 0.01 && !Spr.IsMoving(_pill, Prop.Opacity);
        if (!_placed || !animated || reduce || wasHidden)
        {
            _x.Snap(x);
            _w.Snap(w);
            _placed = true;
        }
        else
        {
            _x.Retarget(x);
            _w.Retarget(w);
        }
        if (animated && !reduce)
        {
            Spr.To(_pill, Prop.Opacity, 1, SpringSpec.Default);
        }
        else
        {
            Spr.Set(_pill, Prop.Opacity, 1);
        }
        Apply();
        if (!_x.IsSettled || !_w.IsSettled)
        {
            StartTicking();
        }
    }

    private void StartTicking()
    {
        if (_ticking)
        {
            return;
        }
        _ticking = true;
        Spr.OnFrame(dt =>
        {
            _x.Step(dt);
            _w.Step(dt);
            Apply();
            _ticking = !(_x.IsSettled && _w.IsSettled);
            return _ticking;
        }, Dispatcher);
    }

    private void Apply()
    {
        var t0 = FrameTrace.Now();
        ApplyCore();
        FrameTrace.Span("nav.apply", t0);
    }

    private void ApplyCore()
    {
        var x = _x.Value;
        var w = Math.Max(ItemHeight, _w.Value);
        _left.TranslationX = x;
        _right.TranslationX = x + w - ItemHeight;
        _middle.TranslationX = x + ItemHeight / 2;
        _middle.ScaleX = Math.Max(0, w - ItemHeight) / MidBase;
    }
}
