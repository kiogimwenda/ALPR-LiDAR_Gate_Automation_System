// AllowlistViewModel.cs — registered vehicles (residents, staff, visitors).

using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using GateControl.Core.Formatting;
using GateControl.Core.Models;
using GateControl.Core.Services;

namespace GateControl.Core.ViewModels;

// Rows compare by what they show, so a refresh that brings the same
// vehicles back leaves the bound list alone (ReplaceWith).
public sealed class AllowlistRow : IEquatable<AllowlistRow>
{
    public bool Equals(AllowlistRow? o) => o is not null &&
        (Plate, Owner, Classes, Restricted, Entry.Notes) == (o.Plate, o.Owner, o.Classes, o.Restricted, o.Entry.Notes);

    public override bool Equals(object? obj) => Equals(obj as AllowlistRow);

    public override int GetHashCode() => HashCode.Combine(Plate, Owner, Classes, Restricted);

    public AllowlistRow(AllowlistEntry e)
    {
        Entry = e;
        Plate = Display.Plate(e.Plate);
        Owner = string.Join(" · ", new[] { e.OwnerName, e.OwnerUnit }.Where(s => s.Length > 0));
        Classes = e.AllowedClasses.Count == 0
            ? "Any vehicle"
            : string.Join(", ", e.AllowedClasses.Select(Display.VehicleClass));
        Restricted = e.TimeWindows.Count > 0 ? "Set hours" : "";
        if (e.ValidUntil is { } until)
        {
            Restricted = (Restricted.Length > 0 ? Restricted + " · " : "") +
                         "until " + until.Local.ToString("d MMM yyyy", System.Globalization.CultureInfo.CurrentCulture);
        }
    }

    public AllowlistEntry Entry { get; }
    public string Plate { get; }
    public string Owner { get; }
    public string Classes { get; }
    public string Restricted { get; }
    public bool HasRestriction => Restricted.Length > 0;
}

public sealed partial class AllowlistViewModel : ViewModelBase
{
    private readonly Session _session;
    private readonly IDialogs _dialogs;
    private readonly IHaptics _haptics;
    private List<AllowlistRow> _all = [];

    public AllowlistViewModel(Session session, IDialogs dialogs, IHaptics haptics)
    {
        _session = session;
        _dialogs = dialogs;
        _haptics = haptics;
    }

    public ObservableCollection<AllowlistRow> Items { get; } = [];

    [ObservableProperty] private string _query = "";
    [ObservableProperty] private PlateEditorViewModel? _editor;

    public bool CanEdit => _session.CanOperate;
    public string CountText => _all.Count == 1 ? "1 vehicle" : $"{Display.Count(_all.Count)} vehicles";

    partial void OnQueryChanged(string value) => ApplyFilter();

    [RelayCommand]
    public async Task LoadAsync()
    {
        await RunAsync(async () =>
        {
            var all = new List<AllowlistEntry>();
            var token = "";
            do
            {
                var page = await _session.Api.AllowlistAsync(500, token);
                all.AddRange(page.Entries);
                token = page.NextPageToken;
            } while (token.Length > 0 && all.Count < 20_000);
            _all = all.OrderBy(e => e.Plate, StringComparer.Ordinal).Select(e => new AllowlistRow(e)).ToList();
            OnPropertyChanged(nameof(CountText));
            ApplyFilter();
        });
    }

    private void ApplyFilter()
    {
        var q = new string(Query.Where(char.IsLetterOrDigit).ToArray()).ToUpperInvariant();
        var text = Query.Trim();
        Items.ReplaceWith(_all.Where(r =>
            q.Length == 0 ||
            r.Entry.Plate.Contains(q, StringComparison.OrdinalIgnoreCase) ||
            r.Entry.OwnerName.Contains(text, StringComparison.OrdinalIgnoreCase) ||
            r.Entry.OwnerUnit.Contains(text, StringComparison.OrdinalIgnoreCase)));
    }

    [RelayCommand]
    private void Add()
    {
        if (CanEdit)
        {
            Editor = new PlateEditorViewModel(null, SaveAsync, () => Editor = null);
        }
    }

