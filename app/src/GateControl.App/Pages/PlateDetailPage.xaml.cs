using GateControl.Core.ViewModels;

namespace GateControl.Pages;

public partial class PlateDetailPage : PageBase
{
    private readonly PlateDetailViewModel _vm;

    public PlateDetailPage(PlateDetailViewModel vm)
    {
        InitializeComponent();
        BindingContext = _vm = vm;
    }

    protected override Task LoadAsync() => _vm.LoadAsync();
}
