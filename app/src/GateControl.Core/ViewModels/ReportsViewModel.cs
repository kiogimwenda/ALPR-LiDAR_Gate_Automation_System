// ReportsViewModel.cs — search, visits, unreadable reads, daily trend.
//
// MeruLPR's reports, adapted to this system:
//   Search    advanced search over every decision (plate, gate, verdict,
//             vehicle type, direction, minimum confidence, dates) with
//             paging and CSV export
//   Visits    who is on site now, longest visits, dwell distribution
//   Unreadable  frames that could not be acted on, by reason
//   Trend     one bar per day

using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using GateControl.Core.Api;
using GateControl.Core.Formatting;
using GateControl.Core.Models;
using GateControl.Core.Services;

namespace GateControl.Core.ViewModels;

public enum ReportKind
{
    Search,
    Visits,
    Unreadable,
    Trend,
}

public sealed record Choice(string Label, string Value);

public sealed partial class ReportsViewModel : ViewModelBase
{
    public const int PageSize = 50;

    private readonly Session _session;
    private readonly INavigator _nav;
    private readonly IFileShare _share;
    private readonly TimeProvider _clock;

    public ReportsViewModel(Session session, INavigator nav, IFileShare share, TimeProvider clock)
    {
        _session = session;
        _nav = nav;
        _share = share;
        _clock = clock;
        var today = clock.GetLocalNow().Date;
        _fromDate = today.AddDays(-6);
        _toDate = today;
    }

    // ---- shared -------------------------------------------------------------------

    [ObservableProperty] private ReportKind _kind = ReportKind.Search;
    [ObservableProperty] private DateTime _fromDate;
    [ObservableProperty] private DateTime _toDate;

    partial void OnKindChanged(ReportKind value) => _ = LoadAsync();

    public DateRange Range
    {
        get
        {
            var offset = _clock.GetLocalNow().Offset;
            var from = new DateTimeOffset(FromDate.Date, offset);
            var to = new DateTimeOffset(ToDate.Date.AddDays(1), offset);
            return to > from ? new DateRange(from, to) : new DateRange(to.AddDays(-1), from.AddDays(1));
        }
    }

    [RelayCommand]
    public Task LoadAsync() => Kind switch
    {
        ReportKind.Search => SearchAsync(),
        ReportKind.Visits => LoadVisitsAsync(),
        ReportKind.Unreadable => LoadUnreadableAsync(),
        _ => LoadTrendAsync(),
    };

    [RelayCommand]
    private Task ShowPlateAsync(string? plate) =>
        string.IsNullOrEmpty(plate) ? Task.CompletedTask : _nav.ShowPlateAsync(plate);

    // ---- search ---------------------------------------------------------------------

    [ObservableProperty] private string _plate = "";
    [ObservableProperty] private Choice _verdict = Verdicts[0];
    [ObservableProperty] private Choice _vehicleClass = Classes[0];
    [ObservableProperty] private Choice _direction = Directions[0];
    [ObservableProperty] private Choice _gate = AnyGate;
    [ObservableProperty] private double _minConfidence;
    [ObservableProperty] private long _total;
    [ObservableProperty] private bool _canLoadMore;

    private static readonly Choice AnyGate = new("Any gate", "");

    public static IReadOnlyList<Choice> Verdicts { get; } =
    [
        new("Any result", ""), new("Allowed", "AUTHORIZED"), new("Denied", "DENIED"),
        new("Unclear read", "LOW_CONFIDENCE"), new("Needs review", "MANUAL_REVIEW"),
    ];

    public static IReadOnlyList<Choice> Classes { get; } =
        [new Choice("Any vehicle", ""), .. Display.RestrictableClasses.Select(c => new Choice(Display.VehicleClass(c), c))];

    public static IReadOnlyList<Choice> Directions { get; } =
        [new("Any direction", ""), new("Entry", "entry"), new("Exit", "exit")];

    public ObservableCollection<Choice> Gates { get; } = [AnyGate];
    public ObservableCollection<DecisionRow> Results { get; } = [];

    public string ResultCaption => Total == 0 ? "No matching vehicles" :
        Total == 1 ? "1 vehicle" : $"{Display.Count(Total)} vehicles";

    partial void OnTotalChanged(long value) => OnPropertyChanged(nameof(ResultCaption));

    public DecisionFilter Filter => new()
    {
        Plate = Plate,
        GateId = Gate.Value,
        Verdict = Verdict.Value,
        VehicleClass = VehicleClass.Value,
        Direction = Direction.Value,
        MinConfidence = MinConfidence > 0 ? MinConfidence : null,
    };

