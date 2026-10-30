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
#if WINDOWS
        // The caption follows the app's theme tokens, not the OS theme:
        // with the app in light mode on a dark OS the title text was white
        // on light grey.
        // Set explicitly, not by AppThemeBinding: the caption is outside the
        // theme reveal's snapshot, so it would switch the moment the theme is
        // applied under the snapshot (during the press), ahead of the wipe.
        // RootPage switches it on the wipe's first frame; other theme changes
        // (the OS theme with Appearance = System) switch it at once.
        var titleBar = new TitleBar { Title = "Gate Control", HeightRequest = 32 };
        _caption = titleBar;
        ApplyCaption(Current!.RequestedTheme);
        Current.RequestedThemeChanged += (_, e) =>
        {
            if (!Motion.ThemeReveal.Busy)
            {
                ApplyCaption(e.RequestedTheme);
            }
        };
        window.TitleBar = titleBar;
#endif
#if DEBUG || GC_HARNESS
        Platform.Automation.Start(window, _services);
#endif
        return window;
    }

#if WINDOWS
    private static TitleBar? _caption;
#endif

    // The window caption in `theme` (Windows; no-op elsewhere).
    public static void ApplyCaption(AppTheme theme)
    {
#if WINDOWS
        if (_caption is null || Current is null)
        {
            return;
        }
        var dark = theme == AppTheme.Dark;
        var res = Current.Resources;
        _caption.BackgroundColor = (Color)res[dark ? "OuterDark" : "OuterLight"];
        _caption.ForegroundColor = (Color)res[dark ? "LabelDark" : "LabelLight"];
#endif
    }
}
