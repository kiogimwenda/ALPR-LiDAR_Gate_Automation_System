// Base.cs — shared view-model plumbing and the session host.

using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using GateControl.Core.Api;
using GateControl.Core.Formatting;
using GateControl.Core.Models;
using GateControl.Core.Services;

namespace GateControl.Core.ViewModels;

public abstract partial class ViewModelBase : ObservableObject
{
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(IsIdle))]
    private bool _isBusy;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasError))]
    private string _error = "";

    public bool IsIdle => !IsBusy;
    public bool HasError => Error.Length > 0;

    // Runs `work`, turning failures into a readable Error instead of a
    // crash. Busy is only shown for work that takes long enough to notice.
    protected async Task<bool> RunAsync(Func<Task> work, bool showBusy = true)
    {
        if (showBusy)
        {
            IsBusy = true;
        }
        Error = "";
        try
        {
            await work();
            return true;
        }
        catch (ApiException ex)
        {
            Error = ex.Message;
        }
        catch (HttpRequestException)
        {
            Error = "Can't reach the gate server. Check the network and try again.";
        }
        catch (TaskCanceledException)
        {
            Error = "The gate server took too long to answer.";
        }
        finally
        {
            IsBusy = false;
        }
        return false;
    }
}

// Owns the signed-in Session: sign-in, resume on launch, sign-out.
public sealed partial class SessionHost : ObservableObject
{
    private readonly ISessionStore _store;
    private readonly Func<Uri, string?, GateApi> _apiFactory;
    private readonly Func<Uri, LiveFeed> _feedFactory;

    public SessionHost(ISessionStore store, Func<Uri, string?, GateApi>? apiFactory = null,
                       Func<Uri, LiveFeed>? feedFactory = null)
    {
        _store = store;
        _apiFactory = apiFactory ?? ((uri, token) => new GateApi(uri, token));
        _feedFactory = feedFactory ?? (uri => new LiveFeed(uri));
    }

    [ObservableProperty]
    private Session? _current;

    public event EventHandler? SignedOut;

    public static bool TryParseServer(string input, out Uri uri)
    {
        var text = input.Trim();
        if (text.Length > 0 && !text.Contains("://", StringComparison.Ordinal))
        {
            text = "http://" + text;
        }
        if (Uri.TryCreate(text, UriKind.Absolute, out var parsed) &&
            (parsed.Scheme == Uri.UriSchemeHttp || parsed.Scheme == Uri.UriSchemeHttps) &&
            parsed.Host.Length > 0)
        {
            // Always a directory URL so relative "api/…" paths resolve under it.
            var b = new UriBuilder(parsed) { Path = parsed.AbsolutePath.TrimEnd('/') + "/" };
            uri = b.Uri;
            return true;
        }
        uri = new Uri("http://localhost/");
        return false;
    }

    public string? SavedServer => _store.ServerUrl;
    public string? SavedUsername => _store.LastUsername;

    // Signs in; an open-access backend (no accounts configured) is entered
    // without credentials.
    public async Task SignInAsync(Uri server, string username, string password, CancellationToken ct = default)
    {
        var api = _apiFactory(server, null);
        try
        {
            await api.HealthAsync(ct);
            string? token = null;
            try
            {
                var login = await api.LoginAsync(username, password, ct);
                token = login.Token;
                api.SetToken(token);
            }
            catch (ApiException ex) when (ex.Status == System.Net.HttpStatusCode.ServiceUnavailable)
            {
                // Open access: the backend has no accounts.
            }
            var me = await api.MeAsync(ct);
            _store.ServerUrl = server.ToString();
            _store.LastUsername = username;
            await _store.SetTokenAsync(token);
            Activate(api, me);
        }
        catch
        {
            api.Dispose();
            throw;
        }
    }

    // On launch: a stored server + token that the backend still accepts.
    public async Task<bool> TryResumeAsync(CancellationToken ct = default)
    {
        if (_store.ServerUrl is not { } url || !TryParseServer(url, out var server))
        {
            return false;
        }
        var token = await _store.GetTokenAsync();
        var api = _apiFactory(server, token);
        try
        {
            var me = await api.MeAsync(ct);
            if (me.AuthRequired && string.IsNullOrEmpty(token))
            {
                api.Dispose();
                return false;
            }
            Activate(api, me);
            return true;
        }
        catch (Exception ex) when (ex is ApiException or HttpRequestException or TaskCanceledException)
        {
            api.Dispose();
            return false;
        }
    }

