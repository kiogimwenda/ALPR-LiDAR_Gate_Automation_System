// PlatformBlur.cs — the reveal blur (spec A2–A4) where the platform can do
// it on the GPU: Android 12+ (API 31) RenderEffect on the view's layer.
// Windows, iOS, Mac and older Android have no per-view blur that is cheap
// per frame; there the reveal's rise + scale stand in (spec §11 deviations).

namespace GateControl.Motion;

public static class PlatformBlur
{
    // Off until measured within budget on a real Android 12+ device
    // (review: p90 ≤ 16.7 ms with blur on). The emulator never booted in
    // Phase 2b, so it stays off; the rise + scale substitute runs instead.
    public static bool Enabled { get; set; }

    public static bool Supported =>
#if ANDROID
        Enabled && OperatingSystem.IsAndroidVersionAtLeast(31);
#else
        false;
#endif

    // Blur `view` by `radius` dip; 0 removes the effect.
    public static void Set(VisualElement view, double radius)
    {
#if ANDROID
        if (!OperatingSystem.IsAndroidVersionAtLeast(31) || view.Handler is not IViewHandler handler)
        {
            return;
        }
        var native = (handler.ContainerView ?? handler.PlatformView) as Android.Views.View;
        if (native is null)
        {
            return;
        }
        if (radius <= 0.05)
        {
            native.SetRenderEffect(null);
            return;
        }
        var px = (float)(radius * DeviceDisplay.Current.MainDisplayInfo.Density);
        native.SetRenderEffect(Android.Graphics.RenderEffect.CreateBlurEffect(px, px, Android.Graphics.Shader.TileMode.Decal!));
#else
        _ = view;
        _ = radius;
#endif
    }
}
