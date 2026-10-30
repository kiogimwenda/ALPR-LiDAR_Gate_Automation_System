// Motion.cs — springs on views, press feedback, Reduce Motion.
//
// The design principles this file implements:
//
//   01 Respond on press      Pressable scales to 0.97 on touch-DOWN,
//                            within ~100 ms, and springs back on release.
//   03 Interruptible         Every animated property is one long-lived
//                            Spring; a new target retargets it in place,
//                            keeping position and velocity. Reversing a
//                            motion halfway turns it around smoothly.
//   04 Springs, not curves   No durations or easing curves anywhere.
//   10 Gesture details       A press that moves ~10 pt becomes a scroll,
//                            not a tap: the highlight cancels.
//   11 Smooth frames         Only transform (scale/translation/rotation)
//                            and opacity are ever animated.
//   14 Reduced motion        With the OS setting on, movement becomes a
//                            quick cross-fade and press feedback becomes
//                            a brief dim.

using System.Diagnostics;
using System.Runtime.CompilerServices;
using GateControl.Core.Motion;

namespace GateControl.Motion;

public enum Prop
{
    Scale,
    Opacity,
    TranslationX,
    TranslationY,
    Rotation,
    ScaleX,
}

public static class MotionSettings
{
    private static bool? _reduce;

    // The OS "Reduce Motion" / "Remove animations" setting, re-read on resume.
#if DEBUG || GC_HARNESS
    public static bool ReduceMotion => Override ?? (_reduce ??= Detect());

    // Verification only (Platform/Automation.cs): force the setting. Not
    // compiled into Release builds.
    public static bool? Override { get; set; }
#else
    public static bool ReduceMotion => _reduce ??= Detect();
#endif

    public static void Refresh() => _reduce = Detect();

    private static bool Detect()
    {
#if IOS || MACCATALYST
        return UIKit.UIAccessibility.IsReduceMotionEnabled;
#elif ANDROID
        var resolver = Android.App.Application.Context.ContentResolver;
        return resolver is not null &&
               Android.Provider.Settings.Global.GetFloat(resolver,
                   Android.Provider.Settings.Global.AnimatorDurationScale, 1f) == 0f;
#elif WINDOWS
        return !new Windows.UI.ViewManagement.UISettings().AnimationsEnabled;
#else
        return false;
#endif
    }
}

public static class Spr
{
    private sealed class Track
    {
        public required Spring Spring { get; init; }
        public required Prop Prop { get; init; }
        public Action? OnSettled { get; set; }
        // Clock time before which the spring holds still (stagger).
        public double StartAt { get; set; }
    }

    private sealed class Driven
    {
        public readonly Dictionary<Prop, Track> Tracks = [];
    }

    private static readonly ConditionalWeakTable<VisualElement, Driven> State = new();
    private static readonly List<WeakReference<VisualElement>> Active = [];
    private static readonly Stopwatch Clock = Stopwatch.StartNew();
    private static FrameDriver? _timer;
    private static double _lastTick;

    private static double Read(VisualElement v, Prop p) => p switch
    {
        Prop.Scale => v.Scale,
        Prop.Opacity => v.Opacity,
        Prop.TranslationX => v.TranslationX,
        Prop.TranslationY => v.TranslationY,
        Prop.ScaleX => v.ScaleX,
        _ => v.Rotation,
    };

    private static void Write(VisualElement v, Prop p, double value)
    {
#if DEBUG || GC_HARNESS
        var t0 = FrameTrace.Now();
        WriteCore(v, p, value);
        if (FrameTrace.On && (FrameTrace.Now() - t0) * 1000.0 / System.Diagnostics.Stopwatch.Frequency > 1)
        {
            FrameTrace.Span($"write.{p}.{v.GetType().Name}.{(v as Element)?.AutomationId ?? ""}", t0);
        }
#else
        WriteCore(v, p, value);
#endif
    }

    private static void WriteCore(VisualElement v, Prop p, double value)
    {
        switch (p)
        {
            case Prop.Scale: v.Scale = value; break;
            case Prop.Opacity: v.Opacity = Math.Clamp(value, 0, 1); break;
            case Prop.TranslationX: v.TranslationX = value; break;
            case Prop.TranslationY: v.TranslationY = value; break;
            case Prop.ScaleX: v.ScaleX = value; break;
            default: v.Rotation = value; break;
        }
    }

