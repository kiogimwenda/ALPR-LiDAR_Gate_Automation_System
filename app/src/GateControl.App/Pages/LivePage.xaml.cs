using GateControl.Controls;
using GateControl.Core.Formatting;
using GateControl.Core.ViewModels;
using GateControl.Motion;

namespace GateControl.Pages;

public partial class LivePage : PageBase
{
    private readonly LiveViewModel _vm;

    // Typed handle for item templates: {Binding Vm.X, Source={x:Reference Page}}.
    public LiveViewModel Vm => _vm;
    private readonly IDispatcherTimer _staleness;

    public LivePage(LiveViewModel vm)
    {
        InitializeComponent();
        BindingContext = _vm = vm;
        // A new vehicle drops into the top of the feed (or fades, with Reduce Motion).
        FeedList.ChildAdded += (_, e) =>
        {
            if (e.Element is VisualElement row && FeedList.Children.IndexOf(row) == 0)
            {
                Spr.Enter(row);
            }
        };
        vm.PropertyChanged += (_, e) =>
        {
            if (e.PropertyName == nameof(LiveViewModel.FeedState))
            {
                PaintFeedState();
            }
        };
        PaintFeedState();
        // A gate that stops reporting greys out within a second.
        _staleness = Dispatcher.CreateTimer();
        _staleness.Interval = TimeSpan.FromSeconds(1);
        _staleness.Tick += (_, _) => _vm.RefreshStaleness();
    }

    private void PaintFeedState()
    {
        var tone = _vm.IsLive ? Tone.Positive : Tone.Caution;
        FeedDot.Fill = new SolidColorBrush(ChartView.ToneColor(tone));
        FeedPill.BackgroundColor = ChartView.ToneColor(tone).WithAlpha(0.16f);
    }

    protected override Task LoadAsync() => _vm.LoadAsync();

    protected override void OnAppearing()
    {
        base.OnAppearing();
        _staleness.Start();
    }

    protected override void OnDisappearing()
    {
        base.OnDisappearing();
        _staleness.Stop();
    }

    private async void OnMoreClicked(object? sender, EventArgs e)
    {
        if (sender is not Button { CommandParameter: GateTileViewModel tile })
        {
            return;
        }
        var choice = await DisplayActionSheetAsync(tile.Name, "Cancel", null,
            tile.IsLocked || tile.State == "OPEN" ? ["Release hold", "Hold open", "Lock down"] : ["Hold open", "Lock down"]);
        switch (choice)
        {
            case "Hold open":
                await tile.HoldOpenCommand.ExecuteAsync(null);
                break;
            case "Lock down":
                await tile.LockdownCommand.ExecuteAsync(null);
                break;
            case "Release hold":
                await tile.ReleaseCommand.ExecuteAsync(null);
                break;
        }
    }
}
