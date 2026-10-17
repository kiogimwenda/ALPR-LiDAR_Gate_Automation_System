// GateApi.cs — typed client for the gate-dashboard REST API.
//
// One instance per signed-in session. Every call carries the bearer
// token; a 401 raises SessionExpired so the shell can send the operator
// back to sign-in instead of showing a wall of errors. Errors surface as
// ApiException with the backend's own {"error": "..."} message, which is
// written for people ("this action needs the operator role").

using System.Globalization;
using System.Net;
using System.Net.Http.Headers;
using System.Net.Http.Json;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;
using GateControl.Core.Models;

namespace GateControl.Core.Api;

public sealed class ApiException(HttpStatusCode status, string message) : Exception(message)
{
    public HttpStatusCode Status { get; } = status;
    public bool IsForbidden => Status == HttpStatusCode.Forbidden;
    public bool IsUnauthorized => Status == HttpStatusCode.Unauthorized;
}

// A reporting window. Times are local to the device; the backend buckets
// hours/days with TzOffsetMinutes so the charts read in the operator's clock.
public readonly record struct DateRange(DateTimeOffset From, DateTimeOffset To)
{
    public int TzOffsetMinutes => (int)From.Offset.TotalMinutes;

    public static DateRange Today(TimeProvider clock)
    {
        var now = clock.GetLocalNow();
        var start = new DateTimeOffset(now.Date, now.Offset);
        return new DateRange(start, now);
    }

    public static DateRange LastDays(TimeProvider clock, int days)
    {
        var now = clock.GetLocalNow();
        var start = new DateTimeOffset(now.Date, now.Offset).AddDays(-(days - 1));
        return new DateRange(start, now);
    }

    internal string Query =>
        $"from={From.ToUnixTimeMilliseconds()}&to={To.ToUnixTimeMilliseconds()}&tz={TzOffsetMinutes}";
}

public sealed record DecisionFilter
{
    public string Plate { get; init; } = "";
    public string GateId { get; init; } = "";
    public string Verdict { get; init; } = "";
    public string VehicleClass { get; init; } = "";
    public string Direction { get; init; } = "";
    public double? MinConfidence { get; init; }

    internal string Query
    {
        get
        {
            var q = new StringBuilder();
            void Add(string key, string value)
            {
                if (!string.IsNullOrWhiteSpace(value))
                {
                    q.Append('&').Append(key).Append('=').Append(Uri.EscapeDataString(value.Trim()));
                }
            }
            Add("plate", Plate);
            Add("gate", GateId);
            Add("verdict", Verdict);
            Add("class", VehicleClass);
            Add("direction", Direction);
            if (MinConfidence is { } mc)
            {
                Add("minConfidence", mc.ToString("0.###", CultureInfo.InvariantCulture));
            }
            return q.ToString();
        }
    }
}

public sealed class GateApi : IDisposable
{
    public static readonly JsonSerializerOptions Json = new(JsonSerializerDefaults.Web)
    {
        NumberHandling = JsonNumberHandling.AllowReadingFromString,
    };

    private readonly HttpClient _http;

    public GateApi(Uri baseUri, string? token, HttpMessageHandler? handler = null)
    {
        BaseUri = baseUri;
        // A short connect timeout lets a host name with several addresses
        // (IPv6 then IPv4, as "localhost" often is) fall through to the
        // next address quickly instead of waiting out the whole request.
        _http = handler is null
            ? new HttpClient(new SocketsHttpHandler
            {
                ConnectTimeout = TimeSpan.FromSeconds(5),
                PooledConnectionLifetime = TimeSpan.FromMinutes(5),
            })
            : new HttpClient(handler, disposeHandler: false);
        _http.BaseAddress = baseUri;
        _http.Timeout = TimeSpan.FromSeconds(20);
        SetToken(token);
    }

    public Uri BaseUri { get; }
    public string? Token { get; private set; }

    // Raised (once per failing call) when the backend answers 401.
    public event EventHandler? SessionExpired;

    public void SetToken(string? token)
    {
        Token = token;
        _http.DefaultRequestHeaders.Authorization =
            string.IsNullOrEmpty(token) ? null : new AuthenticationHeaderValue("Bearer", token);
    }

    // ---- auth ---------------------------------------------------------------

    public async Task<LoginResponse> LoginAsync(string user, string password, CancellationToken ct = default)
    {
        using var resp = await _http.PostAsJsonAsync("api/auth/login",
            new { username = user, password }, Json, ct).ConfigureAwait(false);
        if (resp.StatusCode == HttpStatusCode.Unauthorized)
        {
            throw new ApiException(resp.StatusCode, "That username and password don't match.");
        }
        return await Read<LoginResponse>(resp, ct, raiseExpired: false).ConfigureAwait(false);
    }

    public Task<Me> MeAsync(CancellationToken ct = default) => Get<Me>("api/auth/me", ct);

