using GateControl.Core.Api;
using GateControl.Core.Formatting;
using GateControl.Core.Models;
using GateControl.Core.Services;
using GateControl.Core.ViewModels;

namespace GateControl.Core.Tests;

internal sealed class InlineUi : IUiDispatcher
{
    public void Post(Action action) => action();
}

internal sealed class RecordingHaptics : IHaptics
{
    public List<HapticKind> Played { get; } = [];
    public void Play(HapticKind kind) => Played.Add(kind);
}

internal sealed class YesDialogs : IDialogs
{
    public Task AlertAsync(string title, string message) => Task.CompletedTask;
    public Task<bool> ConfirmAsync(string title, string message, string accept, bool destructive = false) =>
        Task.FromResult(true);
}

internal sealed class NoNav : INavigator
{
    public Task ShowPlateAsync(string plate) => Task.CompletedTask;
    public Task ShowSignInAsync() => Task.CompletedTask;
    public Task ShowShellAsync() => Task.CompletedTask;
}

internal sealed class FixedClock(DateTimeOffset now) : TimeProvider
{
    public DateTimeOffset Now { get; set; } = now;
    public override DateTimeOffset GetUtcNow() => Now.ToUniversalTime();
    public override TimeZoneInfo LocalTimeZone => TimeZoneInfo.Utc;
}

public class ViewModelTests
{
    private static readonly DateTimeOffset T0 = new(2026, 3, 2, 9, 0, 0, TimeSpan.Zero);

    private static (LiveViewModel vm, RecordingHaptics haptics, FixedClock clock) Live(string role = "operator")
    {
        var api = new GateApi(new Uri("http://x/"), "t",
            new FakeHandler(_ => FakeHandler.Json("{\"ack\":{\"commandId\":\"c1\"}}")));
        var session = new Session(api, new LiveFeed(api.BaseUri), new Me { User = "u", Role = role });
        var haptics = new RecordingHaptics();
        var clock = new FixedClock(T0);
        return (new LiveViewModel(session, new InlineUi(), haptics, new YesDialogs(), new NoNav(), clock),
                haptics, clock);
    }

    private static LiveFrame DecisionFrame(string id, string verdict, string reason = "NONE") => new()
    {
        Type = "decision",
        Decision = new LiveDecision
        {
            DecisionId = id, GateId = "gate-01", MatchedPlate = "KDA123A", Verdict = verdict,
            DenyReason = reason, MatchedClass = "SUV", Confidence = 0.91,
        },
    };

    [Fact]
    public void FeedShowsNewestFirstAndIsBounded()
    {
        var (vm, _, _) = Live();
        for (var i = 0; i < LiveViewModel.FeedLimit + 5; i++)
        {
            vm.Apply(DecisionFrame("d" + i, "AUTHORIZED"));
        }
        Assert.Equal(LiveViewModel.FeedLimit, vm.Feed.Count);
        Assert.Equal("d" + (LiveViewModel.FeedLimit + 4), vm.Feed[0].Id);
        Assert.Equal("KDA 123A", vm.Feed[0].Plate);
        Assert.Equal("Allowed", vm.Feed[0].Verdict);
        Assert.Equal(Tone.Positive, vm.Feed[0].Tone);
    }

    [Fact]
    public void HapticsFireOnlyForRefusalsThatMatter()
    {
        var (vm, haptics, _) = Live();
        vm.Apply(DecisionFrame("a", "AUTHORIZED"));
        vm.Apply(DecisionFrame("b", "DENIED", "NOT_ON_ALLOWLIST"));
        Assert.Empty(haptics.Played);
        vm.Apply(DecisionFrame("c", "DENIED", "BLOCKLISTED"));
        vm.Apply(DecisionFrame("d", "MANUAL_REVIEW", "CLASS_MISMATCH"));
        Assert.Equal([HapticKind.Error, HapticKind.Warning], haptics.Played);
    }

