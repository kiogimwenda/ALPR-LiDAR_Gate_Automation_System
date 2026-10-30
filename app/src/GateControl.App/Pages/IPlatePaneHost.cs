// IPlatePaneHost.cs — tab pages with a detail pane (Alerts, Reports): on
// wide windows a tapped plate opens in the pane beside the list (the
// reference's two-pane Messages layout) instead of being pushed.

namespace GateControl.Pages;

public interface IPlatePaneHost
{
    // True when the pane is showing (wide window).
    bool HasPane { get; }

    Task ShowPlateAsync(string plate);
}