    // Animate `prop` of `view` to `target`. If that property is already in
    // flight, the running spring is retargeted (interruptible). `velocity`
    // hands a gesture's speed (units per second) into the motion.
    // `delay` (seconds) holds the motion still before it starts (stagger);
    // a retarget during the delay keeps the original start time.
    public static void To(VisualElement view, Prop prop, double target, SpringSpec? spec = null,
                          double? velocity = null, Action? settled = null, double delay = 0)
    {
        var driven = State.GetOrCreateValue(view);
        if (!driven.Tracks.TryGetValue(prop, out var track))
        {
            track = new Track { Spring = new Spring(Read(view, prop), spec ?? SpringSpec.Default), Prop = prop };
            driven.Tracks[prop] = track;
            track.StartAt = Now + Math.Max(0, delay);
        }
        else if (delay > 0)
        {
            track.StartAt = Math.Max(track.StartAt, Now + delay);
        }
        track.OnSettled = settled;
        track.Spring.Retarget(target, spec ?? track.Spring.Spec, velocity);
        if (track.Spring.IsSettled && track.StartAt <= Now)
        {
            Write(view, prop, target);
            settled?.Invoke();
            driven.Tracks.Remove(prop);
            return;
        }
        Enlist(view);
    }

    // Jump immediately (layout, Reduce Motion), cancelling any motion.
    public static void Set(VisualElement view, Prop prop, double value)
    {
        if (State.TryGetValue(view, out var driven))
        {
            driven.Tracks.Remove(prop);
        }
        Write(view, prop, value);
    }

    // Seconds on the motion clock (shared by springs and frame callbacks).
    public static double Now => Clock.Elapsed.TotalSeconds;

    // A per-frame callback on the motion ticker, for motion that isn't a
    // spring on a view property (count-ups, blur, clips, paired springs).
    // `step` gets dt in seconds and returns false when it is finished.
    public static void OnFrame(Func<double, bool> step, IDispatcher? dispatcher = null)
    {
        Frames.Add(step);
        EnsureTimer(dispatcher);
    }

    public static bool IsMoving(VisualElement view, Prop prop) =>
        State.TryGetValue(view, out var d) && d.Tracks.ContainsKey(prop);

    private static readonly List<Func<double, bool>> Frames = [];

    private static void Enlist(VisualElement view)
    {
        if (!Active.Any(w => w.TryGetTarget(out var v) && ReferenceEquals(v, view)))
        {
            Active.Add(new WeakReference<VisualElement>(view));
        }
        EnsureTimer(view.Dispatcher);
    }

    private static void EnsureTimer(IDispatcher? preferred)
    {
        if (_timer is null)
        {
            var dispatcher = preferred ?? Application.Current?.Dispatcher;
            if (dispatcher is null)
            {
                return;
            }
            _timer = new FrameDriver(dispatcher, Tick);
            _lastTick = Clock.Elapsed.TotalSeconds;
        }
        if (!_timer.IsRunning)
        {
            _lastTick = Clock.Elapsed.TotalSeconds;
            _timer.Start();
        }
    }

    // Ticks once per displayed frame: WinUI CompositionTarget.Rendering,
    // Android Choreographer. (A DispatcherTimer only ticks every ~15.6 ms on
    // Windows, so on a 120–240 Hz display motion advanced in 16 ms steps and
    // looked steppy; review "page-load smoothness".) Elsewhere: a timer.
    private sealed class FrameDriver(IDispatcher dispatcher, Action tick)
    {
#if !WINDOWS && !ANDROID
        private IDispatcherTimer? _fallback;
#endif

        public bool IsRunning { get; private set; }

        public void Start()
        {
            if (dispatcher.IsDispatchRequired)
            {
                dispatcher.Dispatch(Start);
                return;
            }
            if (IsRunning)
            {
                return;
            }
            IsRunning = true;
#if WINDOWS
            Microsoft.UI.Xaml.Media.CompositionTarget.Rendering += OnRendering;
#elif ANDROID
            // One callback chain only: after a quick Stop/Start the previous
            // callback is still pending and carries on; posting another
            // would tick twice per frame.
            if (!_posted)
            {
                _posted = true;
                Android.Views.Choreographer.Instance!.PostFrameCallback(Callback);
            }
#else
            _fallback ??= CreateFallback();
            _fallback.Start();
#endif
        }

        public void Stop()
        {
            if (!IsRunning)
            {
                return;
            }
            IsRunning = false;
#if WINDOWS
            Microsoft.UI.Xaml.Media.CompositionTarget.Rendering -= OnRendering;
#elif !ANDROID
            _fallback?.Stop();
#endif
        }

#if !WINDOWS && !ANDROID
        private IDispatcherTimer CreateFallback()
        {
            var timer = dispatcher.CreateTimer();
            timer.Interval = TimeSpan.FromMilliseconds(1000.0 / 120);
            timer.Tick += (_, _) => tick();
            return timer;
        }
#endif

#if WINDOWS
        private void OnRendering(object? sender, object e) => tick();
#elif ANDROID
        private FrameCallback? _callback;
        private bool _posted;