    public Task ChangePasswordAsync(string current, string next, CancellationToken ct = default) =>
        Send(HttpMethod.Post, "api/auth/password", new { current, @new = next }, ct);

    public async Task<Health> HealthAsync(CancellationToken ct = default)
    {
        using var resp = await _http.GetAsync("api/health", ct).ConfigureAwait(false);
        return await Read<Health>(resp, ct, raiseExpired: false).ConfigureAwait(false);
    }

    // ---- live -----------------------------------------------------------------

    public Task<StatusResponse> StatusAsync(CancellationToken ct = default) =>
        Get<StatusResponse>("api/status", ct);

    // Returns the server's receipt; its CommandId matches the completion
    // ack that arrives on the live feed when the gate finishes.
    public async Task<CommandAck> CommandAsync(string gateId, string kind, CancellationToken ct = default)
    {
        using var resp = await _http.PostAsJsonAsync(
            $"api/gates/{Uri.EscapeDataString(gateId)}/command", new { kind }, Json, ct).ConfigureAwait(false);
        var receipt = await Read<CommandReceipt>(resp, ct, raiseExpired: true).ConfigureAwait(false);
        return receipt.Ack ?? new CommandAck();
    }

    private sealed record CommandReceipt(CommandAck? Ack);

    // ---- reports ----------------------------------------------------------------

    public Task<Stats> StatsAsync(DateRange r, CancellationToken ct = default) =>
        Get<Stats>($"api/reports/stats?{r.Query}", ct);

    public Task<List<HourBucket>> HourlyAsync(DateRange r, CancellationToken ct = default) =>
        Get<List<HourBucket>>($"api/reports/hourly?{r.Query}", ct);

    public Task<List<DayBucket>> DailyAsync(DateRange r, CancellationToken ct = default) =>
        Get<List<DayBucket>>($"api/reports/daily?{r.Query}", ct);

    public Task<List<ClassCount>> ClassesAsync(DateRange r, CancellationToken ct = default) =>
        Get<List<ClassCount>>($"api/reports/classes?{r.Query}", ct);

    public Task<VerdictBreakdown> VerdictsAsync(DateRange r, CancellationToken ct = default) =>
        Get<VerdictBreakdown>($"api/reports/verdicts?{r.Query}", ct);

    public Task<List<TopPlate>> TopPlatesAsync(DateRange r, int limit, CancellationToken ct = default) =>
        Get<List<TopPlate>>($"api/reports/top-plates?{r.Query}&limit={limit}", ct);

    public Task<SessionsReport> SessionsAsync(DateRange r, CancellationToken ct = default) =>
        Get<SessionsReport>($"api/reports/sessions?{r.Query}", ct);

    public Task<RejectedReport> RejectedAsync(DateRange r, int limit, int offset, CancellationToken ct = default) =>
        Get<RejectedReport>($"api/reports/rejected?{r.Query}&limit={limit}&offset={offset}", ct);

    public Task<DecisionPage> SearchAsync(DateRange r, DecisionFilter f, int limit, int offset,
                                          CancellationToken ct = default) =>
        Get<DecisionPage>($"api/decisions?{r.Query}{f.Query}&limit={limit}&offset={offset}", ct);

    public async Task<Stream> ExportCsvAsync(DateRange r, DecisionFilter f, CancellationToken ct = default)
    {
        var resp = await _http.GetAsync($"api/decisions/export?{r.Query}{f.Query}",
            HttpCompletionOption.ResponseHeadersRead, ct).ConfigureAwait(false);
        await EnsureOk(resp, ct, raiseExpired: true).ConfigureAwait(false);
        return await resp.Content.ReadAsStreamAsync(ct).ConfigureAwait(false);
    }

    public Task<PlateHistory> PlateAsync(string plate, CancellationToken ct = default) =>
        Get<PlateHistory>($"api/plates/{Uri.EscapeDataString(plate)}?limit=100", ct);

    // ---- alerts -------------------------------------------------------------------

    public Task<AlertPage> AlertsAsync(bool unreadOnly, int limit, int offset, CancellationToken ct = default) =>
        Get<AlertPage>($"api/alerts?unread={(unreadOnly ? 1 : 0)}&limit={limit}&offset={offset}", ct);

    public Task AcknowledgeAsync(long id, CancellationToken ct = default) =>
        Send(HttpMethod.Post, $"api/alerts/{id}/ack", null, ct);

    public Task AcknowledgeAllAsync(CancellationToken ct = default) =>
        Send(HttpMethod.Post, "api/alerts/ack-all", null, ct);

    // ---- gates, users, settings ---------------------------------------------------------

    public Task<List<GateInfo>> GatesAsync(CancellationToken ct = default) =>
        Get<List<GateInfo>>("api/gates", ct);

