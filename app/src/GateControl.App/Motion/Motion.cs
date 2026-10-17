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
}

public static class MotionSettings
{
    private static bool? _reduce;

    // The OS "Reduce Motion" / "Remove animations" setting, re-read on resume.
    public static bool ReduceMotion => _reduce ??= Detect();

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
    }

    private sealed class Driven
    {
        public readonly Dictionary<Prop, Track> Tracks = [];
    }

    private static readonly ConditionalWeakTable<VisualElement, Driven> State = new();
    private static readonly List<WeakReference<VisualElement>> Active = [];
    private static readonly Stopwatch Clock = Stopwatch.StartNew();
    private static IDispatcherTimer? _timer;
    private static double _lastTick;

    private static double Read(VisualElement v, Prop p) => p switch
    {
        Prop.Scale => v.Scale,
        Prop.Opacity => v.Opacity,
        Prop.TranslationX => v.TranslationX,
        Prop.TranslationY => v.TranslationY,
        _ => v.Rotation,
    };

    private static void Write(VisualElement v, Prop p, double value)
    {
        switch (p)
        {
            case Prop.Scale: v.Scale = value; break;
            case Prop.Opacity: v.Opacity = Math.Clamp(value, 0, 1); break;
            case Prop.TranslationX: v.TranslationX = value; break;
            case Prop.TranslationY: v.TranslationY = value; break;
            default: v.Rotation = value; break;
        }
    }

    // Animate `prop` of `view` to `target`. If that property is already in
    // flight, the running spring is retargeted (interruptible). `velocity`
    // hands a gesture's speed (units per second) into the motion.
    public static void To(VisualElement view, Prop prop, double target, SpringSpec? spec = null,
                          double? velocity = null, Action? settled = null)
    {
        var driven = State.GetOrCreateValue(view);
        if (!driven.Tracks.TryGetValue(prop, out var track))
        {
            track = new Track { Spring = new Spring(Read(view, prop), spec ?? SpringSpec.Default), Prop = prop };
            driven.Tracks[prop] = track;
        }
        track.OnSettled = settled;
        track.Spring.Retarget(target, spec ?? track.Spring.Spec, velocity);
        if (track.Spring.IsSettled)
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

    public static bool IsMoving(VisualElement view, Prop prop) =>
        State.TryGetValue(view, out var d) && d.Tracks.ContainsKey(prop);

    private static void Enlist(VisualElement view)
    {
        if (!Active.Any(w => w.TryGetTarget(out var v) && ReferenceEquals(v, view)))
        {
            Active.Add(new WeakReference<VisualElement>(view));
        }
        if (_timer is null)
        {
            var dispatcher = view.Dispatcher ?? Application.Current?.Dispatcher;
            if (dispatcher is null)
            {
                return;
            }
            _timer = dispatcher.CreateTimer();
            _timer.Interval = TimeSpan.FromMilliseconds(1000.0 / 120);
            _timer.Tick += (_, _) => Tick();
            _lastTick = Clock.Elapsed.TotalSeconds;
        }
        if (!_timer.IsRunning)
        {
            _lastTick = Clock.Elapsed.TotalSeconds;
            _timer.Start();
        }
    }

    private static void Tick()
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
                track.Spring.Step(dt);
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
        if (Active.Count == 0)
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

        var tap = new TapGestureRecognizer();
        tap.Tapped += (_, _) =>
        {
            Release();
            if (cancelled)
            {
                return;
            }
            var command = GetCommand(view);
            var parameter = GetCommandParameter(view);
            if (command?.CanExecute(parameter) == true)
            {
                command.Execute(parameter);
            }
        };
        view.GestureRecognizers.Add(tap);
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
