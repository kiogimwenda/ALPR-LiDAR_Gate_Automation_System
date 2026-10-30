// IconsExtra.cs — hand-drawn glyphs on Lucide's 24×24 grid (stroke 2, round
// caps) for the two Lucide icons whose path data the platforms' native
// shape renderers drop (tiny arcs and nested curves in "activity" and
// "car"). Lines and full-circle arcs only. Same licence terms as our code.

namespace GateControl.Controls;

public static class IconsExtra
{
    // A pulse line (in place of Lucide "activity").
    public const string Pulse = "M 22 12 L 18 12 L 15 21 L 9 3 L 6 12 L 2 12";

    // A car seen from the side (in place of Lucide "car").
    public const string Vehicle =
        "M 5 17 L 3 17 L 3 12 L 6 7 L 16 7 L 19 11 L 21 12 L 21 17 L 19 17 " +
        "M 9 17 L 15 17 M 3 12 L 19 12 " +
        "M 5 17 A 2 2 0 1 0 9 17 A 2 2 0 1 0 5 17 Z " +
        "M 15 17 A 2 2 0 1 0 19 17 A 2 2 0 1 0 15 17 Z";
}
