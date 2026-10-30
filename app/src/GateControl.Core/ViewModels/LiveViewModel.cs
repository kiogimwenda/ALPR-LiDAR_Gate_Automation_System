// LiveViewModel.cs — gates right now + the decision stream.
//
// The guard's screen: a tile per gate (state, beam, link quality, the
// last thing that happened) with its controls, and the running feed of
// plate decisions. Fed by the WebSocket; /api/status seeds the tiles on
// open so the screen paints before the next telemetry tick.

using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using GateControl.Core.Api;
using GateControl.Core.Formatting;
using GateControl.Core.Models;
using GateControl.Core.Services;

namespace GateControl.Core.ViewModels;

public sealed partial class GateTileViewModel : ObservableObject
{
    private readonly Func<GateTileViewModel, string, Task> _command;

    public GateTileViewModel(string gateId, Func<GateTileViewModel, string, Task> command)
    {
        GateId = gateId;
        _command = command;
        _name = gateId;
    }

    public string GateId { get; }

    [ObservableProperty] private string _name;
    [ObservableProperty] private string _location = "";
    [ObservableProperty] private string _direction = "both";

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(StateText), nameof(StateTone), nameof(IsOpenish), nameof(IsLocked))]
    private string _state = "UNKNOWN";

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(BeamText))]
    private bool _beamClear = true;

    [ObservableProperty] private DateTimeOffset? _lastSeen;
    [ObservableProperty] private string _lastActivity = "No activity yet";
    [ObservableProperty] private string _commandStatus = "";
    // The last command was refused or failed: the status shows as an error.
    [ObservableProperty] private bool _commandFailed;
    [ObservableProperty] private bool _commandPending;
    public string PendingCommandId { get; set; } = "";
    [ObservableProperty] private bool _isStale;
    [ObservableProperty] private string _detail = "";

    public string StateText => Display.GateState(State);
    public Tone StateTone => IsStale ? Tone.Neutral : Display.GateStateTone(State);
    public bool IsOpenish => State is "OPEN" or "OPENING";
    public bool IsLocked => State == "LOCKDOWN";
    public string BeamText => BeamClear ? "Beam clear" : "Beam blocked";
    public string DirectionText => Display.Direction(Direction);

    partial void OnIsStaleChanged(bool value) => OnPropertyChanged(nameof(StateTone));

    public void Apply(Telemetry t, DateTimeOffset seen)
    {
        State = t.State;
        BeamClear = t.BeamClear;
        LastSeen = seen;
        IsStale = false;
        var parts = new List<string>();
        if (t.RssiDbm != 0)
        {
            parts.Add($"{t.RssiDbm} dBm");
        }
        if (t.SupplyVoltage > 0)
        {
            parts.Add($"{t.SupplyVoltage:0.0} V");
        }
        if (t.FwVersion.Length > 0)
        {
            parts.Add($"fw {t.FwVersion}");
        }
        Detail = string.Join(" · ", parts);
    }

    [RelayCommand] private Task OpenAsync() => _command(this, "OPEN_GATE");
    [RelayCommand] private Task CloseAsync() => _command(this, "CLOSE_GATE");
    [RelayCommand] private Task HoldOpenAsync() => _command(this, "LATCH_OPEN");
    [RelayCommand] private Task LockdownAsync() => _command(this, "LATCH_CLOSE");
    [RelayCommand] private Task ReleaseAsync() => _command(this, "RELEASE_LATCH");
}

public sealed partial class LiveViewModel : ViewModelBase
{
    public const int FeedLimit = 60;
    private static readonly TimeSpan StaleAfter = TimeSpan.FromSeconds(10);

    private readonly Session _session;
    private readonly IUiDispatcher _ui;
    private readonly IHaptics _haptics;
    private readonly IDialogs _dialogs;
    private readonly INavigator _nav;
    private readonly TimeProvider _clock;

    public LiveViewModel(Session session, IUiDispatcher ui, IHaptics haptics, IDialogs dialogs,
                         INavigator nav, TimeProvider clock)
    {
        _session = session;
        _ui = ui;
        _haptics = haptics;
        _dialogs = dialogs;
        _nav = nav;
        _clock = clock;
        _feedState = session.Feed.State;
        session.Feed.FrameReceived += OnFrame;
        session.Feed.StateChanged += (_, s) => _ui.Post(() => FeedState = s);
    }

