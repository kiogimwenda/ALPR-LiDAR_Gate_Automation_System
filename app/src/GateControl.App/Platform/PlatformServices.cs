// PlatformServices.cs — Core's service interfaces, implemented with MAUI.

using GateControl.Core.Services;
using GateControl.Core.ViewModels;
using GateControl.Pages;

namespace GateControl.Platform;

public sealed class MauiUiDispatcher : IUiDispatcher
{
    public void Post(Action action)
    {
        if (MainThread.IsMainThread)
        {
            action();
        }
        else
        {
            MainThread.BeginInvokeOnMainThread(action);
        }
    }
}

// Server address and username in Preferences; the session token in the
// platform keychain / keystore (SecureStorage), never in plain prefs.
public sealed class SessionStore : ISessionStore
{
    private const string TokenKey = "session_token";

    public string? ServerUrl
    {
        get => Preferences.Default.Get<string?>("server_url", null);
        set => Preferences.Default.Set("server_url", value);
    }

    public string? LastUsername
    {
        get => Preferences.Default.Get<string?>("last_username", null);
        set => Preferences.Default.Set("last_username", value);
    }

    public async Task<string?> GetTokenAsync()
    {
        try
        {
            return await SecureStorage.Default.GetAsync(TokenKey);
        }
        catch (Exception)
        {
            // Keychain unavailable (e.g. reinstall on iOS): treat as signed out.
            return null;
        }
    }

    public async Task SetTokenAsync(string? token)
    {
        try
        {
            if (string.IsNullOrEmpty(token))
            {
                SecureStorage.Default.Remove(TokenKey);
            }
            else
            {
                await SecureStorage.Default.SetAsync(TokenKey, token);
            }
        }
        catch (Exception)
        {
            // Not persisting a token only means signing in again next launch.
        }
    }
}

// Multimodal feedback: called in the same frame as the motion it pairs with.
public sealed class Haptics : IHaptics
{
    public void Play(HapticKind kind)
    {
        // Logged, so the feedback path is visible on platforms without a
        // haptic engine (Windows, Mac) and in field logs.
        Microsoft.Extensions.Logging.LoggerExtensions.LogInformation(AppLog.For("Haptics"), "haptic {Kind}", kind);
        try
        {
#if IOS
            switch (kind)
            {
                case HapticKind.Tap:
                    // MAUI picks the right impact API for the iOS version.
                    HapticFeedback.Default.Perform(HapticFeedbackType.Click);
                    break;
                default:
                    var type = kind switch
                    {
                        HapticKind.Success => UIKit.UINotificationFeedbackType.Success,
                        HapticKind.Warning => UIKit.UINotificationFeedbackType.Warning,
                        _ => UIKit.UINotificationFeedbackType.Error,
                    };
                    new UIKit.UINotificationFeedbackGenerator().NotificationOccurred(type);
                    break;
            }
#elif ANDROID
            var view = Microsoft.Maui.ApplicationModel.Platform.CurrentActivity?.Window?.DecorView;
            if (view is not null && OperatingSystem.IsAndroidVersionAtLeast(30))
            {
                view.PerformHapticFeedback(kind switch
                {
                    HapticKind.Success => Android.Views.FeedbackConstants.Confirm,
                    HapticKind.Tap => Android.Views.FeedbackConstants.KeyboardTap,
                    _ => Android.Views.FeedbackConstants.Reject,
                });
            }
            else
            {
                HapticFeedback.Default.Perform(kind == HapticKind.Tap
                    ? HapticFeedbackType.Click : HapticFeedbackType.LongPress);
            }
#else
            // Mac Catalyst / Windows: no haptic engine for app feedback.
            _ = kind;
#endif
        }
        catch (Exception)
        {
            // Feedback is never worth a crash.
        }
    }
}

public sealed class Dialogs : IDialogs
{
    private static Page? Top => Application.Current?.Windows.FirstOrDefault()?.Page;

    public Task AlertAsync(string title, string message) =>
        Top?.DisplayAlertAsync(title, message, "OK") ?? Task.CompletedTask;

    public async Task<bool> ConfirmAsync(string title, string message, string accept, bool destructive = false)
    {
        if (Top is not { } page)
        {
            return false;
        }
        if (destructive)
        {
            // An action sheet puts the destructive choice in red (iOS) and
            // keeps Cancel the easy, default way out.
            var choice = await page.DisplayActionSheetAsync(message, "Cancel", accept);
            return choice == accept;
        }
        return await page.DisplayAlertAsync(title, message, accept, "Cancel");
    }
}

// CSV exports go through the platform share sheet (save to Files, mail, …).
public sealed class ShareSheet : IFileShare
{
    public async Task ShareAsync(string title, string fileName, Stream content, string contentType)
    {
        var path = Path.Combine(FileSystem.CacheDirectory, fileName);
        await using (var file = File.Create(path))
        {
            await content.CopyToAsync(file);
        }
        await Share.Default.RequestAsync(new ShareFileRequest
        {
            Title = title,
            File = new ShareFile(path, contentType),
        });
    }
}

public sealed class AppearanceService : IAppearanceService
{
    public Appearance Current
    {
        get => (Appearance)Preferences.Default.Get("appearance", (int)Appearance.System);
        set
        {
            Preferences.Default.Set("appearance", (int)value);
            Apply(value);
        }
    }

    // The theme on screen only: no saved preference, no change seen by the
    // appearance setting (a theme press that may still be cancelled).
    public void Preview(Appearance a) => Apply(a);

    public static void Apply(Appearance a)
    {
        if (Application.Current is { } app)
        {
            app.UserAppTheme = a switch
            {
                Appearance.Light => AppTheme.Light,
                Appearance.Dark => AppTheme.Dark,
                _ => AppTheme.Unspecified,
            };
        }
    }
}

public sealed class Navigator(IServiceProvider services) : INavigator
{
    private static Window? MainWindow => Application.Current?.Windows.FirstOrDefault();

    public Task ShowPlateAsync(string plate)
    {
        var host = services.GetRequiredService<SessionHost>();
        if (host.Current is not { } session || MainWindow?.Page is not NavigationPage nav)
        {
            return Task.CompletedTask;
        }
        // Two-pane tabs show it beside the list on wide windows.
        if (nav.CurrentPage is RootPage { CurrentPage: IPlatePaneHost { HasPane: true } pane })
        {
            return pane.ShowPlateAsync(plate);
        }
        var page = new PlateDetailPage(new PlateDetailViewModel(session, plate));
        return nav.PushAsync(page);
    }

    public Task ShowSignInAsync()
    {
        if (MainWindow is { } w)
        {
            w.Page = services.GetRequiredService<SignInPage>();
        }
        return Task.CompletedTask;
    }

    public Task ShowShellAsync()
    {
        if (MainWindow is { } w)
        {
            // The persistent shell, with pushed pages (plate history,
            // account, admin) stacked above it.
            var nav = new NavigationPage(services.GetRequiredService<RootPage>());
            var res = Application.Current!.Resources;
            nav.SetAppThemeColor(NavigationPage.BarBackgroundColorProperty,
                (Color)res["BackgroundLight"], (Color)res["BackgroundDark"]);
            nav.SetAppThemeColor(NavigationPage.BarTextColorProperty,
                (Color)res["LabelLight"], (Color)res["LabelDark"]);
            w.Page = nav;
        }
        return Task.CompletedTask;
    }
}