    [Fact]
    public void TelemetryCreatesSortedTilesAndStalenessGreysThemOut()
    {
        var (vm, _, clock) = Live();
        vm.Apply(new LiveFrame { Type = "telemetry", Telemetry = new Telemetry { GateId = "gate-02", State = "OPEN" } });
        vm.Apply(new LiveFrame { Type = "telemetry", Telemetry = new Telemetry { GateId = "gate-01", State = "CLOSED" } });
        Assert.Equal(["gate-01", "gate-02"], vm.Gates.Select(g => g.GateId));
        var open = vm.Gates[1];
        Assert.Equal("Open", open.StateText);
        Assert.Equal(Tone.Positive, open.StateTone);
        clock.Now = T0.AddSeconds(30);
        vm.RefreshStaleness();
        Assert.True(open.IsStale);
        Assert.Equal(Tone.Neutral, open.StateTone);
    }

    [Fact]
    public async Task AcksCompleteOnlyTheTileThatIssuedTheCommand()
    {
        var (vm, haptics, _) = Live();
        vm.Apply(new LiveFrame { Type = "telemetry", Telemetry = new Telemetry { GateId = "gate-01" } });
        vm.Apply(new LiveFrame { Type = "telemetry", Telemetry = new Telemetry { GateId = "gate-02" } });
        await vm.Gates[0].OpenCommand.ExecuteAsync(null);
        Assert.True(vm.Gates[0].CommandPending);
        Assert.Equal("c1", vm.Gates[0].PendingCommandId);
        vm.Apply(new LiveFrame { Type = "ack", Ack = new CommandAck { CommandId = "other", Completed = true, Success = true } });
        Assert.True(vm.Gates[0].CommandPending);
        vm.Apply(new LiveFrame { Type = "ack", Ack = new CommandAck { CommandId = "c1", Completed = true, Success = false, Error = "beam blocked" } });
        Assert.False(vm.Gates[0].CommandPending);
        Assert.Equal("beam blocked", vm.Gates[0].CommandStatus);
        Assert.True(vm.Gates[0].CommandFailed);
        Assert.Equal(HapticKind.Error, haptics.Played.Last());
    }

    [Fact]
    public async Task ViewersCannotCommandGates()
    {
        var (vm, _, _) = Live(role: "viewer");
        vm.Apply(new LiveFrame { Type = "telemetry", Telemetry = new Telemetry { GateId = "gate-01" } });
        Assert.False(vm.CanOperate);
        await vm.Gates[0].OpenCommand.ExecuteAsync(null);
        Assert.False(vm.Gates[0].CommandPending);
    }

    [Fact]
    public void DashboardTurnsNumbersIntoReadableMetrics()
    {
        var api = new GateApi(new Uri("http://x/"), "t", new FakeHandler(_ => FakeHandler.Json("{}")));
        var session = new Session(api, new LiveFeed(api.BaseUri), new Me { Role = "viewer" });
        var vm = new DashboardViewModel(session, new NoNav(), new FixedClock(T0));
        vm.Apply(new Stats
        {
            Captures = 200, UniqueVehicles = 120, Authorized = 150, Denied = 40, ManualReview = 10,
            CurrentlyOnSite = 7, AvgDwellMinutes = 95, PeakHour = 8, Gates = 2, GatesOnline = 1, UnreadAlerts = 3,
        }, [new HourBucket { Hour = 8, Captures = 30, Entries = 20, Exits = 5 }],
           [new ClassCount("SEDAN", 100)], new SessionsReport(), new VerdictBreakdown(), []);
        Assert.Equal("200", vm.Headline[0].Value);
        Assert.Equal("75% of reads", vm.Headline[1].Caption);
        Assert.Equal("50", vm.Headline[2].Value);  // denied + review
        Assert.Equal("1 h 35 min", vm.Secondary[2].Value);
        Assert.Equal(Tone.Caution, vm.Secondary[4].Tone);  // a gate is offline
        Assert.Equal("Car", vm.Classes[0].Label);
        Assert.Equal(20, vm.HourlyEntries[0].Value);
    }

