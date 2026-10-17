// Sheet.cs — an iOS-style sheet: drag to dismiss, fully interruptible.
//
//   02 Direct manipulation  the sheet follows the finger 1:1 from where it
//                           was grabbed
//   05 Velocity handoff     release speed carries into the settle spring
//   06 Momentum             the release is judged by where the throw would
//                           land (Gesture.Project), not where the finger is
//   07 Spatial consistency  it leaves the way it came — down
//   09 Rubber-banding       dragged up past fully open, it resists (0.55)
//   03 Interruptible        open/close can reverse mid-flight
//   12 Materials            one translucent scrim behind one solid sheet
//   14 Reduced motion       slides become a quick cross-fade
//
// On wide windows (tablets, desktop) the sheet is a centred card with the
// same motion, as on iPadOS.

using System.Windows.Input;
using GateControl.Core.Motion;
using GateControl.Motion;
using Microsoft.Maui.Controls.Shapes;

namespace GateControl.Controls;

public sealed class Sheet : ContentView
{
    public static readonly BindableProperty IsOpenProperty = BindableProperty.Create(
        nameof(IsOpen), typeof(bool), typeof(Sheet), false, BindingMode.TwoWay,
        propertyChanged: (b, _, n) => ((Sheet)b).Animate((bool)n, null));

    public static readonly BindableProperty SheetContentProperty = BindableProperty.Create(
        nameof(SheetContent), typeof(View), typeof(Sheet), null,
        propertyChanged: (b, _, n) => ((Sheet)b)._presenter.Content = n as View);

    public static readonly BindableProperty DismissCommandProperty = BindableProperty.Create(
        nameof(DismissCommand), typeof(ICommand), typeof(Sheet));

    private const double WideBreakpoint = 720;

    private readonly BoxView _scrim;
    private readonly Border _panel;
    private readonly ContentView _presenter;
    private readonly Grid _root;
    private readonly List<(double t, double y)> _samples = [];
    private double _dragStart;
    private bool _dragging;

    public Sheet()
    {
        IsVisible = false;
        InputTransparent = true;

        _scrim = new BoxView { Opacity = 0 };
        _scrim.SetAppThemeColor(BoxView.ColorProperty,
            (Color)Application.Current!.Resources["ScrimLight"], (Color)Application.Current.Resources["ScrimDark"]);
        var scrimTap = new TapGestureRecognizer();
        scrimTap.Tapped += (_, _) => Dismiss(0);
        _scrim.GestureRecognizers.Add(scrimTap);

        var handle = new Border
        {
            WidthRequest = 36,
            HeightRequest = 5,
            StrokeThickness = 0,
            StrokeShape = new RoundRectangle { CornerRadius = 2.5 },
            HorizontalOptions = LayoutOptions.Center,
            Margin = new Thickness(0, 8, 0, 6),
        };
        handle.SetAppThemeColor(BackgroundColorProperty,
            (Color)Application.Current.Resources["TertiaryLabelLight"],
            (Color)Application.Current.Resources["TertiaryLabelDark"]);
        // A generous grab area around the visible handle.
        var grab = new Grid { HeightRequest = 28, BackgroundColor = Colors.Transparent, Children = { handle } };
        var pan = new PanGestureRecognizer();
        pan.PanUpdated += OnPan;
        grab.GestureRecognizers.Add(pan);

        _presenter = new ContentView();
        _panel = new Border
        {
            StrokeThickness = 0,
            Padding = new Thickness(0, 0, 0, 12),
            VerticalOptions = LayoutOptions.End,
            Content = new Grid
            {
                RowDefinitions = [new RowDefinition(GridLength.Auto), new RowDefinition(GridLength.Star)],
                Children = { grab, _presenter },
            },
        };
        Grid.SetRow(_presenter, 1);
        _panel.SetAppThemeColor(BackgroundColorProperty,
            (Color)Application.Current.Resources["BackgroundLight"],
            (Color)Application.Current.Resources["SurfaceDark"]);

        _root = new Grid { Children = { _scrim, _panel } };
        Content = _root;
        SizeChanged += (_, _) => LayoutPanel();
    }

    public bool IsOpen
    {
        get => (bool)GetValue(IsOpenProperty);
        set => SetValue(IsOpenProperty, value);
    }

    public View? SheetContent
    {
        get => (View?)GetValue(SheetContentProperty);
        set => SetValue(SheetContentProperty, value);
    }

    // Invoked when the user dismisses (drag, flick, scrim tap).
    public ICommand? DismissCommand
    {
        get => (ICommand?)GetValue(DismissCommandProperty);
        set => SetValue(DismissCommandProperty, value);
    }

