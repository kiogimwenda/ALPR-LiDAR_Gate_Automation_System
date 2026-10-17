// App.xaml.cs — launch: resume a saved session or ask to sign in.

using GateControl.Core.ViewModels;
using GateControl.Motion;
using GateControl.Pages;
using GateControl.Platform;

namespace GateControl;

public partial class App : Application
{
    private readonly IServiceProvider _services;

    public App(IServiceProvider services)
    {
        _services = services;
        InitializeComponent();
        AppearanceService.Apply(services.GetRequiredService<AppearanceService>().Current);
        var host = services.GetRequiredService<SessionHost>();
        host.SignedOut += (_, _) => MainThread.BeginInvokeOnMainThread(() =>
        {
            if (Windows.FirstOrDefault() is { } w)
            {
                w.Page = _services.GetRequiredService<SignInPage>();
            }
        });
    }

    protected override Window CreateWindow(IActivationState? activationState)
    {
        var window = new Window(new LaunchPage(_services)) { Title = "Gate Control" };
        // The OS motion setting may change while we're in the background.
        window.Resumed += (_, _) => MotionSettings.Refresh();
        window.MinimumWidth = 380;
        window.MinimumHeight = 600;
        return window;
    }
}
