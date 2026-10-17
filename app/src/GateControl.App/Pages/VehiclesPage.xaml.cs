using GateControl.Core.ViewModels;

namespace GateControl.Pages;

public partial class VehiclesPage : PageBase
{
    private readonly AllowlistViewModel _vm;

    // Typed handle for item templates: {Binding Vm.X, Source={x:Reference Page}}.
    public AllowlistViewModel Vm => _vm;

    public VehiclesPage(AllowlistViewModel vm)
    {
        InitializeComponent();
        BindingContext = _vm = vm;
        // Keep the last editor bound while the sheet slides away, so the
        // form doesn't blank out mid-animation.
        vm.PropertyChanged += (_, e) =>
        {
            if (e.PropertyName == nameof(AllowlistViewModel.Editor) && vm.Editor is { } editor)
            {
                EditorForm.BindingContext = editor;
            }
        };
    }

    protected override Task LoadAsync() => _vm.CanEdit ? _vm.LoadAsync() : Task.CompletedTask;
}
