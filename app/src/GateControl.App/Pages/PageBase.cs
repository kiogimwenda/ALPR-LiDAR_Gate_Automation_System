// PageBase.cs — load on first appearance, refresh on return.
//
// Tab pages are hosted by the persistent RootPage, which forwards their
// appearing / disappearing through HostAppearing / HostDisappearing and
// runs their entrance on every switch. Pushed pages (plate history,
// admin) enter here, the first time they appear.

using GateControl.Motion;

namespace GateControl.Pages;

public abstract class PageBase : ContentPage
{
    private bool _loaded;

    protected abstract Task LoadAsync();

    protected override async void OnAppearing()
    {
        base.OnAppearing();
        if (!_loaded && Content is VisualElement content)
        {
            Controls.ScrollFit.Apply(content);
            // First paint: staggered entrance (spec A3/A4), not a pop.
            Reveal.Page(content);
        }
        _loaded = true;
        Motion.FrameTrace.Mark("data.load.start");
        await LoadAsync();
        Motion.FrameTrace.Mark("data.load.end");
    }

    // RootPage, while the tab is parked off-screen: load the data ahead of
    // the first visit, so the first entrance doesn't inflate rows mid-reveal.
    public async Task PreloadAsync()
    {
        if (_loaded)
        {
            return;
        }
        _loaded = true;
        await LoadAsync();
    }

    public bool HasLoaded => _loaded;

    // Called by RootPage for hosted tab pages.
    public void HostAppearing() => OnAppearing();

    public void HostDisappearing() => OnDisappearing();
}
