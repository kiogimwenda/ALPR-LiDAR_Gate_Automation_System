using GateControl.Core.ViewModels;

namespace GateControl.Pages;

public partial class DashboardPage : PageBase
{
    private readonly DashboardViewModel _vm;

    // Typed handle for item templates: {Binding Vm.X, Source={x:Reference Page}}.
    public DashboardViewModel Vm => _vm;
    private readonly IDispatcherTimer _refresh;

    public DashboardPage(DashboardViewModel vm)
    {
        InitializeComponent();
        BindingContext = _vm = vm;
        PeriodPicker.Items = ["Today", "7 days", "30 days"];
        // Quietly refresh while visible (MeruLPR refreshes every 30 s).
        _refresh = Dispatcher.CreateTimer();
        _refresh.Interval = TimeSpan.FromSeconds(30);
        _refresh.Tick += async (_, _) => await _vm.LoadAsync();
    }

    protected override Task LoadAsync() => _vm.LoadAsync();

    public void ReplayCharts() => HourlyChart.Replay();

    protected override void OnAppearing()
    {
        base.OnAppearing();
        _refresh.Start();
    }

    protected override void OnDisappearing()
    {
        base.OnDisappearing();
        _refresh.Stop();
    }

    // The glow image follows the card's size without taking part in layout
    // (sized larger than the card, it stretched the card to its height).
    private void OnAccentSized(object? sender, EventArgs e)
    {
        var (glow, card) = (LeadGlow, LeadCard);
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
