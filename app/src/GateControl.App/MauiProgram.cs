// MauiProgram.cs — composition root.

using GateControl.Core.Services;
using GateControl.Core.ViewModels;
using GateControl.Motion;
using GateControl.Pages;
using GateControl.Platform;
using Microsoft.Extensions.Logging;
using Microsoft.Maui.Handlers;

namespace GateControl;

public static class MauiProgram
{
    public static MauiApp CreateMauiApp()
    {
        var builder = MauiApp.CreateBuilder();
        builder.UseMauiApp<App>();

        // Every Button responds on touch-down (Motion.cs, principle 01).
        ButtonHandler.Mapper.AppendToMapping("PressFeedback", (_, view) =>
        {
            if (view is Button b)
            {
                Pressable.Wire(b);
            }
        });

        // Text fields sit inside grouped cards (the iOS Settings look): the
        // platform's own field chrome — iOS rounded border, Android
        // underline, WinUI border and focus bar — would draw a second box.
        EntryHandler.Mapper.AppendToMapping("Borderless", (handler, _) =>
        {
#if IOS || MACCATALYST
            handler.PlatformView.BorderStyle = UIKit.UITextBorderStyle.None;
#elif ANDROID
            handler.PlatformView.Background = null;
#elif WINDOWS
            handler.PlatformView.BorderThickness = new Microsoft.UI.Xaml.Thickness(0);
            handler.PlatformView.Background = Transparent();
#endif
        });
        PickerHandler.Mapper.AppendToMapping("Borderless", (handler, _) =>
        {
#if ANDROID
            handler.PlatformView.Background = null;
#elif WINDOWS
            handler.PlatformView.BorderThickness = new Microsoft.UI.Xaml.Thickness(0);
            handler.PlatformView.Background = Transparent();
#endif
        });
        DatePickerHandler.Mapper.AppendToMapping("Borderless", (handler, _) =>
        {
#if ANDROID
            handler.PlatformView.Background = null;
#elif WINDOWS
            handler.PlatformView.BorderThickness = new Microsoft.UI.Xaml.Thickness(0);
            handler.PlatformView.Background = Transparent();
#endif
        });

        var s = builder.Services;
        s.AddSingleton(TimeProvider.System);
        s.AddSingleton<IUiDispatcher, MauiUiDispatcher>();
        s.AddSingleton<ISessionStore, SessionStore>();
        s.AddSingleton<IHaptics, Haptics>();
        s.AddSingleton<IDialogs, Dialogs>();
        s.AddSingleton<IFileShare, ShareSheet>();
        s.AddSingleton<AppearanceService>();
        s.AddSingleton<IAppearanceService>(sp => sp.GetRequiredService<AppearanceService>());
        s.AddSingleton<INavigator, Navigator>();
        s.AddSingleton(sp => new SessionHost(sp.GetRequiredService<ISessionStore>()));

        s.AddTransient<SignInViewModel>();
        s.AddTransient<SignInPage>();

        // Screens that need a signed-in session are built per sign-in.
        static Session Current(IServiceProvider sp) =>
            sp.GetRequiredService<SessionHost>().Current
            ?? throw new InvalidOperationException("not signed in");
        s.AddTransient(sp => Current(sp));
        s.AddTransient<LiveViewModel>();
        s.AddTransient<DashboardViewModel>();
        s.AddTransient<ReportsViewModel>();
        s.AddTransient<AllowlistViewModel>();
        s.AddTransient<AlertsViewModel>();
        s.AddTransient<SettingsViewModel>();
        s.AddTransient<UsersViewModel>();
        s.AddTransient<GatesViewModel>();
        s.AddTransient<DataSettingsViewModel>();
        s.AddTransient<LivePage>();
        s.AddTransient<DashboardPage>();
        s.AddTransient<ReportsPage>();
        s.AddTransient<VehiclesPage>();
        s.AddTransient<AlertsPage>();
        s.AddTransient<SettingsPage>();
        s.AddTransient<UsersPage>();
        s.AddTransient<GatesPage>();
        s.AddTransient<DataPage>();
        s.AddTransient<AppShell>();

#if DEBUG
        builder.Logging.AddDebug();
#endif
        return builder.Build();
    }

#if WINDOWS
    private static Microsoft.UI.Xaml.Media.SolidColorBrush Transparent() =>
        new(Microsoft.UI.Colors.Transparent);
#endif
}