        private FrameCallback Callback => _callback ??= new FrameCallback(() =>
        {
            _posted = false;
            if (!IsRunning)
            {
                return;
            }
            tick();
            if (IsRunning && !_posted)
            {
                _posted = true;
                Android.Views.Choreographer.Instance!.PostFrameCallback(Callback);
            }
        });

        private sealed class FrameCallback(Action onFrame) : Java.Lang.Object, Android.Views.Choreographer.IFrameCallback
        {
            public void DoFrame(long frameTimeNanos) => onFrame();
        }
#endif
    }

#if DEBUG || GC_HARNESS
    // Verification: how long each motion tick takes and how much it moved.
    internal static Action<double, int, int>? TickTrace;
    internal static double SlowestCallbackMs;
    internal static string SlowestCallback = "";
#endif

    private static void Tick()
    {
#if DEBUG || GC_HARNESS
        SlowestCallbackMs = 0;
        SlowestCallback = "";
        var tickStart = Clock.Elapsed.TotalMilliseconds;
        var tickTicks = FrameTrace.Now();
        var springCount = Active.Count;
        var frameCount = Frames.Count;
#endif
        TickCore();
#if DEBUG || GC_HARNESS
        TickTrace?.Invoke(Clock.Elapsed.TotalMilliseconds - tickStart, springCount, frameCount);
        FrameTrace.Span("motion.tick", tickTicks);
#endif
    }

    private static void TickCore()
    {
        var now = Clock.Elapsed.TotalSeconds;
        // Real elapsed time (capped): the closed-form spring is exact for
        // any step, so a slow frame lands where it should, not late.
        var dt = Math.Min(now - _lastTick, 0.1);
        _lastTick = now;
        for (var i = Active.Count - 1; i >= 0; i--)
        {
            if (!Active[i].TryGetTarget(out var view) || !State.TryGetValue(view, out var driven))
            {
                Active.RemoveAt(i);
                continue;
            }
            foreach (var track in driven.Tracks.Values.ToList())
            {
                if (track.StartAt > now)
                {
                    continue;  // staggered: not its turn yet
                }
                // First frame after the delay: step only the time since it began.
                track.Spring.Step(Math.Min(dt, now - track.StartAt));
                Write(view, track.Prop, track.Spring.Value);
                if (track.Spring.IsSettled)
                {
                    driven.Tracks.Remove(track.Prop);
                    track.OnSettled?.Invoke();
                }
            }
            if (driven.Tracks.Count == 0)
            {
                Active.RemoveAt(i);
            }
        }
        for (var i = Frames.Count - 1; i >= 0; i--)
        {
            bool more;
            try
            {
#if DEBUG || GC_HARNESS
                var cbStart = Clock.Elapsed.TotalMilliseconds;
                more = Frames[i](dt);
                var cbMs = Clock.Elapsed.TotalMilliseconds - cbStart;
                if (cbMs > SlowestCallbackMs)
                {
                    SlowestCallbackMs = cbMs;
                    SlowestCallback = Frames[i].Method.DeclaringType?.FullName + "." + Frames[i].Method.Name;
                }
#else
                more = Frames[i](dt);
#endif
            }
            catch (Exception)
            {
                more = false;  // a broken callback must not stop all motion
            }
            if (!more)
            {
                Frames.RemoveAt(i);
            }
        }
        if (Active.Count == 0 && Frames.Count == 0)
        {
            _timer?.Stop();
        }
    }

    // ---- composites ------------------------------------------------------------

    // New content arriving: a short drop-in, or a quick fade under Reduce Motion.
    public static void Enter(VisualElement view, double fromY = -14)
    {
        if (MotionSettings.ReduceMotion)
        {
            Set(view, Prop.Opacity, 0);
            To(view, Prop.Opacity, 1, SpringSpec.Press);
            return;
        }
        Set(view, Prop.Opacity, 0);
        Set(view, Prop.TranslationY, fromY);
        To(view, Prop.Opacity, 1, SpringSpec.Default);
        To(view, Prop.TranslationY, 0, SpringSpec.Default);
    }
}

// Press feedback for any view: scale 0.97 on touch-down, back on release,
// with the action (Command) on release — and a ~10 pt move turns the press
// into a scroll instead of a tap.
public enum PressRole { Button, TabItem }

public static class Pressable
{
    public static readonly BindableProperty CommandProperty = BindableProperty.CreateAttached(
        "Command", typeof(System.Windows.Input.ICommand), typeof(Pressable), null,
        propertyChanged: (b, _, _) => Attach(b as View));

