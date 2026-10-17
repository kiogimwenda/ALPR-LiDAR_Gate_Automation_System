using GateControl.Core.ViewModels;

namespace GateControl.Pages;

public partial class DataPage : PageBase
{
    private readonly DataSettingsViewModel _vm;

    public DataPage(DataSettingsViewModel vm)
    {
        InitializeComponent();
        BindingContext = _vm = vm;
        vm.PropertyChanged += (_, _) => Paint();
        Paint();
    }

    private void Paint()
    {
        RetentionLabel.Text = $"{Math.Round(_vm.RetentionDays):0} days";
        TimeoutLabel.Text = $"{Math.Round(_vm.SessionTimeoutHours):0} hours";
    }

    protected override Task LoadAsync() => _vm.LoadAsync();
}
