// ThemeReveal.cs — light ↔ dark as a wavy disc spreading from the button
// (spec A11): snapshot the old theme, lay it over the window, switch the
// theme underneath, and cut a growing hole in the snapshot (window minus
// wavy disc) along ease-in-quad over 0.62 s; then drop the snapshot. The
// new theme is live inside the disc as it grows, exactly as in the
// reference.
//
//   * Fast start (target: first wipe frame ≤ 100 ms after the tap): on
//     pointer-DOWN (Prepare) the snapshot is taken natively — WinUI
//     RenderTargetBitmap shown directly as the native Image source, Android
//     PixelCopy into a Bitmap on the native ImageView; no PNG round-trip —
//     and, once it is on screen, the new theme is applied underneath it, so
//     the slow restyle runs during the press. The tap only starts the wipe.
//     A press not followed by a tap within 1 s is undone (old theme back
//     under the snapshot, snapshot dropped); a later tap starts afresh.
//   * Capture failed: logged (AppLog, also in Release) overlay fallback —
//     a disc of the new canvas colour grows over the window, the theme
//     switches under full cover, and the disc fades out.
//   * Reduce Motion: the snapshot cross-fades out over 0.2 s.
//   * Tapping again mid-wipe finishes the running wipe at once and starts
//     the new one from the current state.

using GateControl.Core.Motion;
using GateControl.Platform;
using Microsoft.Extensions.Logging;
using Microsoft.Maui.Controls.Shapes;

namespace GateControl.Motion;

public static class ThemeReveal
{
    private const double FreshFor = 1.0;  // seconds a pointer-down snapshot stays usable

    private static View? _overlay;
    private static Func<double, bool>? _finish;
    private static Pending? _pending;
    private static ILogger Log => AppLog.For("ThemeReveal");

    // A pointer-down in progress: its snapshot overlay and, once that is on
    // screen, the new theme already applied underneath it.
    private sealed class Pending
    {
        public required object Key { get; init; }
        public required Action Revert { get; init; }
        public double At { get; init; }
        public Task<bool> Ready { get; set; } = Task.FromResult(false);
        public Image? Overlay { get; set; }
        public bool Applied { get; set; }
    }

    // A press or a wipe in progress.
    public static bool Busy => _pending is not null || _overlay is not null;

    // The target of the press in progress, if any.
    public static object? PendingKey => _pending?.Key;

    // Pointer-down on the theme button. The restyle is the slow part (every
    // AppThemeBinding of every tab, then a 60–75 ms layout and render frame),
    // so it starts now, during the press: the snapshot of the old theme goes
    // on screen, and the new theme (`key`, applied by `apply`) is switched
    // underneath it, invisibly. The tap then only has to start the wipe.
    // No tap within FreshFor (a cancelled or held press): `revert` restores
    // the old theme under the snapshot and the snapshot is dropped.
    public static void Prepare(Grid host, VisualElement captureTarget, object key, Action apply, Action revert)
    {
        if (_overlay is not null || _pending is not null)
        {
            return;
        }
        var p = new Pending { Key = key, Revert = revert, At = Spr.Now };
        _pending = p;
        FrameTrace.Mark("theme.prepare");
        p.Ready = PrepareAsync(p, host, captureTarget, apply);
        host.Dispatcher.DispatchDelayed(TimeSpan.FromSeconds(FreshFor), () =>
        {
            if (ReferenceEquals(_pending, p))
            {
                _ = CancelAsync(host, p, "stale");
            }
        });
    }

    private static async Task<bool> PrepareAsync(Pending p, Grid host, VisualElement captureTarget, Action apply)
    {
        object? snapshot = null;
        try
        {
            snapshot = await CaptureNativeAsync(captureTarget);
        }
        catch (Exception e)
        {
            Log.LogWarning(e, "pointer-down snapshot failed");
        }
        if (snapshot is null || !ReferenceEquals(_pending, p))
        {
            return false;
        }
        var image = NewOverlay(host);
        host.Children.Add(image);
        p.Overlay = image;
        if (!await ShowAsync(image, snapshot) || !ReferenceEquals(_pending, p))
        {
            host.Children.Remove(image);
            p.Overlay = null;
            return false;
        }
        FrameTrace.Mark("theme.shown");
        apply();
        p.Applied = true;
        FrameTrace.Mark("theme.applied");
        return true;
    }

