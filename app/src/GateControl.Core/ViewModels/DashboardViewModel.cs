// DashboardViewModel.cs — the site at a glance for a chosen period.
//
// MeruLPR's dashboard, adapted: headline numbers, hourly activity,
// entries vs exits, vehicle mix, dwell time, verdicts and the most
// frequent plates — for today, the last 7 days or the last 30.

using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using GateControl.Core.Api;
using GateControl.Core.Formatting;
using GateControl.Core.Models;
using GateControl.Core.Services;

namespace GateControl.Core.ViewModels;

public enum Period
{
    Today,
    Week,
    Month,
}

public sealed partial class DashboardViewModel : ViewModelBase
{
    private readonly Session _session;
    private readonly INavigator _nav;
    private readonly TimeProvider _clock;

    public DashboardViewModel(Session session, INavigator nav, TimeProvider clock)
    {
        _session = session;
        _nav = nav;
        _clock = clock;
    }

    [ObservableProperty] private Period _period = Period.Today;
    [ObservableProperty] private Stats? _stats;

    public ObservableCollection<Metric> Headline { get; } = [];
    public ObservableCollection<Metric> Secondary { get; } = [];
    public ObservableCollection<ChartPoint> HourlyCaptures { get; } = [];
    public ObservableCollection<ChartPoint> HourlyEntries { get; } = [];
    public ObservableCollection<ChartPoint> HourlyExits { get; } = [];
    public ObservableCollection<ChartPoint> Classes { get; } = [];
    public ObservableCollection<ChartPoint> Dwell { get; } = [];
    public ObservableCollection<ChartPoint> Verdicts { get; } = [];
    public ObservableCollection<TopPlate> TopPlates { get; } = [];

    public string PeriodCaption => Period switch
    {
        Period.Today => "Today",
        Period.Week => "Last 7 days",
        _ => "Last 30 days",
    };

    partial void OnPeriodChanged(Period value)
    {
        OnPropertyChanged(nameof(PeriodCaption));
        _ = LoadAsync();
    }

    public DateRange Range => Period switch
    {
        Period.Today => DateRange.Today(_clock),
        Period.Week => DateRange.LastDays(_clock, 7),
        _ => DateRange.LastDays(_clock, 30),
    };

    [RelayCommand]
    public async Task LoadAsync()
    {
        var range = Range;
        await RunAsync(async () =>
        {
            var api = _session.Api;
            var statsT = api.StatsAsync(range);
            var hourlyT = api.HourlyAsync(range);
            var classesT = api.ClassesAsync(range);
            var sessionsT = api.SessionsAsync(range);
            var verdictsT = api.VerdictsAsync(range);
            var topT = api.TopPlatesAsync(range, 8);
            await Task.WhenAll(statsT, hourlyT, classesT, sessionsT, verdictsT, topT);
            Apply(await statsT, await hourlyT, await classesT, await sessionsT, await verdictsT, await topT);
        }, showBusy: Stats is null);
    }

    [RelayCommand]
    private Task ShowPlateAsync(TopPlate? p) =>
        p is null ? Task.CompletedTask : _nav.ShowPlateAsync(p.Plate);

    internal void Apply(Stats s, List<HourBucket> hourly, List<ClassCount> classes, SessionsReport sessions,
                        VerdictBreakdown verdicts, List<TopPlate> top)
    {
        Stats = s;
        var refused = s.Denied + s.LowConfidence + s.ManualReview;
        Headline.ReplaceWith(
        [
            new Metric("Vehicles", Display.Count(s.Captures), $"{Display.Count(s.UniqueVehicles)} different plates"),
            new Metric("Allowed", Display.Count(s.Authorized),
                       s.Captures > 0 ? Display.Percent((double)s.Authorized / s.Captures) + " of reads" : "",
                       Tone.Positive),
            new Metric("Refused", Display.Count(refused),
                       s.ManualReview > 0 ? $"{s.ManualReview} need review" : "",
                       refused > 0 ? Tone.Negative : Tone.Neutral),
            new Metric("On site now", Display.Count(s.CurrentlyOnSite), "", Tone.Accent),
        ]);
        Secondary.ReplaceWith(
        [
            new Metric("Entries", Display.Count(s.Entries), "Allowed in"),
            new Metric("Exits", Display.Count(s.Exits), "Allowed out"),
            new Metric("Average stay", Display.Minutes(s.AvgDwellMinutes), "Completed visits"),
            new Metric("Busiest hour", Display.Hour(s.PeakHour), ""),
            new Metric("Gates online", $"{s.GatesOnline} of {s.Gates}", "",
                       s.Gates > 0 && s.GatesOnline < s.Gates ? Tone.Caution : Tone.Neutral),
            new Metric("Open alerts", Display.Count(s.UnreadAlerts), "",
                       s.UnreadAlerts > 0 ? Tone.Negative : Tone.Neutral),
        ]);
        HourlyCaptures.ReplaceWith(hourly.Select(h => new ChartPoint(Display.Hour(h.Hour), h.Captures)));
        HourlyEntries.ReplaceWith(hourly.Select(h => new ChartPoint(Display.Hour(h.Hour), h.Entries, Tone.Positive)));
        HourlyExits.ReplaceWith(hourly.Select(h => new ChartPoint(Display.Hour(h.Hour), h.Exits, Tone.Caution)));
        Classes.ReplaceWith(classes.Select(c => new ChartPoint(Display.VehicleClass(c.VehicleClass), c.Count)));
        Dwell.ReplaceWith(sessions.Categories.Select(c => new ChartPoint(c.Category, c.Count)));
        Verdicts.ReplaceWith(verdicts.ByVerdict.Select(v =>
            new ChartPoint(Display.Verdict(v.Verdict), v.Count, Display.VerdictTone(v.Verdict))));
        TopPlates.ReplaceWith(top);
    }
}
