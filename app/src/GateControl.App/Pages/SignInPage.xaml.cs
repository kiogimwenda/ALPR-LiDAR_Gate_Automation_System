using GateControl.Core.ViewModels;
using GateControl.Motion;

namespace GateControl.Pages;

public partial class SignInPage : ContentPage
{
    public SignInPage(SignInViewModel vm)
    {
        InitializeComponent();
        BindingContext = vm;
    }

    protected override void OnAppearing()
    {
        base.OnAppearing();
        Spr.Enter(Card, fromY: 18);
        // Put the cursor where the work is.
        var vm = (SignInViewModel)BindingContext;
        (vm.Server.Length == 0 ? ServerEntry : vm.Username.Length == 0 ? UserEntry : PasswordEntry).Focus();
    }

    private void OnServerCompleted(object? sender, EventArgs e) => UserEntry.Focus();
    private void OnUserCompleted(object? sender, EventArgs e) => PasswordEntry.Focus();
}