    private bool IsWide => Width >= WideBreakpoint;
    private double ClosedOffset => Math.Max(_panel.Height, Height) + 40;

    private void LayoutPanel()
    {
        if (IsWide)
        {
            _panel.WidthRequest = 560;
            _panel.MaximumHeightRequest = Math.Max(320, Height - 120);
            _panel.HorizontalOptions = LayoutOptions.Center;
            _panel.VerticalOptions = LayoutOptions.Center;
            _panel.StrokeShape = new RoundRectangle { CornerRadius = 22 };
        }
        else
        {
            _panel.WidthRequest = -1;
            _panel.MaximumHeightRequest = Math.Max(320, Height - 56);
            _panel.HorizontalOptions = LayoutOptions.Fill;
            _panel.VerticalOptions = LayoutOptions.End;
            _panel.StrokeShape = new RoundRectangle { CornerRadius = new CornerRadius(22, 22, 0, 0) };
        }
    }

    private void Animate(bool open, double? velocity)
    {
        if (open)
        {
            IsVisible = true;
            InputTransparent = false;
            if (!Spr.IsMoving(_panel, Prop.TranslationY) && _panel.TranslationY == 0 && _scrim.Opacity == 0)
            {
                // Coming in from rest: start below the window.
                Spr.Set(_panel, Prop.TranslationY, MotionSettings.ReduceMotion ? 0 : ClosedOffset);
                if (MotionSettings.ReduceMotion)
                {
                    Spr.Set(_panel, Prop.Opacity, 0);
                }
            }
            Spr.To(_scrim, Prop.Opacity, 1, SpringSpec.Default);
            if (MotionSettings.ReduceMotion)
            {
                Spr.To(_panel, Prop.Opacity, 1, SpringSpec.Press);
            }
            else
            {
                Spr.To(_panel, Prop.TranslationY, 0, SpringSpec.Sheet, velocity);
            }
            return;
        }
        InputTransparent = true;
        Spr.To(_scrim, Prop.Opacity, 0, SpringSpec.Default);
        if (MotionSettings.ReduceMotion)
        {
            Spr.To(_panel, Prop.Opacity, 0, SpringSpec.Press, settled: Hide);
        }
        else
        {
            // Leaves the way it came. A flick keeps its speed (0.8 damping).
            Spr.To(_panel, Prop.TranslationY, ClosedOffset,
                   velocity is null ? SpringSpec.Sheet : SpringSpec.Flick, velocity, settled: Hide);
        }
    }

    private void Hide()
    {
        if (!IsOpen)
        {
            IsVisible = false;
            Spr.Set(_panel, Prop.TranslationY, 0);
            Spr.Set(_panel, Prop.Opacity, 1);
        }
    }

    private void Dismiss(double velocity)
    {
        if (DismissCommand?.CanExecute(null) == true)
        {
            DismissCommand.Execute(null);
        }
        // The command normally clears the bound state (IsOpen → false); if
        // nothing is bound, close directly.
        if (IsOpen)
        {
            SetValue(IsOpenProperty, false);
        }
        Animate(false, velocity);
    }

    private void OnPan(object? sender, PanUpdatedEventArgs e)
    {
        var now = Environment.TickCount64 / 1000.0;
        switch (e.StatusType)
        {
            case GestureStatus.Started:
                _dragging = true;
                // Grab wherever the sheet is right now — even mid-animation.
                _dragStart = _panel.TranslationY;
                _samples.Clear();
                break;
            case GestureStatus.Running when _dragging:
                var y = _dragStart + e.TotalY;
                // Past fully open: resist (rubber band), never jump.
                var shown = y < 0 ? Gesture.RubberBand(y, Math.Max(1, _panel.Height)) : y;
                Spr.Set(_panel, Prop.TranslationY, shown);
                _samples.Add((now, y));
                _samples.RemoveAll(s => now - s.t > 0.1);
                break;
            case GestureStatus.Completed or GestureStatus.Canceled when _dragging:
                _dragging = false;
                var velocity = 0.0;
                if (_samples.Count >= 2)
                {
                    var (t0, y0) = _samples[0];
                    var (t1, y1) = _samples[^1];
                    velocity = t1 > t0 ? (y1 - y0) / (t1 - t0) : 0;
                }
                var landing = Gesture.Project(_panel.TranslationY, velocity);
                if (landing > _panel.Height * 0.5)
                {
                    Dismiss(velocity);
                }
                else
                {
                    Spr.To(_panel, Prop.TranslationY, 0, SpringSpec.Sheet, velocity);
                }
                break;
        }
    }
}
