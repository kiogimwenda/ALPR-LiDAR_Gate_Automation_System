// LaunchPage.cs — shown while a saved session is checked.

using GateControl.Core.Services;
using GateControl.Core.ViewModels;

namespace GateControl.Pages;

public sealed class LaunchPage : ContentPage
{
    private readonly IServiceProvider _services;

    public LaunchPage(IServiceProvider services)
    {
        _services = services;
        Content = new VerticalStackLayout
        {
            VerticalOptions = LayoutOptions.Center,
            Spacing = 16,
            Children =
            {
                new Image { Source = "brand_gate.png", HeightRequest = 96, WidthRequest = 96 },
                new ActivityIndicator { IsRunning = true, HorizontalOptions = LayoutOptions.Center },
            },
        };
    }

    protected override async void OnAppearing()
    {
        base.OnAppearing();
        var host = _services.GetRequiredService<SessionHost>();
        var nav = _services.GetRequiredService<INavigator>();
        if (await host.TryResumeAsync())
        {
            await nav.ShowShellAsync();
        }
        else
        {
            await nav.ShowSignInAsync();
        }
    }
}
