using System.Globalization;
using GateControl.Core.Formatting;

namespace GateControl.Core.Tests;

public class NumberTextTests
{
    private static readonly CultureInfo En = CultureInfo.GetCultureInfo("en-US");

    [Theory]
    [InlineData("12,406", "", 12406, 0, true, "")]
    [InlineData("87%", "", 87, 0, false, "%")]
    [InlineData("3.5 h", "", 3.5, 1, false, " h")]
    [InlineData("$635,386.60", "$", 635386.60, 2, true, "")]
    [InlineData("-2,500 pts", "", -2500, 0, true, " pts")]
    public void FindsTheNumberAndItsFormat(string text, string prefix, double value, int decimals, bool grouped, string suffix)
    {
        var n = NumberText.Parse(text, En)!;
        Assert.Equal(prefix, n.Prefix);
        Assert.Equal(value, n.Value, 6);
        Assert.Equal(decimals, n.Decimals);
        Assert.Equal(grouped, n.Grouped);
        Assert.Equal(suffix, n.Suffix);
        Assert.Equal(text, n.Format(value));
    }

    [Theory]
    [InlineData(null)]
    [InlineData("")]
    [InlineData("—")]
    [InlineData("Never")]
    [InlineData("14:00")]
    [InlineData("Peak 9:00")]
    [InlineData("17/04/2026")]
    [InlineData("3 PM")]
    public void TextWithoutANumberIsNotAnimated(string? text)
    {
        Assert.Null(NumberText.Parse(text, En));
    }

    [Fact]
    public void FormatsIntermediateValuesLikeTheTarget()
    {
        var n = NumberText.Parse("$12,406.00", En)!;
        Assert.Equal("$0.00", n.Format(0));
        Assert.Equal("$4,360.25", n.Format(4360.2499));
        Assert.Equal("$1,000.00", n.Format(999.999));
    }

    [Fact]
    public void RespectsTheCulture()
    {
        var de = CultureInfo.GetCultureInfo("de-DE");
        var n = NumberText.Parse("12.406,5 km", de)!;
        Assert.Equal(12406.5, n.Value, 6);
        Assert.Equal("1.000,0 km", n.Format(1000));
    }
}

public class TileMathTests
{
    [Theory]
    [InlineData(524, 3, 150, 8, 6, 3)]   // KPI container: three tiles fit
    [InlineData(300, 3, 150, 8, 6, 1)]   // narrow: one per row
    [InlineData(330, 5, 150, 8, 6, 2)]
    [InlineData(2000, 3, 150, 8, 6, 3)]  // never more columns than tiles
    [InlineData(2000, 9, 150, 8, 4, 4)]  // capped
    [InlineData(0, 3, 150, 8, 6, 1)]
    [InlineData(500, 0, 150, 8, 6, 1)]
    public void ColumnsFitTheWidth(double width, int count, double min, double spacing, int max, int expected)
    {
        Assert.Equal(expected, GateControl.Core.Formatting.TileMath.Columns(width, count, min, spacing, max));
    }

    [Fact]
    public void TilesFillTheRowExactly()
    {
        var w = GateControl.Core.Formatting.TileMath.TileWidth(524, 3, 8);
        Assert.Equal(524, 3 * w + 2 * 8, 6);
    }
}
