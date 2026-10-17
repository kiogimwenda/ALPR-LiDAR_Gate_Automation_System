using GateControl.Core.ViewModels;

namespace GateControl.Pages;

public partial class AlertsPage : PageBase
{
    private readonly AlertsViewModel _vm;

    // Typed handle for item templates: {Binding Vm.X, Source={x:Reference Page}}.
    public AlertsViewModel Vm => _vm;

    public AlertsPage(AlertsViewModel vm)
    {
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
    }

    protected override Task LoadAsync() => _vm.LoadAsync();
}