    [RelayCommand]
    public async Task SearchAsync()
    {
        await RunAsync(async () =>
        {
            await EnsureGatesAsync();
            var page = await _session.Api.SearchAsync(Range, Filter, PageSize, 0);
            Results.ReplaceWith(page.Items.Select(DecisionRow.From));
            Total = page.Total;
            CanLoadMore = Results.Count < Total;
        });
    }

    [RelayCommand]
    private async Task LoadMoreAsync()
    {
        if (!CanLoadMore || IsBusy)
        {
            return;
        }
        await RunAsync(async () =>
        {
            var page = await _session.Api.SearchAsync(Range, Filter, PageSize, Results.Count);
            foreach (var d in page.Items)
            {
                Results.Add(DecisionRow.From(d));
            }
            Total = page.Total;
            CanLoadMore = Results.Count < Total && page.Items.Count > 0;
        }, showBusy: false);
    }

    [RelayCommand]
    private void ClearFilters()
    {
        Plate = "";
        Verdict = Verdicts[0];
        VehicleClass = Classes[0];
        Direction = Directions[0];
        Gate = AnyGate;
        MinConfidence = 0;
        _ = SearchAsync();
    }

    [RelayCommand]
    private async Task ExportAsync()
    {
        await RunAsync(async () =>
        {
            await using var csv = await _session.Api.ExportCsvAsync(Range, Filter);
            var name = $"gate-vehicles-{FromDate:yyyyMMdd}-{ToDate:yyyyMMdd}.csv";
            await _share.ShareAsync("Export vehicles", name, csv, "text/csv");
        });
    }

    private async Task EnsureGatesAsync()
    {
        if (Gates.Count > 1)
        {
            return;
        }
        try
        {
            foreach (var g in await _session.Api.GatesAsync())
            {
                Gates.Add(new Choice(g.DisplayName, g.GateId));
            }
        }
        catch (ApiException)
        {
            // Filtering by gate is optional.
        }
    }

    // ---- visits -------------------------------------------------------------------------

    public ObservableCollection<SessionRow> OnSite { get; } = [];
    public ObservableCollection<SessionRow> Longest { get; } = [];
    public ObservableCollection<ChartPoint> DwellChart { get; } = [];
    [ObservableProperty] private string _visitsSummary = "";

    private async Task LoadVisitsAsync()
    {
        await RunAsync(async () =>
        {
            var s = await _session.Api.SessionsAsync(Range);
            OnSite.ReplaceWith(s.OnSite);
            Longest.ReplaceWith(s.Longest);
            DwellChart.ReplaceWith(s.Categories.Select(c => new ChartPoint(c.Category, c.Count)));
            VisitsSummary = s.Completed == 0
                ? "No completed visits in this period. Visits need an entry gate and an exit gate (Settings → Gates)."
                : $"{Display.Count(s.Completed)} completed visits · average stay {Display.Minutes(s.AvgMinutes)}";
        });
    }

    // ---- unreadable ---------------------------------------------------------------------

    public ObservableCollection<ChartPoint> UnreadableReasons { get; } = [];
    public ObservableCollection<DecisionRow> Unreadable { get; } = [];
    [ObservableProperty] private long _unreadableTotal;

    private async Task LoadUnreadableAsync()
    {
        await RunAsync(async () =>
        {
            var r = await _session.Api.RejectedAsync(Range, 100, 0);
            UnreadableTotal = r.Total;
            UnreadableReasons.ReplaceWith(r.Breakdown.Select(b =>
                new ChartPoint(Display.Reason(b.Reason), b.Count, Tone.Caution)));
            Unreadable.ReplaceWith(r.Items.Select(DecisionRow.From));
        });
    }

    // ---- trend --------------------------------------------------------------------------

    public ObservableCollection<ChartPoint> TrendVehicles { get; } = [];
    public ObservableCollection<ChartPoint> TrendRefused { get; } = [];

    private async Task LoadTrendAsync()
    {
        await RunAsync(async () =>
        {
            var days = await _session.Api.DailyAsync(Range);
            TrendVehicles.ReplaceWith(days.Select(d => new ChartPoint(ShortDate(d.Date), d.Captures)));
            TrendRefused.ReplaceWith(days.Select(d => new ChartPoint(ShortDate(d.Date), d.Denied, Tone.Negative)));
        });
    }

    private static string ShortDate(string iso) =>
        DateTime.TryParse(iso, System.Globalization.CultureInfo.InvariantCulture,
                          System.Globalization.DateTimeStyles.None, out var d)
            ? d.ToString("d MMM", System.Globalization.CultureInfo.CurrentCulture)
            : iso;
}
