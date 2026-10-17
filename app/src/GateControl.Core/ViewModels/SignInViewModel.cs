// SignInViewModel.cs — server address + credentials.

using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using GateControl.Core.Services;

namespace GateControl.Core.ViewModels;

public sealed partial class SignInViewModel : ViewModelBase
{
    private readonly SessionHost _host;
    private readonly INavigator _nav;
    private readonly IHaptics _haptics;

    public SignInViewModel(SessionHost host, INavigator nav, IHaptics haptics)
    {
        _host = host;
        _nav = nav;
        _haptics = haptics;
        _server = host.SavedServer ?? "";
        _username = host.SavedUsername ?? "";
    }

    [ObservableProperty]
    [NotifyCanExecuteChangedFor(nameof(SignInCommand))]
    private string _server;

    [ObservableProperty]
    [NotifyCanExecuteChangedFor(nameof(SignInCommand))]
    private string _username;

    [ObservableProperty]
    [NotifyCanExecuteChangedFor(nameof(SignInCommand))]
    private string _password = "";

    private bool CanSignIn() => Server.Trim().Length > 0 && !IsBusy;

    [RelayCommand(CanExecute = nameof(CanSignIn))]
    private async Task SignInAsync()
    {
        if (!SessionHost.TryParseServer(Server, out var uri))
        {
            Error = "Enter the gate server's address, for example 192.168.1.50:8080.";
            _haptics.Play(HapticKind.Error);
            return;
        }
        var ok = await RunAsync(() => _host.SignInAsync(uri, Username.Trim(), Password));
        if (ok)
        {
            Password = "";
            _haptics.Play(HapticKind.Success);
            await _nav.ShowShellAsync();
        }
        else
        {
            _haptics.Play(HapticKind.Error);
        }
    }
}