    [Fact]
    public void LiveTallyGivesTheAllowedShare()
    {
        var api = new GateApi(new Uri("http://x/"), "t", new FakeHandler(_ => FakeHandler.Json("{}")));
        var session = new Session(api, new LiveFeed(api.BaseUri), new Me { Role = "viewer" });
        var vm = new LiveViewModel(session, new InlineUi(), new RecordingHaptics(), new YesDialogs(), new NoNav(), new FixedClock(T0));
        Assert.Equal(0, vm.AllowedShare);
        vm.TodayAllowed = 3;
        vm.TodayDenied = 1;
        Assert.Equal(4, vm.TodayTotal);
        Assert.Equal(0.75, vm.AllowedShare, 6);
    }

    [Theory]
    [InlineData(3, "Ada", "Good evening, Ada")]
    [InlineData(8, "Ada", "Good morning, Ada")]
    [InlineData(13, "Ada", "Good afternoon, Ada")]
    [InlineData(19, "Ada", "Good evening, Ada")]
    [InlineData(9, "", "Good morning")]
    public void GreetingUsesTheUsersNameAndLocalTime(int hour, string name, string expected)
    {
        Assert.Equal(expected, DashboardViewModel.GreetingFor(hour, name));
    }

    [Fact]
    public void DashboardSplitsHourlyBarsIntoAllowedAndRefused()
    {
        var api = new GateApi(new Uri("http://x/"), "t", new FakeHandler(_ => FakeHandler.Json("{}")));
        var session = new Session(api, new LiveFeed(api.BaseUri), new Me { Role = "viewer", FullName = "Ada Lovelace" });
        var vm = new DashboardViewModel(session, new NoNav(), new FixedClock(T0));
        vm.Apply(new Stats(), [new HourBucket { Hour = 8, Captures = 30, Authorized = 22, Denied = 8 }],
                 [], new SessionsReport(), new VerdictBreakdown(), []);
        Assert.Equal(22, vm.HourlyAllowed[0].Value);
        Assert.Equal(vm.Headline[0], vm.Lead);
        Assert.Equal(vm.Headline.Count - 1, vm.HeadlineRest.Count);
        Assert.Equal(8, vm.HourlyRefused[0].Value);
        Assert.StartsWith("Good ", vm.Greeting);
        Assert.EndsWith("Ada Lovelace", vm.Greeting);
    }

    [Theory]
    [InlineData("kda 123a", "KDA123A")]
    [InlineData("KDA-123A", "KDA123A")]
    [InlineData(" k d a 1 2 3 a ", "KDA123A")]
    public void PlateEditorNormalisesPlates(string input, string expected)
    {
        Assert.Equal(expected, PlateEditorViewModel.Normalise(input));
    }

    [Fact]
    public void PlateEditorKeepsExistingRestrictions()
    {
        AllowlistUpsert? saved = null;
        var existing = new AllowlistEntry
        {
            Plate = "KDA123A", AllowedClasses = ["SUV"], TimeWindows = [new TimeWindow(360, 1080, 31)],
        };
        var vm = new PlateEditorViewModel(existing, u => { saved = u; return Task.FromResult<string?>(null); }, () => { });
        Assert.Contains(vm.Classes, c => c.Code == "SUV" && c.Selected);
        vm.SaveCommand.Execute(null);
        Assert.NotNull(saved);
        Assert.Equal(["SUV"], saved!.AllowedClasses);
        Assert.Single(saved.TimeWindows);  // hours set elsewhere survive an edit here
    }

    [Fact]
    public void UserEditorRequiresAPasswordOnlyForNewUsers()
    {
        var create = new UserEditorViewModel(null, (_, _) => Task.FromResult<string?>(null), () => { })
        {
            Username = "guard2",
        };
        Assert.False(create.SaveCommand.CanExecute(null));
        create.Password = "long-enough-pw";
        Assert.True(create.SaveCommand.CanExecute(null));

        var edit = new UserEditorViewModel(new UserAccount { Id = 3, Username = "guard2", Role = "operator" },
            (_, _) => Task.FromResult<string?>(null), () => { });
        Assert.True(edit.SaveCommand.CanExecute(null));
        Assert.Equal("operator", edit.Role.Value);
        edit.Password = "short";
        Assert.False(edit.SaveCommand.CanExecute(null));
    }