    [RelayCommand]
    private void Edit(AllowlistRow? row)
    {
        if (CanEdit && row is not null)
        {
            Editor = new PlateEditorViewModel(row.Entry, SaveAsync, () => Editor = null);
        }
    }

    [RelayCommand]
    private async Task RemoveAsync(AllowlistRow? row)
    {
        if (!CanEdit || row is null)
        {
            return;
        }
        if (!await _dialogs.ConfirmAsync($"Remove {row.Plate}?",
                "This vehicle will no longer open the gate.", "Remove", destructive: true))
        {
            return;
        }
        if (await RunAsync(() => _session.Api.RemoveAllowlistAsync(row.Entry.Plate)))
        {
            _haptics.Play(HapticKind.Success);
            _all.Remove(row);
            Items.Remove(row);
            OnPropertyChanged(nameof(CountText));
        }
    }

    private async Task<string?> SaveAsync(AllowlistUpsert upsert)
    {
        try
        {
            await _session.Api.UpsertAllowlistAsync(upsert);
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

public sealed partial class ClassToggle : ObservableObject
{
    public ClassToggle(string code, bool selected)
    {
        Code = code;
        Label = Display.VehicleClass(code);
        _selected = selected;
    }

    public string Code { get; }
    public string Label { get; }
    [ObservableProperty] private bool _selected;
}

public sealed partial class PlateEditorViewModel : ViewModelBase
{
    private readonly Func<AllowlistUpsert, Task<string?>> _save;
    private readonly Action _close;
    private readonly AllowlistEntry? _original;

    public PlateEditorViewModel(AllowlistEntry? existing, Func<AllowlistUpsert, Task<string?>> save, Action close)
    {
        _original = existing;
        _save = save;
        _close = close;
        _plate = existing?.Plate ?? "";
        _ownerName = existing?.OwnerName ?? "";
        _ownerUnit = existing?.OwnerUnit ?? "";
        _notes = existing?.Notes ?? "";
        _hasExpiry = existing?.ValidUntil is not null;
        _expires = existing?.ValidUntil?.Local.Date ?? DateTime.Today.AddDays(1);
        Classes = new ObservableCollection<ClassToggle>(Display.RestrictableClasses.Select(c =>
            new ClassToggle(c, existing?.AllowedClasses.Contains(c) == true)));
    }

    public bool IsNew => _original is null;
    public string Title => IsNew ? "Add vehicle" : "Edit " + Display.Plate(_original!.Plate);
    public ObservableCollection<ClassToggle> Classes { get; }

    [ObservableProperty]
    [NotifyCanExecuteChangedFor(nameof(SaveCommand))]
    private string _plate;

    [ObservableProperty] private string _ownerName;
    [ObservableProperty] private string _ownerUnit;
    [ObservableProperty] private string _notes;
    [ObservableProperty] private bool _hasExpiry;
    [ObservableProperty] private DateTime _expires;

    // Kenyan plates: 5–8 letters and digits once spaces/dashes are removed.
    public static string Normalise(string plate) =>
        new string(plate.Where(char.IsLetterOrDigit).ToArray()).ToUpperInvariant();

    private bool CanSave() => Normalise(Plate).Length is >= 3 and <= 10 && !IsBusy;

    [RelayCommand(CanExecute = nameof(CanSave))]
    private async Task SaveAsync()
    {
        IsBusy = true;
        Error = "";
        var upsert = new AllowlistUpsert
        {
            Plate = Normalise(Plate),
            OwnerName = OwnerName.Trim(),
            OwnerUnit = OwnerUnit.Trim(),
            Notes = Notes.Trim(),
            AllowedClasses = Classes.Where(c => c.Selected).Select(c => c.Code).ToList(),
            TimeWindows = _original?.TimeWindows ?? [],
            ValidUntilMs = HasExpiry
                ? new DateTimeOffset(Expires.Date.AddDays(1).AddSeconds(-1)).ToUnixTimeMilliseconds()
                : null,
        };
        var error = await _save(upsert);
        IsBusy = false;
        Error = error ?? "";
    }

    [RelayCommand]
    private void Cancel() => _close();
}
