// SegmentedControl.cs — iOS-style segmented control.
//
// The selection thumb slides on a spring and can be redirected mid-slide
// (tap another segment while it moves: it turns around from where it is).
// Segments respond on touch-down. Under Reduce Motion the thumb jumps.

using GateControl.Core.Motion;
using GateControl.Motion;
using Microsoft.Maui.Controls.Shapes;

namespace GateControl.Controls;

public sealed class SegmentedControl : ContentView
{
    public static readonly BindableProperty ItemsProperty = BindableProperty.Create(
        nameof(Items), typeof(IList<string>), typeof(SegmentedControl), null,
        propertyChanged: (b, _, _) => ((SegmentedControl)b).Build());

    public static readonly BindableProperty SelectedIndexProperty = BindableProperty.Create(
        nameof(SelectedIndex), typeof(int), typeof(SegmentedControl), 0, BindingMode.TwoWay,
        propertyChanged: (b, _, _) => ((SegmentedControl)b).MoveThumb(animated: true));

    private readonly Grid _grid = new() { ColumnSpacing = 0, Padding = 2 };
    private readonly Border _thumb;
    private readonly List<Label> _labels = [];

    public SegmentedControl()
    {
        Pressable.SetArrows(this, step =>
        {
            var count = Items?.Count ?? 0;
            if (count == 0)
            {
                return;
            }
            SelectedIndex = (SelectedIndex + step + count) % count;
            if (_labels[SelectedIndex].Parent is View cell)
            {
                Pressable.Focus(cell);
            }
        });
        var res = Application.Current!.Resources;
        var track = new Border
        {
            StrokeThickness = 0,
            StrokeShape = new RoundRectangle { CornerRadius = 9 },
            Content = _grid,
            HeightRequest = 34,
        };
        track.SetAppThemeColor(BackgroundColorProperty, (Color)res["FillLight"], (Color)res["FillDark"]);
        _thumb = new Border
        {
            StrokeThickness = 0,
            StrokeShape = new RoundRectangle { CornerRadius = 7 },
            HorizontalOptions = LayoutOptions.Start,
            Shadow = new Shadow { Radius = 4, Opacity = 0.12f, Offset = new Point(0, 1) },
        };
        _thumb.SetAppThemeColor(BackgroundColorProperty, (Color)res["SurfaceLight"], (Color)res["SurfaceRaisedDark"]);
        Content = track;
        _grid.SizeChanged += (_, _) => MoveThumb(animated: false);
    }

    public IList<string>? Items
    {
        get => (IList<string>?)GetValue(ItemsProperty);
        set => SetValue(ItemsProperty, value);
    }

    public int SelectedIndex
    {
        get => (int)GetValue(SelectedIndexProperty);
        set => SetValue(SelectedIndexProperty, value);
    }

    private void Build()
    {
        _grid.Children.Clear();
        _grid.ColumnDefinitions.Clear();
        _labels.Clear();
        var items = Items ?? [];
        _grid.Children.Add(_thumb);
        Grid.SetColumnSpan(_thumb, Math.Max(1, items.Count));
        for (var i = 0; i < items.Count; i++)
        {
            _grid.ColumnDefinitions.Add(new ColumnDefinition(GridLength.Star));
            var label = new Label
            {
                Text = items[i],
                FontSize = 13,
                FontAttributes = FontAttributes.Bold,
                HorizontalTextAlignment = TextAlignment.Center,
                VerticalTextAlignment = TextAlignment.Center,
                InputTransparent = false,
            };
            var index = i;
            var cell = new Grid { BackgroundColor = Colors.Transparent, Children = { label } };
            SemanticProperties.SetDescription(cell, items[i]);
            Pressable.SetRole(cell, PressRole.TabItem);
            Pressable.SetCommand(cell, new Command(() => SelectedIndex = index));
            Grid.SetColumn(cell, i);
            _grid.Children.Add(cell);
            _labels.Add(label);
        }
        MoveThumb(animated: false);
    }

    private void MoveThumb(bool animated)
    {
        var count = Items?.Count ?? 0;
        if (count == 0 || _grid.Width <= 0)
        {
            return;
        }
        var segment = (_grid.Width - _grid.Padding.HorizontalThickness) / count;
        _thumb.WidthRequest = segment;
        var x = segment * Math.Clamp(SelectedIndex, 0, count - 1);
        if (animated && !MotionSettings.ReduceMotion)
        {
            Spr.To(_thumb, Prop.TranslationX, x, SpringSpec.Move);
        }
        else
        {
            Spr.Set(_thumb, Prop.TranslationX, x);
        }
        for (var i = 0; i < _labels.Count; i++)
        {
            _labels[i].Opacity = i == SelectedIndex ? 1 : 0.75;
            if (_labels[i].Parent is View cellView)
            {
                Pressable.SetSelected(cellView, i == SelectedIndex);
            }
        }
    }
}
