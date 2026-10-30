// HoverLift.cs — cards rise 5 dip under the pointer (spec A12, from the
// reference's `whileHover={{ y: -5 }}`). Pointer platforms only; touch
// keeps Pressable's press feedback. Reduce Motion: no lift.

using GateControl.Core.Motion;

namespace GateControl.Motion;

public static class HoverLift
{
    public const double Lift = -5;

    public static readonly BindableProperty EnabledProperty = BindableProperty.CreateAttached(
        "Enabled", typeof(bool), typeof(HoverLift), false,
        propertyChanged: (b, _, n) => { if ((bool)n && b is View v) Attach(v); });

    public static bool GetEnabled(BindableObject b) => (bool)b.GetValue(EnabledProperty);
    public static void SetEnabled(BindableObject b, bool v) => b.SetValue(EnabledProperty, v);

    private static void Attach(View view)
    {
        var pointer = new PointerGestureRecognizer();
        pointer.PointerEntered += (_, _) =>
        {
            if (!MotionSettings.ReduceMotion)
            {
                Spr.To(view, Prop.TranslationY, Lift, SpringSpec.Default);
            }
        };
        pointer.PointerExited += (_, _) => Spr.To(view, Prop.TranslationY, 0, SpringSpec.Default);
        view.GestureRecognizers.Add(pointer);
    }
}
