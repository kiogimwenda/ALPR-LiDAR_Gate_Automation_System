// PageBase.cs — load on first appearance, refresh on return.

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
            // First paint: the page's content settles in rather than popping.
            Spr.Enter(content, fromY: MotionSettings.ReduceMotion ? 0 : 10);
        }
        _loaded = true;
        await LoadAsync();
    }
}
