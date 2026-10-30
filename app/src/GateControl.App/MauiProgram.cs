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
        // No GC latency mode: the gen-2 collections that cost 40–95 ms frames
        // were induced (WinUI's memory check, per-frame WinUI objects), which
        // SustainedLowLatency does not prevent; they are fixed at the source
        // (RootPage.SettleXamlCleanup, WinTransform, BlobClip, ChartView.Text).
        var builder = MauiApp.CreateBuilder();
        builder.UseMauiApp<App>();
        // Inter with tabular figures frozen in (Resources/Fonts/OFL.txt):
        // counting numbers keep their width, so they never jitter (spec §4).
        builder.ConfigureFonts(fonts =>
        {
            fonts.AddFont("InterGC-Regular.ttf", "Inter");
            fonts.AddFont("InterGC-Medium.ttf", "InterMedium");
            fonts.AddFont("InterGC-SemiBold.ttf", "InterSemiBold");
            fonts.AddFont("InterGC-Bold.ttf", "InterBold");
        });

#if WINDOWS
        // Transforms updated in place, not rebuilt per frame (WinTransform.cs).
        WinTransform.Register();
        // Parked tabs unreachable by focus and UI Automation (ParkHost.cs).
        builder.ConfigureMauiHandlers(h => h.AddHandler<Controls.ParkHost, Controls.ParkHostHandler>());
#endif

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
        s.AddTransient<RootPage>();

#if DEBUG
        builder.Logging.AddDebug();
#endif
        builder.Logging.AddProvider(new FileLoggerProvider());
        var app = builder.Build();
        AppLog.Init(app.Services.GetRequiredService<ILoggerFactory>());
        return app;
    }

#if WINDOWS
    private static Microsoft.UI.Xaml.Media.SolidColorBrush Transparent() =>
        new(Microsoft.UI.Colors.Transparent);

#endif
}
