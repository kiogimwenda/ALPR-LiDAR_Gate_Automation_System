// TileMath.cs — column count and tile width for TileGrid (equal-width,
// wrapping tiles). Pure, so it is unit-tested.

namespace GateControl.Core.Formatting;

public static class TileMath
{
    public static int Columns(double width, int count, double minTile, double spacing, int maxColumns)
    {
        if (count <= 0 || width <= 0 || minTile <= 0)
        {
            return 1;
        }
        var fit = (int)Math.Floor((width + spacing) / (minTile + spacing));
        return Math.Clamp(Math.Min(fit, Math.Min(count, maxColumns)), 1, Math.Max(1, maxColumns));
    }

    public static double TileWidth(double width, int columns, double spacing) =>
        Math.Max(0, (width - (columns - 1) * spacing) / Math.Max(1, columns));
}
