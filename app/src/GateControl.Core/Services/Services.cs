// Services.cs — what the view models need from the platform, as interfaces.
//
// The MAUI app implements these (Platform/*.cs); tests substitute fakes.

using GateControl.Core.Api;
using GateControl.Core.Models;

namespace GateControl.Core.Services;

// Runs work on the UI thread (frames arrive on the WebSocket thread).
public interface IUiDispatcher
{
    void Post(Action action);
}

// Device-local persistence. Tokens go to the platform keychain / keystore.
public interface ISessionStore
{
    string? ServerUrl { get; set; }
    string? LastUsername { get; set; }
    Task<string?> GetTokenAsync();
    Task SetTokenAsync(string? token);
}

public enum HapticKind
{
    Tap,        // a press registered
    Success,    // gate opened, alert cleared
    Warning,    // denied / held for review
    Error,      // command failed, blocklisted plate
}

// Multimodal feedback: fired in the same frame as the motion it accompanies.
public interface IHaptics
{
    void Play(HapticKind kind);
}

public interface IDialogs
{
    Task AlertAsync(string title, string message);
    Task<bool> ConfirmAsync(string title, string message, string accept, bool destructive = false);
}

// Hands a file to the platform's share sheet / save dialog.
public interface IFileShare
{
    Task ShareAsync(string title, string fileName, Stream content, string contentType);
}

public interface INavigator
{
    Task ShowPlateAsync(string plate);
    Task ShowSignInAsync();
    Task ShowShellAsync();
}

// The signed-in session shared by every screen.
public sealed class Session
{
    public Session(GateApi api, LiveFeed feed, Me me)
    {
        Api = api;
        Feed = feed;
        Me = me;
    }

    public GateApi Api { get; }
    public LiveFeed Feed { get; }
    public Me Me { get; private set; }

    public bool CanOperate => Me.Role is "operator" or "admin";
    public bool IsAdmin => Me.Role == "admin";
    public string DisplayName => string.IsNullOrWhiteSpace(Me.FullName) ? Me.User : Me.FullName;
}