    private static async Task CancelAsync(Grid host, Pending p, string why)
    {
        if (ReferenceEquals(_pending, p))
        {
            _pending = null;
        }
        FrameTrace.Mark("theme.cancel." + why);
        await p.Ready;
        if (p.Applied)
        {
            p.Revert();
            p.Applied = false;
        }
        if (p.Overlay is { } overlay)
        {
            // Drop the snapshot once the old theme is drawn again under it.
            await NextFramesAsync(2);
            host.Children.Remove(overlay);
            p.Overlay = null;
        }
    }

    private static Image NewOverlay(Grid host)
    {
        var image = new Image
        {
            Aspect = Aspect.Fill,
            InputTransparent = true,
            WidthRequest = host.Width,
            HeightRequest = host.Height,
            HorizontalOptions = LayoutOptions.Start,
            VerticalOptions = LayoutOptions.Start,
        };
        Grid.SetRowSpan(image, Math.Max(1, host.RowDefinitions.Count));
        Grid.SetColumnSpan(image, Math.Max(1, host.ColumnDefinitions.Count));
        return image;
    }

    // `host` spans the whole window and is the top layer; `origin` is the
    // button centre in host coordinates; `apply` switches the theme.
    // `done` runs once the overlay is gone (finished, or cut short by a new
    // wipe).
    // `started` runs on the wipe's first drawn frame.
    public static async Task RunAsync(Grid host, VisualElement captureTarget, Point origin, object key, Action apply,
                                      Color newCanvas, Action? done = null, Action? started = null)
    {
        FinishRunning(host);
        Image? image = null;
        if (_pending is { } p)
        {
            _pending = null;
            if (p.Key.Equals(key) && Spr.Now - p.At <= FreshFor && await p.Ready && p.Overlay is { } ready)
            {
                // The press already switched the theme under the snapshot.
                FrameTrace.Mark("theme.snapshot.prepared");
                image = ready;
            }
            else
            {
                await CancelAsync(host, p, "unused");
            }
        }
        if (image is null)
        {
            // No usable press (keyboard, screen reader, a held press): the
            // whole sequence now.
            FrameTrace.Mark("theme.snapshot.new");
            object? snapshot = null;
            try
            {
                snapshot = await CaptureNativeAsync(captureTarget);
            }
            catch (Exception e)
            {
                Log.LogWarning(e, "snapshot failed; using the overlay fallback");
            }
            if (snapshot is null)
            {
                Log.LogWarning("no snapshot on this platform/state; using the overlay fallback");
                await FallbackAsync(host, origin, apply, newCanvas, done, started);
                return;
            }
            image = NewOverlay(host);
            host.Children.Add(image);
            _overlay = image;
            if (!await ShowAsync(image, snapshot))
            {
                host.Children.Remove(image);
                _overlay = null;
                Log.LogWarning("snapshot could not be shown; using the overlay fallback");
                await FallbackAsync(host, origin, apply, newCanvas, done, started);
                return;
            }
            // The snapshot is on screen: switch the theme underneath it.
            FrameTrace.Mark("theme.shown");
            apply();
            FrameTrace.Mark("theme.applied");
        }
        _overlay = image;

        var size = new Size(host.Width, host.Height);
        if (MotionSettings.ReduceMotion)
        {
            var fade = new TimedProgress(1, 0.2, Curve.Linear);
            fade.Retarget(0);
            Run(host, image, done, started, dt =>
            {
                fade.Step(dt);
                image.Opacity = fade.Value;
                FrameTrace.Mark(FormattableString.Invariant($"theme.fade {fade.Value:F3}"));
                return !fade.IsDone;
            });
            return;
        }

        var start = 22.0;  // the button's radius: the disc starts at its size
        var cover = Blob.CoverRadius(origin.X, origin.Y, size.Width, size.Height);
        var progress = TimedProgress.ThemeReveal();
        progress.Retarget(1);
        Action<double, double>? hole = null;
#if WINDOWS
        hole = BlobClip.NativeHole(image, origin);
#endif
        hole ??= (r, phase) => image.Clip = BlobClip.RectMinusDisc(size, origin, r, phase);
        hole(start, 0);
        Run(host, image, done, started, dt =>
        {
            progress.Step(dt);
            var p = progress.Value;
            var r = start + (cover - start) * p;
            hole(r, Blob.PhaseAt(p));
            FrameTrace.Mark(FormattableString.Invariant($"theme.wipe {r:F2} {Blob.PhaseAt(p):F4} {size.Width:F0} {size.Height:F0} {origin.X:F1} {origin.Y:F1}"));
            return !progress.IsDone;
        });
    }