    [Fact]
    public void NewUsersDefaultToViewer()
    {
        var create = new UserEditorViewModel(null, (_, _) => Task.FromResult<string?>(null), () => { });
        Assert.Equal("viewer", create.Role.Value);
    }
}

public class DisplayTests
{
    [Theory]
    [InlineData("KDA123A", "KDA 123A")]
    [InlineData("kca001z", "KCA 001Z")]
    [InlineData("GKA123", "GKA123")]  // government plates keep their own form
    [InlineData("", "No plate")]
    public void PlatesReadLikeTheyArePrinted(string raw, string shown)
    {
        Assert.Equal(shown, Display.Plate(raw));
    }

    [Fact]
    public void WireNamesNeverReachTheScreen()
    {
        Assert.Equal("Unclear read", Display.Verdict("LOW_CONFIDENCE"));
        Assert.Equal("Different vehicle type", Display.Reason("CLASS_MISMATCH"));
        Assert.Equal("Car", Display.VehicleClass("SEDAN"));
        Assert.Equal("Some new reason", Display.Reason("SOME_NEW_REASON"));
        Assert.Equal("", Display.Reason("NONE"));
    }

    [Fact]
    public void RelativeTimes()
    {
        var now = new DateTimeOffset(2026, 3, 2, 12, 0, 0, TimeSpan.Zero);
        Assert.Equal("Just now", Display.Relative(now.AddSeconds(-10), now));
        Assert.Equal("5 min ago", Display.Relative(now.AddMinutes(-5), now));
        Assert.StartsWith("Yesterday", Display.Relative(now.AddDays(-1), now));
    }

    [Theory]
    [InlineData(null, "—")]
    [InlineData(42.4, "42 min")]
    [InlineData(120.0, "2 h")]
    [InlineData(135.0, "2 h 15 min")]
    public void Durations(double? minutes, string text)
    {
        Assert.Equal(text, Display.Minutes(minutes));
    }
}

// ViewModels/Base.cs: unchanged data is not rebuilt (moved here from TimingTests so the
// Core motion/formatting commit stands on its own).
public class ReplaceWithTests
{
    [Fact]
    public void ReplaceWithLeavesEqualContentsAlone()
    {
        var c = new System.Collections.ObjectModel.ObservableCollection<GateControl.Core.ViewModels.ChartPoint>
        {
            new("a", 1), new("b", 2),
        };
        var changes = 0;
        c.CollectionChanged += (_, _) => changes++;
        c.ReplaceWith([new("a", 1), new("b", 2)]);
        Assert.Equal(0, changes);
        c.ReplaceWith([new("a", 1), new("b", 3)]);
        Assert.True(changes > 0);
        Assert.Equal(3, c[1].Value);
    }

    [Fact]
    public void RowsCompareByWhatTheyShow()
    {
        var a = new DecisionRow("d1", "KDA123X", "AUTHORIZED", "", "SUV", "g1", 0.9, DateTimeOffset.UnixEpoch, "x");
        var b = new DecisionRow("d1", "KDA123X", "AUTHORIZED", "", "SUV", "g1", 0.9, DateTimeOffset.UnixEpoch, "x");
        var c = new DecisionRow("d2", "KDA123X", "AUTHORIZED", "", "SUV", "g1", 0.9, DateTimeOffset.UnixEpoch, "x");
        Assert.Equal(a, b);
        Assert.NotEqual(a, c);
        var list = new System.Collections.ObjectModel.ObservableCollection<DecisionRow> { a };
        var changes = 0;
        list.CollectionChanged += (_, _) => changes++;
        list.ReplaceWith([b]);
        Assert.Equal(0, changes);
    }
}