    public Task ConfigureGateAsync(string gateId, string name, string location, string direction,
                                   CancellationToken ct = default) =>
        Send(HttpMethod.Put, $"api/gates/{Uri.EscapeDataString(gateId)}",
             new { name, location, direction }, ct);

    public Task<List<UserAccount>> UsersAsync(CancellationToken ct = default) =>
        Get<List<UserAccount>>("api/users", ct);

    public Task SaveUserAsync(UserAccount user, string? password, CancellationToken ct = default)
    {
        var body = new Dictionary<string, object?>
        {
            ["username"] = user.Username,
            ["fullName"] = user.FullName,
            ["email"] = user.Email,
            ["role"] = user.Role,
            ["active"] = user.Active,
        };
        if (!string.IsNullOrEmpty(password))
        {
            body["password"] = password;
        }
        return user.Id == 0
            ? Send(HttpMethod.Post, "api/users", body, ct)
            : Send(HttpMethod.Put, $"api/users/{user.Id}", body, ct);
    }

    public Task DeleteUserAsync(long id, CancellationToken ct = default) =>
        Send(HttpMethod.Delete, $"api/users/{id}", null, ct);

    public Task<SiteSettings> SettingsAsync(CancellationToken ct = default) =>
        Get<SiteSettings>("api/settings", ct);

    public Task SaveSettingsAsync(SiteSettings s, CancellationToken ct = default) =>
        Send(HttpMethod.Put, "api/settings", s, ct);

    // ---- allowlist --------------------------------------------------------------------

    public Task<AllowlistPage> AllowlistAsync(int pageSize, string pageToken, CancellationToken ct = default) =>
        Get<AllowlistPage>($"api/allowlist?pageSize={pageSize}&pageToken={Uri.EscapeDataString(pageToken)}", ct);

    public Task UpsertAllowlistAsync(AllowlistUpsert entry, CancellationToken ct = default) =>
        Send(HttpMethod.Post, "api/allowlist", entry, ct);

    public Task RemoveAllowlistAsync(string plate, CancellationToken ct = default) =>
        Send(HttpMethod.Delete, $"api/allowlist/{Uri.EscapeDataString(plate)}", null, ct);

    // ---- plumbing -------------------------------------------------------------------------

    private async Task<T> Get<T>(string path, CancellationToken ct)
    {
        using var resp = await _http.GetAsync(path, ct).ConfigureAwait(false);
        return await Read<T>(resp, ct, raiseExpired: true).ConfigureAwait(false);
    }

    private async Task Send(HttpMethod method, string path, object? body, CancellationToken ct)
    {
        using var req = new HttpRequestMessage(method, path);
        if (body is not null)
        {
            req.Content = JsonContent.Create(body, body.GetType(), options: Json);
        }
        using var resp = await _http.SendAsync(req, ct).ConfigureAwait(false);
        await EnsureOk(resp, ct, raiseExpired: true).ConfigureAwait(false);
    }

    private async Task<T> Read<T>(HttpResponseMessage resp, CancellationToken ct, bool raiseExpired)
    {
        await EnsureOk(resp, ct, raiseExpired).ConfigureAwait(false);
        var value = await resp.Content.ReadFromJsonAsync<T>(Json, ct).ConfigureAwait(false);
        return value ?? throw new ApiException(resp.StatusCode, "The server sent an empty answer.");
    }

    private async Task EnsureOk(HttpResponseMessage resp, CancellationToken ct, bool raiseExpired)
    {
        if (resp.IsSuccessStatusCode)
        {
            return;
        }
        if (resp.StatusCode == HttpStatusCode.Unauthorized && raiseExpired)
        {
            SessionExpired?.Invoke(this, EventArgs.Empty);
        }
        var message = await ErrorMessage(resp, ct).ConfigureAwait(false);
        throw new ApiException(resp.StatusCode, message);
    }

    internal static async Task<string> ErrorMessage(HttpResponseMessage resp, CancellationToken ct)
    {
        try
        {
            var body = await resp.Content.ReadAsStringAsync(ct).ConfigureAwait(false);
            using var doc = JsonDocument.Parse(body);
            if (doc.RootElement.TryGetProperty("error", out var e) && e.GetString() is { Length: > 0 } msg)
            {
                return char.ToUpperInvariant(msg[0]) + msg[1..] + (msg.EndsWith('.') ? "" : ".");
            }
        }
        catch (JsonException)
        {
        }
        return resp.StatusCode switch
        {
            HttpStatusCode.Unauthorized => "Your session has ended. Sign in again.",
            HttpStatusCode.Forbidden => "Your account isn't allowed to do that.",
            HttpStatusCode.ServiceUnavailable => "The gate server isn't reachable right now.",
            HttpStatusCode.GatewayTimeout => "The gate server took too long to answer.",
            _ => $"The server answered {(int)resp.StatusCode} {resp.ReasonPhrase}.",
        };
    }

    public void Dispose() => _http.Dispose();
}