    private static void Run(Grid host, View overlay, Action? whenGone, Action? started, Func<double, bool> step)
    {
        var done = false;
        var first = true;
        bool Step(double dt)
        {
            if (done)
            {
                return false;
            }
            // The theme switch restyles the whole window, so the first tick
            // after it can arrive ~100 ms late; count the wipe from its first
            // drawn frame, or that frame would skip a fifth of the curve.
            if (first)
            {
                first = false;
                dt = 0;
                started?.Invoke();
            }
            if (step(dt))
            {
                return true;
            }
            done = true;
            host.Children.Remove(overlay);
            FrameTrace.Mark("theme.end");
            if (ReferenceEquals(_overlay, overlay))
            {
                _overlay = null;
                _finish = null;
            }
            whenGone?.Invoke();
            return false;
        }
        _finish = _ =>
        {
            done = true;
            host.Children.Remove(overlay);
            FrameTrace.Mark("theme.interrupted");
            whenGone?.Invoke();
            return false;
        };
        Spr.OnFrame(Step, host.Dispatcher);
    }

    // ---- native snapshot ----------------------------------------------------------

    private static async Task<object?> CaptureNativeAsync(VisualElement target)
    {
#if WINDOWS
        if (target.Handler?.PlatformView is Microsoft.UI.Xaml.UIElement element)
        {
            var bitmap = new Microsoft.UI.Xaml.Media.Imaging.RenderTargetBitmap();
            await bitmap.RenderAsync(element);
            return bitmap;
        }
        return null;
#elif ANDROID
        if (target.Handler?.PlatformView is not Android.Views.View view || view.Width <= 0 || view.Height <= 0)
        {
            return null;
        }
        var bitmap = Android.Graphics.Bitmap.CreateBitmap(view.Width, view.Height, Android.Graphics.Bitmap.Config.Argb8888!);
        // PixelCopy reads what the GPU actually drew (API 26+). View.Draw into a
        // software canvas throws on hardware bitmaps (images decoded by Glide)
        // and misses RenderEffects.
        if (OperatingSystem.IsAndroidVersionAtLeast(26) &&
            Microsoft.Maui.ApplicationModel.Platform.CurrentActivity?.Window is { } activityWindow)
        {
            var at = new int[2];
            view.GetLocationInWindow(at);
            var done = new TaskCompletionSource<bool>();
            Android.Views.PixelCopy.Request(activityWindow,
                new Android.Graphics.Rect(at[0], at[1], at[0] + view.Width, at[1] + view.Height),
                bitmap, new PixelCopyDone(ok => done.TrySetResult(ok)),
                new Android.OS.Handler(Android.OS.Looper.MainLooper!));
            if (await done.Task)
            {
                return bitmap;
            }
        }
        try
        {
            view.Draw(new Android.Graphics.Canvas(bitmap));
            return bitmap;
        }
        catch (Java.Lang.IllegalArgumentException)
        {
            bitmap.Recycle();
            return null;  // the caller logs and uses the overlay fallback
        }
#else
        // iOS / Mac: MAUI's capture (PNG round-trip); not latency-tuned.
        if (await target.CaptureAsync() is not { } shot)
        {
            return null;
        }
        await using var stream = await shot.OpenReadAsync();
        using var copy = new MemoryStream();
        await stream.CopyToAsync(copy);
        var bytes = copy.ToArray();
        return ImageSource.FromStream(() => new MemoryStream(bytes));
#endif
    }

    // Puts the snapshot on the overlay's native image view and waits until
    // it has been drawn (one rendered frame).
    private static async Task<bool> ShowAsync(Image image, object snapshot)
    {
#if WINDOWS
        if (image.Handler?.PlatformView is not Microsoft.UI.Xaml.Controls.Image native ||
            snapshot is not Microsoft.UI.Xaml.Media.ImageSource source)
        {
            return false;
        }
        native.Stretch = Microsoft.UI.Xaml.Media.Stretch.Fill;
        native.Source = source;
        await NextFramesAsync(2);
        return true;
#elif ANDROID
        if (image.Handler?.PlatformView is not Android.Widget.ImageView native ||
            snapshot is not Android.Graphics.Bitmap bitmap)
        {
            return false;
        }
        native.SetScaleType(Android.Widget.ImageView.ScaleType.FitXy!);
        native.SetImageBitmap(bitmap);
        image.Unloaded += (_, _) => { native.SetImageBitmap(null); bitmap.Recycle(); };
        await NextFramesAsync(2);
        return true;
#else
        if (snapshot is not ImageSource source)
        {
            return false;
        }
        image.Source = source;
        for (var i = 0; i < 30 && (image.IsLoading || image.Width <= 0); i++)
        {
            await Task.Delay(10);
        }
        return true;
#endif
    }