    public ObservableCollection<GateTileViewModel> Gates { get; } = [];
    public ObservableCollection<DecisionRow> Feed { get; } = [];
    public bool CanOperate => _session.CanOperate;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(FeedStateText), nameof(IsLive))]
    private FeedState _feedState;

    public bool IsLive => FeedState == FeedState.Live;
    public string FeedStateText => FeedState switch
    {
        FeedState.Live => "Live",
        FeedState.Connecting => "Connecting…",
        FeedState.Reconnecting => "Reconnecting…",
        _ => "Offline",
    };

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(AllowedShare), nameof(TodayTotal))]
    private int _todayAllowed;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(AllowedShare), nameof(TodayTotal))]
    private int _todayDenied;

    public int TodayTotal => TodayAllowed + TodayDenied;

    // Share of today's reads that were allowed (0–1): the hero's bar.
    public double AllowedShare => TodayTotal == 0 ? 0 : (double)TodayAllowed / TodayTotal;

    [RelayCommand]
    public async Task LoadAsync()
    {
        await RunAsync(async () =>
        {
            var status = await _session.Api.StatusAsync();
            List<GateInfo> configured = [];
            try
            {
                configured = await _session.Api.GatesAsync();
            }
            catch (ApiException)
            {
                // The gate list is optional decoration; the tiles still work.
            }
            var now = _clock.GetUtcNow();
            foreach (var (id, snap) in status.Gates.OrderBy(g => g.Key, StringComparer.Ordinal))
            {
                var tile = TileFor(id);
                if (snap.Telemetry is { } t)
                {
                    tile.Apply(t, snap.LastEvent?.Utc ?? now);
                }
            }
            foreach (var g in configured)
            {
                var tile = TileFor(g.GateId);
                tile.Name = g.DisplayName;
                tile.Location = g.Location;
                tile.Direction = g.Direction;
            }
            RefreshStaleness();
            // Seed today's counters; the feed increments them from here.
            var today = await _session.Api.StatsAsync(DateRange.Today(_clock));
            TodayAllowed = (int)today.Authorized;
            TodayDenied = (int)(today.Captures - today.Authorized);
        });
    }

    [RelayCommand]
    private Task ShowPlateAsync(DecisionRow? row) =>
        row is { HasPlate: true } ? _nav.ShowPlateAsync(row.RawPlate) : Task.CompletedTask;

    // Called by the view on a 1 s timer: a gate that stops reporting turns
    // grey instead of showing its last state forever.
    public void RefreshStaleness()
    {
        var now = _clock.GetUtcNow();
        foreach (var g in Gates)
        {
            g.IsStale = g.LastSeen is not { } seen || now - seen > StaleAfter;
        }
    }

    internal void OnFrame(object? sender, LiveFrame frame)
    {
        _ui.Post(() => Apply(frame));
    }

    internal void Apply(LiveFrame frame)
    {
        var now = _clock.GetUtcNow();
        switch (frame.Type)
        {
            case "telemetry" when frame.Telemetry is { } t:
                TileFor(t.GateId).Apply(t, now);
                break;
            case "decision" when frame.Decision is { } d:
                var row = DecisionRow.From(d);
                Feed.Insert(0, row);
                while (Feed.Count > FeedLimit)
                {
                    Feed.RemoveAt(Feed.Count - 1);
                }
                if (d.Verdict == "AUTHORIZED")
                {
                    TodayAllowed++;
                }
                else
                {
                    TodayDenied++;
                }
                var tile = TileFor(d.GateId);
                tile.LastActivity = $"{row.Plate} · {row.Verdict}";
                // Haptics only when it matters: a refusal the guard should see.
                if (d.DenyReason == "BLOCKLISTED")
                {
                    _haptics.Play(HapticKind.Error);
                }
                else if (d.Verdict is "MANUAL_REVIEW" or "LOW_CONFIDENCE")
                {
                    _haptics.Play(HapticKind.Warning);
                }
                break;
            case "ack" when frame.Ack is { Completed: true } a:
                foreach (var g in Gates.Where(g => g.CommandPending && g.PendingCommandId == a.CommandId))
                {
                    g.CommandPending = false;
                    g.CommandStatus = a.Success ? "Done" : a.Error;
                    g.CommandFailed = !a.Success;
                    _haptics.Play(a.Success ? HapticKind.Success : HapticKind.Error);
                }
                break;
            case "fault" when frame.Fault is { } f:
                TileFor(f.GateId).LastActivity = $"Fault: {f.Code}";
                _haptics.Play(HapticKind.Error);
                break;
        }
    }

    private GateTileViewModel TileFor(string gateId)
    {
        var tile = Gates.FirstOrDefault(g => g.GateId == gateId);
        if (tile is null)
        {
            tile = new GateTileViewModel(gateId, IssueAsync);
            // Keep tiles in a stable, sorted order as gates appear.
            var i = 0;
            while (i < Gates.Count && string.CompareOrdinal(Gates[i].GateId, gateId) < 0)
            {
                i++;
            }
            Gates.Insert(i, tile);
        }
        return tile;
    }

    private async Task IssueAsync(GateTileViewModel tile, string kind)
    {
        if (!CanOperate)
        {
            return;
        }
        if (kind is "LATCH_CLOSE" or "LATCH_OPEN")
        {
            var (title, body, accept) = kind == "LATCH_CLOSE"
                ? ("Lock down " + tile.Name + "?",
                   "The gate closes and stays closed. Every plate is refused until you release it.",
                   "Lock down")
                : ("Hold " + tile.Name + " open?",
                   "The gate opens and stays open for every vehicle until you release it.",
                   "Hold open");
            if (!await _dialogs.ConfirmAsync(title, body, accept, destructive: true))
            {
                return;
            }
        }
        _haptics.Play(HapticKind.Tap);
        tile.CommandPending = true;
        tile.CommandFailed = false;
        tile.CommandStatus = "Sending…";
        var ok = await RunAsync(async () =>
        {
            var receipt = await _session.Api.CommandAsync(tile.GateId, kind);
            tile.PendingCommandId = receipt.CommandId;
        }, showBusy: false);
        if (ok)
        {
            tile.CommandStatus = "Sent";
        }
        else
        {
            tile.CommandPending = false;
            tile.CommandStatus = Error;
            tile.CommandFailed = true;
            _haptics.Play(HapticKind.Error);
        }
    }
}
