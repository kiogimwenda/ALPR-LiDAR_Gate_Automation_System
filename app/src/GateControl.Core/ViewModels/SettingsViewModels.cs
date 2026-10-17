// SettingsViewModels.cs — account, appearance, and the admin screens.

using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using GateControl.Core.Formatting;
using GateControl.Core.Models;
using GateControl.Core.Services;

namespace GateControl.Core.ViewModels;

public enum Appearance
{
    System,
    Light,
    Dark,
}

public interface IAppearanceService
{
    Appearance Current { get; set; }
}

public sealed partial class SettingsViewModel : ViewModelBase
{
    private readonly Session _session;
    private readonly SessionHost _host;
    private readonly IAppearanceService _appearance;
    private readonly IDialogs _dialogs;
    private readonly IHaptics _haptics;

    public SettingsViewModel(Session session, SessionHost host, IAppearanceService appearance,
                             IDialogs dialogs, IHaptics haptics)
    {
        _session = session;
        _host = host;
        _appearance = appearance;
        _dialogs = dialogs;
        _haptics = haptics;
        _appearanceChoice = appearance.Current;
    }

    public string DisplayName => _session.DisplayName;
    public string RoleText => Display.Role(_session.Me.Role);
    public string Server => _session.Api.BaseUri.ToString().TrimEnd('/');
    public bool IsAdmin => _session.IsAdmin;
    public bool AuthRequired => _session.Me.AuthRequired;
    public string Initials
    {
        get
        {
            var parts = DisplayName.Split(' ', StringSplitOptions.RemoveEmptyEntries);
            return parts.Length switch
            {
                0 => "?",
                1 => parts[0][..1].ToUpperInvariant(),
                _ => (parts[0][..1] + parts[^1][..1]).ToUpperInvariant(),
            };
        }
    }

    [ObservableProperty] private Appearance _appearanceChoice;

    partial void OnAppearanceChoiceChanged(Appearance value) => _appearance.Current = value;

    // ---- change password ----------------------------------------------------
    [ObservableProperty] private string _currentPassword = "";
    [ObservableProperty] private string _newPassword = "";
    [ObservableProperty] private string _confirmPassword = "";
    [ObservableProperty] private string _passwordMessage = "";

    [RelayCommand]
    private async Task ChangePasswordAsync()
    {
        PasswordMessage = "";
        if (NewPassword.Length < 10)
        {
            PasswordMessage = "Use at least 10 characters.";
            return;
        }
        if (NewPassword != ConfirmPassword)
        {
            PasswordMessage = "The new passwords don't match.";
            return;
        }
        if (await RunAsync(() => _session.Api.ChangePasswordAsync(CurrentPassword, NewPassword)))
        {
            _haptics.Play(HapticKind.Success);
            CurrentPassword = NewPassword = ConfirmPassword = "";
            PasswordMessage = "Password changed.";
        }
        else
        {
            PasswordMessage = Error;
        }
    }

    [RelayCommand]
    private async Task SignOutAsync()
    {
        if (await _dialogs.ConfirmAsync("Sign out?", "You'll need your password to sign back in.", "Sign out"))
        {
            await _host.SignOutAsync();
        }
    }
}

// ---- users (admin) ---------------------------------------------------------------

public sealed partial class UsersViewModel : ViewModelBase
{
    private readonly Session _session;
    private readonly IDialogs _dialogs;
    private readonly IHaptics _haptics;

    public UsersViewModel(Session session, IDialogs dialogs, IHaptics haptics)
    {
        _session = session;
        _dialogs = dialogs;
        _haptics = haptics;
    }

    public ObservableCollection<UserAccount> Items { get; } = [];
    [ObservableProperty] private UserEditorViewModel? _editor;

    [RelayCommand]
    public async Task LoadAsync()
    {
        await RunAsync(async () => Items.ReplaceWith(await _session.Api.UsersAsync()));
    }

    [RelayCommand]
    private void Add() => Editor = new UserEditorViewModel(null, SaveAsync, () => Editor = null);

    [RelayCommand]
    private void Edit(UserAccount? u)
    {
        if (u is not null)
        {
            Editor = new UserEditorViewModel(u, SaveAsync, () => Editor = null);
        }
    }

    [RelayCommand]
    private async Task DeleteAsync(UserAccount? u)
    {
        if (u is null)
        {
            return;
        }
        if (!await _dialogs.ConfirmAsync($"Delete {u.Username}?",
                "They will be signed out and can't sign in again.", "Delete", destructive: true))
        {
            return;
        }
        if (await RunAsync(() => _session.Api.DeleteUserAsync(u.Id)))
        {
            _haptics.Play(HapticKind.Success);
            Items.Remove(u);
        }
    }

    private async Task<string?> SaveAsync(UserAccount u, string? password)
    {
        try
        {
            await _session.Api.SaveUserAsync(u, password);
        }
        catch (Api.ApiException ex)
        {
            _haptics.Play(HapticKind.Error);
            return ex.Message;
        }
        _haptics.Play(HapticKind.Success);
        Editor = null;
        await LoadAsync();
        return null;
    }
}

public sealed partial class UserEditorViewModel : ViewModelBase
{
    private readonly UserAccount? _original;
    private readonly Func<UserAccount, string?, Task<string?>> _save;
    private readonly Action _close;

