// WinTransform.cs — Windows: animate translation / scale / rotation without
// allocating a WinUI transform per write.
//
// MAUI's own mapping (TransformationExtensions.UpdateTransformation) builds
// a new ScaleTransform and CompositeTransform every time TranslationX/Y,
// Scale or Rotation changes, i.e. per element per frame while the spring
// ticker runs. Each new WinUI object reports native memory to the GC
// (GC.AddMemoryPressure via the projection), and that pressure, not managed
// allocation, induced a gen-2 collection (EventPipe: GCTriggered reason
// InducedNotForced, stack Spr.Write → MapTranslationY → UpdateTransformation)
// 100–700 ms into the first tab entrance after launch: one 40–95 ms frame.
// Here each element keeps one CompositeTransform and only its values change.
// 3D rotation (RotationX/Y, a PlaneProjection) still goes to MAUI.

#if WINDOWS
using System.Runtime.CompilerServices;
using Microsoft.Maui.Handlers;
using Microsoft.Maui.Platform;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Media;

namespace GateControl.Platform;

public static class WinTransform
{
    private static readonly ConditionalWeakTable<FrameworkElement, CompositeTransform> Transforms = new();

    public static void Register()
    {
        foreach (var key in new[]
        {
            nameof(IView.TranslationX), nameof(IView.TranslationY), nameof(IView.Scale),
            nameof(IView.ScaleX), nameof(IView.ScaleY), nameof(IView.Rotation),
            nameof(IView.RotationX), nameof(IView.RotationY), nameof(IView.AnchorX), nameof(IView.AnchorY),
        })
        {
            ViewHandler.ViewMapper.ModifyMapping(key, (handler, view, previous) =>
            {
                if (!Apply(handler, view))
                {
                    previous?.Invoke(handler, view);
                }
            });
        }
    }

    // Gives `view` its transform ahead of its first animated write.
    public static void Ensure(VisualElement view)
    {
        if (view.Handler is IViewHandler handler)
        {
            Apply(handler, view);
        }
    }

#if DEBUG || GC_HARNESS
    // Harness only ("parity"): false sends every write to stock MAUI, for a
    // side-by-side of the two paths.
    public static bool Enabled { get; set; } = true;
#endif

    private static bool Apply(IViewHandler handler, IView view)
    {
#if DEBUG || GC_HARNESS
        if (!Enabled)
        {
            return false;
        }
#endif
        if (view.RotationX % 360 != 0 || view.RotationY % 360 != 0 ||
            (handler.ContainerView ?? handler.PlatformView) is not FrameworkElement element)
        {
            return false;
        }
        // A container (WrapperView, for a Clip or Shadow) can appear after the
        // first animated write; the inner view then still holds our transform
        // and the container gets another one, applying it twice. Take ours off
        // the inner view (review round 3, item 3).
        if (handler.ContainerView is not null && handler.PlatformView is FrameworkElement inner &&
            !ReferenceEquals(inner, element) && Transforms.TryGetValue(inner, out var stale))
        {
            if (ReferenceEquals(inner.RenderTransform, stale))
            {
                inner.RenderTransform = null;
            }
            Transforms.Remove(inner);
        }
        var origin = new global::Windows.Foundation.Point(view.AnchorX, view.AnchorY);
        if (element.RenderTransformOrigin != origin)
        {
            element.RenderTransformOrigin = origin;
        }
        if (element.Projection is not null)
        {
            element.Projection = null;
        }
        if (!Transforms.TryGetValue(element, out var t))
        {
            t = new CompositeTransform();
            Transforms.Add(element, t);
        }
        if (!ReferenceEquals(element.RenderTransform, t))
        {
            element.RenderTransform = t;
        }
        var scaleX = view.Scale * view.ScaleX;
        var scaleY = view.Scale * view.ScaleY;
        // Unchanged values are not written (each write crosses into WinUI).
        if (t.TranslateX != view.TranslationX) { t.TranslateX = view.TranslationX; }
        if (t.TranslateY != view.TranslationY) { t.TranslateY = view.TranslationY; }
        if (t.ScaleX != scaleX) { t.ScaleX = scaleX; }
        if (t.ScaleY != scaleY) { t.ScaleY = scaleY; }
        if (t.Rotation != view.Rotation) { t.Rotation = view.Rotation; }
        return true;
    }
}
#endif