    public static readonly BindableProperty CommandParameterProperty = BindableProperty.CreateAttached(
        "CommandParameter", typeof(object), typeof(Pressable), null);

    public static readonly BindableProperty EnabledProperty = BindableProperty.CreateAttached(
        "Enabled", typeof(bool), typeof(Pressable), false,
        propertyChanged: (b, _, n) => { if ((bool)n) Attach(b as View); });

    // Keyboard and screen readers (review round 3, item 4): what the element
    // is (Button, or TabItem for the nav and segmented cells) and, for tabs,
    // whether it is the selected one.
    public static readonly BindableProperty RoleProperty = BindableProperty.CreateAttached(
        "Role", typeof(PressRole), typeof(Pressable), PressRole.Button,
        propertyChanged: (b, _, _) => Keyboard.Sync(b as View));

    public static readonly BindableProperty SelectedProperty = BindableProperty.CreateAttached(
        "Selected", typeof(bool), typeof(Pressable), false,
        propertyChanged: (b, _, _) => Keyboard.Sync(b as View));

    public static PressRole GetRole(BindableObject b) => (PressRole)b.GetValue(RoleProperty);
    public static void SetRole(BindableObject b, PressRole v) => b.SetValue(RoleProperty, v);
    public static bool GetSelected(BindableObject b) => (bool)b.GetValue(SelectedProperty);
    public static void SetSelected(BindableObject b, bool v) => b.SetValue(SelectedProperty, v);

    // Arrow keys (Left / Right) inside a group of tabs: the group gets the key
    // and moves the selection. Set by NavCapsule / SegmentedControl.
    public static readonly BindableProperty ArrowsProperty = BindableProperty.CreateAttached(
        "Arrows", typeof(Action<int>), typeof(Pressable), null);

    public static Action<int>? GetArrows(BindableObject b) => (Action<int>?)b.GetValue(ArrowsProperty);
    public static void SetArrows(BindableObject b, Action<int>? v) => b.SetValue(ArrowsProperty, v);

    // Moves keyboard focus to a pressable (arrow-key navigation).
    public static void Focus(View view)
    {
#if WINDOWS
        ((view.Handler?.ContainerView ?? view.Handler?.PlatformView) as Microsoft.UI.Xaml.UIElement)?
            .Focus(Microsoft.UI.Xaml.FocusState.Keyboard);
#endif
    }

    public static System.Windows.Input.ICommand? GetCommand(BindableObject b) =>
        (System.Windows.Input.ICommand?)b.GetValue(CommandProperty);
    public static void SetCommand(BindableObject b, System.Windows.Input.ICommand? v) => b.SetValue(CommandProperty, v);
    public static object? GetCommandParameter(BindableObject b) => b.GetValue(CommandParameterProperty);
    public static void SetCommandParameter(BindableObject b, object? v) => b.SetValue(CommandParameterProperty, v);
    public static bool GetEnabled(BindableObject b) => (bool)b.GetValue(EnabledProperty);
    public static void SetEnabled(BindableObject b, bool v) => b.SetValue(EnabledProperty, v);

    private static readonly ConditionalWeakTable<View, object> Attached = new();

    private static void Attach(View? view)
    {
        if (view is null || Attached.TryGetValue(view, out _))
        {
            return;
        }
        Attached.Add(view, new object());
        Point? down = null;
        var cancelled = false;

        void Press()
        {
            if (MotionSettings.ReduceMotion)
            {
                Spr.To(view, Prop.Opacity, 0.6, SpringSpec.Press);
            }
            else
            {
                Spr.To(view, Prop.Scale, 0.97, SpringSpec.Press);
            }
        }

        void Release()
        {
            Spr.To(view, Prop.Opacity, 1, SpringSpec.Default);
            Spr.To(view, Prop.Scale, 1, SpringSpec.Default);
        }

        var pointer = new PointerGestureRecognizer();
        pointer.PointerPressed += (_, e) =>
        {
            down = e.GetPosition(view);
            cancelled = false;
            Press();
        };
        pointer.PointerMoved += (_, e) =>
        {
            if (down is { } d && !cancelled && e.GetPosition(view) is { } p &&
                Math.Abs(p.X - d.X) + Math.Abs(p.Y - d.Y) > Gesture.DragThreshold)
            {
                cancelled = true;  // it's a scroll, not a tap
                Release();
            }
        };
        pointer.PointerReleased += (_, _) =>
        {
            down = null;
            Release();
        };
        pointer.PointerExited += (_, _) =>
        {
            down = null;
            Release();
        };
        view.GestureRecognizers.Add(pointer);

        void Invoke()
        {
            var command = GetCommand(view);
            var parameter = GetCommandParameter(view);
            if (command?.CanExecute(parameter) == true)
            {
                command.Execute(parameter);
            }
        }

        var tap = new TapGestureRecognizer();
        tap.Tapped += (_, _) =>
        {
            Release();
            if (cancelled)
            {
                return;
            }
            Invoke();
        };
        view.GestureRecognizers.Add(tap);
        Keyboard.Attach(view, Press, Release, Invoke);
    }