    public async Task SignOutAsync()
    {
        await _store.SetTokenAsync(null);
        if (Current is { } s)
        {
            Current = null;
            await s.Feed.StopAsync();
            s.Api.Dispose();
        }
        SignedOut?.Invoke(this, EventArgs.Empty);
    }

    private void Activate(GateApi api, Me me)
    {
        api.SessionExpired += async (_, _) => await SignOutAsync();
        var feed = _feedFactory(api.BaseUri);
        Current = new Session(api, feed, me);
        feed.Start();
    }
}

// One decision as a list row (live feed, search results, plate history).
public sealed class DecisionRow : IEquatable<DecisionRow>
{
    // A decision is immutable once made: its id identifies it.
    public bool Equals(DecisionRow? o) => o is not null && Id == o.Id && Id.Length > 0;

    public override bool Equals(object? obj) => Equals(obj as DecisionRow);

    public override int GetHashCode() => Id.GetHashCode(StringComparison.Ordinal);

    public DecisionRow(string id, string plate, string verdict, string reason, string vehicleClass,
                       string gate, double confidence, DateTimeOffset when, string actor)
    {
        Id = id;
        RawPlate = plate;
        Plate = Display.Plate(plate);
        Verdict = Display.Verdict(verdict);
        VerdictCode = verdict;
        Tone = Display.VerdictTone(verdict);
        Reason = Display.Reason(reason);
        VehicleClass = Display.VehicleClass(vehicleClass);
        Gate = gate;
        Confidence = confidence;
        ConfidenceText = Display.Percent(confidence);
        When = when;
        Actor = actor;
    }

    public static DecisionRow From(LiveDecision d) =>
        new(d.DecisionId, d.MatchedPlate, d.Verdict, d.DenyReason, d.MatchedClass, d.GateId,
            d.Confidence, d.Decided?.Utc ?? DateTimeOffset.UtcNow, d.Actor);

    public static DecisionRow From(Decision d) =>
        new(d.DecisionId, d.Plate, d.Verdict, d.DenyReason, d.VehicleClass,
            string.IsNullOrEmpty(d.GateName) ? d.GateId : d.GateName, d.Confidence,
            d.Decided?.Utc ?? DateTimeOffset.UtcNow, d.Actor);

    public string Id { get; }
    public string RawPlate { get; }
    public string Plate { get; }
    public string Verdict { get; }
    public string VerdictCode { get; }
    public Tone Tone { get; }
    public string Reason { get; }
    public string VehicleClass { get; }
    public string Gate { get; }
    public double Confidence { get; }
    public string ConfidenceText { get; }
    public DateTimeOffset When { get; }
    public string Actor { get; }
    public bool HasPlate => RawPlate.Length > 0;
    public string Detail => string.Join(" · ", new[] { VehicleClass, Gate, ConfidenceText }
        .Where(s => !string.IsNullOrEmpty(s)));
    public string WhenText(DateTimeOffset now) => Display.Relative(When, now);
    public string TimeText => When.ToLocalTime().ToString("t", System.Globalization.CultureInfo.CurrentCulture);
}

// A KPI tile on the dashboard.
public sealed record Metric(string Title, string Value, string Caption, Tone Tone = Tone.Neutral);

// A labelled number for charts (bars, donut slices, lines).
public sealed record ChartPoint(string Label, double Value, Tone Tone = Tone.Accent);

public static class CollectionExtensions
{
    // Replace the contents without allocating a new collection, so bound
    // views keep their scroll position and only re-render what changed.
    //
    // Unchanged contents (by value, for records) are left alone: a refresh
    // that brings the same data back must not rebuild every bound row (that
    // re-inflated whole lists mid-animation and stalled frames).
    public static void ReplaceWith<T>(this ObservableCollection<T> target, IEnumerable<T> items)
    {
        var list = items as IList<T> ?? items.ToList();
        if (list.Count == target.Count && list.SequenceEqual(target))
        {
            return;
        }
        target.Clear();
        foreach (var item in list)
        {
            target.Add(item);
        }
    }
}
