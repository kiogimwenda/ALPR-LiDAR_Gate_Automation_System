using GateControl.Core.Services;
using GateControl.Core.ViewModels;

namespace GateControl.Pages;

public partial class AlertsPage : PageBase, IPlatePaneHost
{
    private const double TwoPaneWidth = 760;

    private readonly AlertsViewModel _vm;
    private readonly Session _session;

    // Typed handle for item templates: {Binding Vm.X, Source={x:Reference Page}}.
    public AlertsViewModel Vm => _vm;

    public AlertsPage(AlertsViewModel vm, Session session)
    {
        _session = session;
        InitializeComponent();
        BindingContext = _vm = vm;
        Filter.Items = ["Open", "All"];
        Filter.PropertyChanged += (_, e) =>
        {
            if (e.PropertyName == nameof(Filter.SelectedIndex))
            {
                _vm.UnreadOnly = Filter.SelectedIndex == 0;
            }
        };
        Panes.SizeChanged += (_, _) => TwoPane.Apply(Panes, DetailPane, TwoPaneWidth);
    }

    public bool HasPane => DetailPane.IsVisible;

    public async Task ShowPlateAsync(string plate)
    {
        DetailEmpty.IsVisible = false;
        Detail.IsVisible = true;
        await Detail.ShowAsync(new PlateDetailViewModel(_session, plate));
    }

    protected override Task LoadAsync() => _vm.LoadAsync();
}