    // Windows: a pressable is a keyboard stop like a button — Tab reaches it
    // (in visual order), Enter / Space press and invoke it, the system focus
    // visual shows on keyboard focus only, and UI Automation sees a named
    // Button (or a TabItem with its selected state). Elsewhere a no-op
    // (Android / iOS keyboard access not built here).
    private static class Keyboard
    {
        public static void Attach(View view, Action press, Action release, Action invoke)
        {
#if WINDOWS
            void Hook()
            {
                if ((view.Handler?.ContainerView ?? view.Handler?.PlatformView) is not Microsoft.UI.Xaml.UIElement el ||
                    el is Microsoft.UI.Xaml.Controls.Control)
                {
                    return;
                }
                if (Hooked.TryGetValue(el, out _))
                {
                    return;
                }
                Hooked.Add(el, new object());
                el.IsTabStop = true;
                el.UseSystemFocusVisuals = true;
                var down = false;
                el.KeyDown += (_, e) =>
                {
                    if (e.Key is global::Windows.System.VirtualKey.Enter or global::Windows.System.VirtualKey.Space)
                    {
                        e.Handled = true;
                        if (!down)
                        {
                            down = true;
                            press();
                        }
                    }
                    else if (e.Key is global::Windows.System.VirtualKey.Left or global::Windows.System.VirtualKey.Right &&
                             FindArrows(view) is { } arrows)
                    {
                        e.Handled = true;
                        arrows(e.Key == global::Windows.System.VirtualKey.Left ? -1 : 1);
                    }
                };
                el.KeyUp += (_, e) =>
                {
                    if (down && e.Key is global::Windows.System.VirtualKey.Enter or global::Windows.System.VirtualKey.Space)
                    {
                        e.Handled = true;
                        down = false;
                        release();
                        invoke();
                    }
                };
                el.LostFocus += (_, _) =>
                {
                    if (down)
                    {
                        down = false;
                        release();
                    }
                };
                Sync(view);
            }
            view.HandlerChanged += (_, _) => Hook();
            Hook();
#endif
        }

        public static void Sync(View? view)
        {
#if WINDOWS
            if ((view?.Handler?.ContainerView ?? view?.Handler?.PlatformView) is not Microsoft.UI.Xaml.UIElement el ||
                el is Microsoft.UI.Xaml.Controls.Control)
            {
                return;
            }
            var tab = GetRole(view!) == PressRole.TabItem;
            Microsoft.UI.Xaml.Automation.AutomationProperties.SetAutomationControlType(el,
                tab ? Microsoft.UI.Xaml.Automation.Peers.AutomationControlType.TabItem
                    : Microsoft.UI.Xaml.Automation.Peers.AutomationControlType.Button);
            Microsoft.UI.Xaml.Automation.AutomationProperties.SetItemStatus(el,
                tab && GetSelected(view!) ? "Selected" : "");
#endif
        }

        private static Action<int>? FindArrows(Element? e)
        {
            for (; e is not null; e = e.Parent)
            {
                if (GetArrows(e) is { } a)
                {
                    return a;
                }
            }
            return null;
        }

#if WINDOWS
        private static readonly ConditionalWeakTable<Microsoft.UI.Xaml.UIElement, object> Hooked = new();
#endif
    }

    // Buttons have native press/release events; use those. Called for
    // every Button by the handler mapper in MauiProgram (once per button).
    public static void Wire(Button button)
    {
        if (Attached.TryGetValue(button, out _))
        {
            return;
        }
        Attached.Add(button, new object());
        button.Pressed += (_, _) =>
        {
            if (MotionSettings.ReduceMotion)
            {
                Spr.To(button, Prop.Opacity, 0.6, SpringSpec.Press);
            }
            else
            {
                Spr.To(button, Prop.Scale, 0.97, SpringSpec.Press);
            }
        };
        button.Released += (_, _) =>
        {
            Spr.To(button, Prop.Opacity, 1, SpringSpec.Default);
            Spr.To(button, Prop.Scale, 1, SpringSpec.Default);
        };
    }
}
