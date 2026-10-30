// AlertsViewModel.cs — blocklisted plates, swapped plates, faults, failed commands.

using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using GateControl.Core.Formatting;
using GateControl.Core.Models;
using GateControl.Core.Services;

namespace GateControl.Core.ViewModels;

public sealed class AlertRow : IEquatable<AlertRow>
{
    public bool Equals(AlertRow? o) => o is not null && Id == o.Id && IsAcknowledged == o.IsAcknowledged &&
        Message == o.Message && AcknowledgedText == o.AcknowledgedText;

    public override bool Equals(object? obj) => Equals(obj as AlertRow);

    public override int GetHashCode() => HashCode.Combine(Id, IsAcknowledged);

    public AlertRow(Alert a)
    {
        Id = a.Id;
        Title = Display.AlertKind(a.Kind);
        Message = a.Message;
        Severity = Display.Severity(a.Severity);
        Tone = a.IsAcknowledged ? Tone.Neutral : Display.SeverityTone(a.Severity);
        Plate = a.Plate;
        Gate = a.GateId;
        Raised = a.Raised?.Utc ?? DateTimeOffset.UtcNow;
        IsAcknowledged = a.IsAcknowledged;
        AcknowledgedText = a.IsAcknowledged
            ? $"Cleared by {a.AcknowledgedBy}" : "";
    }

    public long Id { get; }
    public string Title { get; }
    public string Message { get; }
    public string Severity { get; }
    public Tone Tone { get; }
    public string Plate { get; }
    public string Gate { get; }
    public DateTimeOffset Raised { get; }
    public bool IsAcknowledged { get; }
    public string AcknowledgedText { get; }
    public bool HasPlate => Plate.Length > 0;
    public string TimeText => Raised.ToLocalTime().ToString("g", System.Globalization.CultureInfo.CurrentCulture);
}

public sealed partial class AlertsViewModel : ViewModelBase
{
    private readonly Session _session;
    private readonly IHaptics _haptics;
    private readonly INavigator _nav;
    private readonly IUiDispatcher _ui;

    public AlertsViewModel(Session session, IHaptics haptics, INavigator nav, IUiDispatcher ui)
    {
        _session = session;
        _haptics = haptics;
        _nav = nav;
        _ui = ui;
        // New alerts are derived server-side from these frames; refresh the
        // badge when one arrives rather than polling.
        session.Feed.FrameReceived += (_, f) =>
        {
            if (f.Type == "fault" ||
                (f.Type == "decision" && f.Decision is { } d &&
                 (d.DenyReason == "BLOCKLISTED" || d.Verdict == "MANUAL_REVIEW")) ||
                (f.Type == "ack" && f.Ack is { Completed: true, Success: false }))
            {
                _ui.Post(() => _ = RefreshUnreadAsync());
            }
        };
    }

    public ObservableCollection<AlertRow> Items { get; } = [];
    public bool CanAcknowledge => _session.CanOperate;

    [ObservableProperty] private bool _unreadOnly = true;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(CanClearAll))]
    private long _unread;

    public bool CanClearAll => CanAcknowledge && Unread > 0;

    partial void OnUnreadOnlyChanged(bool value) => _ = LoadAsync();

    public string EmptyText => UnreadOnly ? "All clear. No alerts need attention." : "No alerts yet.";

    [RelayCommand]
    public async Task LoadAsync()
    {
        await RunAsync(async () =>
        {
            var page = await _session.Api.AlertsAsync(UnreadOnly, 200, 0);
            Items.ReplaceWith(page.Items.Select(a => new AlertRow(a)));
            Unread = page.Unread;
            OnPropertyChanged(nameof(EmptyText));
        });
    }

    public async Task RefreshUnreadAsync()
    {
        try
        {
            var page = await _session.Api.AlertsAsync(true, 1, 0);
            Unread = page.Unread;
        }
        catch (Exception ex) when (ex is Api.ApiException or HttpRequestException)
        {
        }
    }

    [RelayCommand]
    private async Task AcknowledgeAsync(AlertRow? row)
    {
        if (row is null || row.IsAcknowledged || !CanAcknowledge)
        {
            return;
        }
        if (await RunAsync(() => _session.Api.AcknowledgeAsync(row.Id), showBusy: false))
        {
            _haptics.Play(HapticKind.Success);
            if (UnreadOnly)
            {
                Items.Remove(row);
            }
            Unread = Math.Max(0, Unread - 1);
            if (!UnreadOnly)
            {
                await LoadAsync();
            }
        }
    }

    [RelayCommand]
    private async Task AcknowledgeAllAsync()
    {
        if (!CanAcknowledge || Unread == 0)
        {
            return;
        }
        if (await RunAsync(() => _session.Api.AcknowledgeAllAsync()))
        {
            _haptics.Play(HapticKind.Success);
            await LoadAsync();
        }
    }

    [RelayCommand]
    private Task ShowPlateAsync(AlertRow? row) =>
        row is { HasPlate: true } ? _nav.ShowPlateAsync(row.Plate) : Task.CompletedTask;
}
