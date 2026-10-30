using GateControl.Core.ViewModels;
using GateControl.Motion;

namespace GateControl.Views;

public partial class PlateHistoryView : ContentView
{
    public PlateHistoryView()
    {
        InitializeComponent();
        Controls.ScrollFit.Apply(this);
    }

    // Shows a plate (in the two-pane layouts): bind, load, enter.
    public async Task ShowAsync(PlateDetailViewModel vm)
    {
        BindingContext = vm;
        Reveal.Page(Body);
        await vm.LoadAsync();
    }
}