    public UserEditorViewModel(UserAccount? existing, Func<UserAccount, string?, Task<string?>> save, Action close)
    {
        _original = existing;
        _save = save;
        _close = close;
        _username = existing?.Username ?? "";
        _fullName = existing?.FullName ?? "";
        _email = existing?.Email ?? "";
        _role = Roles.FirstOrDefault(r => r.Value == existing?.Role) ?? Roles[1];
        _active = existing?.Active ?? true;
    }

    public static IReadOnlyList<Choice> Roles { get; } =
    [
        new("Viewer — sees reports", "viewer"),
        new("Operator — runs gates and the vehicle list", "operator"),
        new("Administrator — everything, including users", "admin"),
    ];

    public bool IsNew => _original is null;
    public string Title => IsNew ? "New user" : _original!.Username;
    public string PasswordHint => IsNew ? "At least 10 characters" : "Leave empty to keep the current password";

    [ObservableProperty]
    [NotifyCanExecuteChangedFor(nameof(SaveCommand))]
    private string _username;

    [ObservableProperty] private string _fullName;
    [ObservableProperty] private string _email;
    [ObservableProperty] private Choice _role;
    [ObservableProperty] private bool _active;

    [ObservableProperty]
    [NotifyCanExecuteChangedFor(nameof(SaveCommand))]
    private string _password = "";

    private bool CanSave() =>
        Username.Trim().Length > 0 && (!IsNew || Password.Length >= 10) &&
        (Password.Length == 0 || Password.Length >= 10) && !IsBusy;

    [RelayCommand(CanExecute = nameof(CanSave))]
    private async Task SaveAsync()
    {
        IsBusy = true;
        Error = "";
        var user = new UserAccount
        {
            Id = _original?.Id ?? 0,
            Username = Username.Trim(),
            FullName = FullName.Trim(),
            Email = Email.Trim(),
            Role = Role.Value,
            Active = Active,
        };
        var error = await _save(user, Password.Length > 0 ? Password : null);
        IsBusy = false;
        Error = error ?? "";
    }

    [RelayCommand]
    private void Cancel() => _close();
}

// ---- gates (admin) ---------------------------------------------------------------

public sealed partial class GateConfigRow : ObservableObject
{
    public GateConfigRow(GateInfo g)
    {
        GateId = g.GateId;
        _name = g.Name;
        _location = g.Location;
        _direction = GatesViewModel.Directions.FirstOrDefault(d => d.Value == g.Direction)
                     ?? GatesViewModel.Directions[2];
        Online = g.Online;
        Status = g.Online ? "Online" : g.LastSeen is { } s ? "Last seen " + s.Local.ToString("g") : "Never seen";
    }

    public string GateId { get; }
    public bool Online { get; }
    public string Status { get; }
    [ObservableProperty] private string _name;
    [ObservableProperty] private string _location;
    [ObservableProperty] private Choice _direction;
    [ObservableProperty] private bool _dirty;

    partial void OnNameChanged(string value) => Dirty = true;
    partial void OnLocationChanged(string value) => Dirty = true;
    partial void OnDirectionChanged(Choice value) => Dirty = true;
}

public sealed partial class GatesViewModel : ViewModelBase
{
    private readonly Session _session;
    private readonly IHaptics _haptics;

    public GatesViewModel(Session session, IHaptics haptics)
    {
        _session = session;
        _haptics = haptics;
    }

    public static IReadOnlyList<Choice> Directions { get; } =
        [new("Entry", "entry"), new("Exit", "exit"), new("Entry & exit", "both")];

    public ObservableCollection<GateConfigRow> Items { get; } = [];

    [RelayCommand]
    public async Task LoadAsync()
    {
        await RunAsync(async () =>
        {
            var gates = await _session.Api.GatesAsync();
            Items.ReplaceWith(gates.Select(g => new GateConfigRow(g)));
            foreach (var row in Items)
            {
                row.Dirty = false;
            }
        });
    }

    [RelayCommand]
    private async Task SaveAsync(GateConfigRow? row)
    {
        if (row is null)
        {
            return;
        }
        if (await RunAsync(() => _session.Api.ConfigureGateAsync(row.GateId, row.Name.Trim(),
                                                                 row.Location.Trim(), row.Direction.Value)))
        {
            row.Dirty = false;
            _haptics.Play(HapticKind.Success);
        }
    }
}

// ---- data (admin) ------------------------------------------------------------------

public sealed partial class DataSettingsViewModel : ViewModelBase
{
    private readonly Session _session;
    private readonly IHaptics _haptics;

    public DataSettingsViewModel(Session session, IHaptics haptics)
    {
        _session = session;
        _haptics = haptics;
    }

    [ObservableProperty] private double _retentionDays = 90;
    [ObservableProperty] private double _sessionTimeoutHours = 12;
    [ObservableProperty] private string _saved = "";

    [RelayCommand]
    public async Task LoadAsync()
    {
        await RunAsync(async () =>
        {
            var s = await _session.Api.SettingsAsync();
            RetentionDays = s.RetentionDays;
            SessionTimeoutHours = s.SessionTimeoutMinutes / 60.0;
        });
    }

    [RelayCommand]
    private async Task SaveAsync()
    {
        Saved = "";
        var settings = new SiteSettings
        {
            RetentionDays = (int)Math.Round(RetentionDays),
            SessionTimeoutMinutes = (int)Math.Round(SessionTimeoutHours * 60),
        };
        if (await RunAsync(() => _session.Api.SaveSettingsAsync(settings)))
        {
            _haptics.Play(HapticKind.Success);
            Saved = "Saved.";
        }
    }
}
