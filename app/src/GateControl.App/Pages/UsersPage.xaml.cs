using GateControl.Core.ViewModels;

namespace GateControl.Pages;

public partial class UsersPage : PageBase
{
    private readonly UsersViewModel _vm;

    // Typed handle for item templates: {Binding Vm.X, Source={x:Reference Page}}.
    public UsersViewModel Vm => _vm;

    public UsersPage(UsersViewModel vm)
    {
        InitializeComponent();
        BindingContext = _vm = vm;
        vm.PropertyChanged += (_, e) =>
        {
            if (e.PropertyName == nameof(UsersViewModel.Editor) && vm.Editor is { } editor)
            {
                EditorForm.BindingContext = editor;
            }
        };
    }

    protected override Task LoadAsync() => _vm.LoadAsync();
}
