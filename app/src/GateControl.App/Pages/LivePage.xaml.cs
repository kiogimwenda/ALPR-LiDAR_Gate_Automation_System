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
        // On the accent card: a white dot when live, amber when not.
        FeedDot.BackgroundColor = _vm.IsLive ? Colors.White : ChartView.ToneColor(Tone.Caution);
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
        // Hosted in RootPage: ask through the window's page, not this one.
        var presenter = Window?.Page ?? Application.Current?.Windows.FirstOrDefault()?.Page ?? this;
        var choice = await presenter.DisplayActionSheetAsync(tile.Name, "Cancel", null,
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

    // The glow image follows the card's size without taking part in layout
    // (sized larger than the card, it stretched the card to its height).
    private void OnAccentSized(object? sender, EventArgs e)
    {
        var (glow, card) = (HeroGlow, HeroCard);
        if (card.Width <= 0 || card.Height <= 0)
        {
            return;
        }
        // Same size as the card (so it never grows the cell), enlarged by a
        // transform to spill 40 dip past the sides and 32 above and below.
        glow.WidthRequest = card.Width;
        glow.HeightRequest = card.Height;
        glow.ScaleX = (card.Width + 80) / card.Width;
        glow.ScaleY = (card.Height + 64) / card.Height;
    }
}