    private static Task NextFramesAsync(int frames)
    {
        var tcs = new TaskCompletionSource();
#if WINDOWS
        var seen = 0;
        void OnRendering(object? sender, object e)
        {
            if (++seen >= frames)
            {
                Microsoft.UI.Xaml.Media.CompositionTarget.Rendering -= OnRendering;
                tcs.TrySetResult();
            }
        }
        Microsoft.UI.Xaml.Media.CompositionTarget.Rendering += OnRendering;
#elif ANDROID
        var left = frames;
        FrameCallback? callback = null;
        callback = new FrameCallback(() =>
        {
            if (--left <= 0)
            {
                tcs.TrySetResult();
            }
            else
            {
                Android.Views.Choreographer.Instance!.PostFrameCallback(callback!);
            }
        });
        Android.Views.Choreographer.Instance!.PostFrameCallback(callback);
#else
        tcs.TrySetResult();
#endif
        return tcs.Task;
    }

#if ANDROID
    private sealed class PixelCopyDone(Action<bool> done) : Java.Lang.Object, Android.Views.PixelCopy.IOnPixelCopyFinishedListener
    {
        public void OnPixelCopyFinished(int copyResult) => done(copyResult == (int)Android.Views.PixelCopyResult.Success);
    }

    private sealed class FrameCallback(Action onFrame) : Java.Lang.Object, Android.Views.Choreographer.IFrameCallback
    {
        public void DoFrame(long frameTimeNanos) => onFrame();
    }
#endif

    // Ends a running wipe at once (the new theme stays) and undoes a press.
    public static void Finish(Grid host)
    {
        if (_overlay is not null)
        {
            FinishRunning(host);
        }
        if (_pending is { } p)
        {
            _ = CancelAsync(host, p, "finished");
        }
    }

    private static void FinishRunning(Grid host)
    {
        if (_overlay is not null)
        {
            _finish?.Invoke(0);
            host.Children.Remove(_overlay);
            _overlay = null;
            _finish = null;
        }
    }

    private static async Task FallbackAsync(Grid host, Point origin, Action apply, Color newCanvas, Action? done, Action? started = null)
    {
        var cover = Blob.CoverRadius(origin.X, origin.Y, host.Width, host.Height);
        var disc = new Ellipse
        {
            Fill = new SolidColorBrush(newCanvas),
            WidthRequest = cover * 2,
            HeightRequest = cover * 2,
            HorizontalOptions = LayoutOptions.Start,
            VerticalOptions = LayoutOptions.Start,
            TranslationX = origin.X - cover,
            TranslationY = origin.Y - cover,
            InputTransparent = true,
            Scale = MotionSettings.ReduceMotion ? 1 : 22 / cover,
            Opacity = MotionSettings.ReduceMotion ? 0 : 1,
        };
        Grid.SetRowSpan(disc, Math.Max(1, host.RowDefinitions.Count));
        Grid.SetColumnSpan(disc, Math.Max(1, host.ColumnDefinitions.Count));
        host.Children.Add(disc);
        _overlay = disc;
        var progress = MotionSettings.ReduceMotion ? new TimedProgress(0, 0.1, Curve.Linear) : TimedProgress.ThemeReveal();
        progress.Retarget(1);
        var tcs = new TaskCompletionSource();
        Spr.OnFrame(dt =>
        {
            progress.Step(dt);
            if (MotionSettings.ReduceMotion)
            {
                disc.Opacity = progress.Value;
            }
            else
            {
                disc.Scale = 22 / cover + (1 - 22 / cover) * progress.Value;
            }
            if (progress.IsDone)
            {
                tcs.TrySetResult();
            }
            return !progress.IsDone;
        }, host.Dispatcher);
        await tcs.Task;
        apply();
        started?.Invoke();
        Spr.To(disc, Prop.Opacity, 0, SpringSpec.Default, settled: () =>
        {
            host.Children.Remove(disc);
            if (ReferenceEquals(_overlay, disc))
            {
                _overlay = null;
            }
            done?.Invoke();
        });
    }
}
