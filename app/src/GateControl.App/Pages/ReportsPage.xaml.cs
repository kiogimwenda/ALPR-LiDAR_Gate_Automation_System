using GateControl.Core.Formatting;
using GateControl.Core.Services;
using GateControl.Core.ViewModels;

namespace GateControl.Pages;

public partial class ReportsPage : PageBase, IPlatePaneHost
{
    private readonly Session _session;
    private readonly ReportsViewModel _vm;

    // Typed handle for item templates: {Binding Vm.X, Source={x:Reference Page}}.
    public ReportsViewModel Vm => _vm;
    private bool _first = true;

    public ReportsPage(ReportsViewModel vm, Session session)
    {
        _session = session;
        InitializeComponent();
        Panes.SizeChanged += (_, _) => TwoPane.Apply(Panes, DetailPane, 820);
        BindingContext = _vm = vm;
        KindPicker.Items = ["Search", "Visits", "Unreadable", "Trend"];
        vm.PropertyChanged += (_, e) =>
        {
            if (e.PropertyName == nameof(ReportsViewModel.MinConfidence))
            {
                ConfidenceLabel.Text = vm.MinConfidence <= 0 ? "Any" : Display.Percent(vm.MinConfidence);
            }
        };
        ConfidenceLabel.Text = "Any";
    }

    public bool HasPane => DetailPane.IsVisible;

    public async Task ShowPlateAsync(string plate)
    {
        DetailEmpty.IsVisible = false;
        Detail.IsVisible = true;
        await Detail.ShowAsync(new PlateDetailViewModel(_session, plate));
    }

    protected override Task LoadAsync()
    {
        if (!_first)
        {
            return Task.CompletedTask;  // keep the operator's results when returning
        }
        _first = false;
        return _vm.LoadAsync();
    }

    private async void OnDateChanged(object? sender, DateChangedEventArgs e)
    {
        if (!_first)
        {
            await _vm.LoadAsync();
        }
    }
}
