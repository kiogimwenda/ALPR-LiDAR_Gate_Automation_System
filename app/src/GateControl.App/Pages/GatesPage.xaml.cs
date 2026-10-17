using GateControl.Core.ViewModels;

namespace GateControl.Pages;

public partial class GatesPage : PageBase
{
    private readonly GatesViewModel _vm;

    // Typed handle for item templates: {Binding Vm.X, Source={x:Reference Page}}.
    public GatesViewModel Vm => _vm;

    public GatesPage(GatesViewModel vm)
    {
        InitializeComponent();
        BindingContext = _vm = vm;
    }

    protected override Task LoadAsync() => _vm.LoadAsync();
}
