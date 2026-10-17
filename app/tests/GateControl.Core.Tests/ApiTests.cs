using System.Net;
using System.Text;
using GateControl.Core.Api;
using GateControl.Core.Models;
using GateControl.Core.ViewModels;

namespace GateControl.Core.Tests;

// Records requests and answers from a script.
internal sealed class FakeHandler(Func<HttpRequestMessage, HttpResponseMessage> respond) : HttpMessageHandler
{
    public List<HttpRequestMessage> Requests { get; } = [];
    public List<string> Bodies { get; } = [];

    protected override async Task<HttpResponseMessage> SendAsync(HttpRequestMessage request, CancellationToken ct)
    {
        Requests.Add(request);
        Bodies.Add(request.Content is null ? "" : await request.Content.ReadAsStringAsync(ct));
        return respond(request);
    }

    public static HttpResponseMessage Json(string body, HttpStatusCode code = HttpStatusCode.OK) =>
        new(code) { Content = new StringContent(body, Encoding.UTF8, "application/json") };
}

public class ApiTests
{
    private static readonly Uri Base = new("http://gate.local:8080/");

    [Fact]
    public async Task ReportsCarryRangeAndTimeZone()
    {
        var h = new FakeHandler(_ => FakeHandler.Json("{\"captures\":12,\"peakHour\":9}"));
        using var api = new GateApi(Base, "tok", h);
        var from = new DateTimeOffset(2026, 3, 2, 0, 0, 0, TimeSpan.FromHours(3));
        var stats = await api.StatsAsync(new DateRange(from, from.AddDays(1)));
        Assert.Equal(12, stats.Captures);
        Assert.Equal(9, stats.PeakHour);
        var uri = h.Requests[0].RequestUri!.ToString();
        Assert.Contains($"from={from.ToUnixTimeMilliseconds()}", uri);
        Assert.Contains("tz=180", uri);
        Assert.Equal("Bearer", h.Requests[0].Headers.Authorization!.Scheme);
    }

    [Fact]
    public async Task SearchEncodesOnlyTheFiltersThatAreSet()
    {
        var h = new FakeHandler(_ => FakeHandler.Json("{\"total\":0,\"items\":[]}"));
        using var api = new GateApi(Base, null, h);
        var r = new DateRange(DateTimeOffset.UnixEpoch, DateTimeOffset.UnixEpoch.AddDays(1));
        await api.SearchAsync(r, new DecisionFilter { Plate = "KDA 1", MinConfidence = 0.75 }, 50, 100);
        var uri = h.Requests[0].RequestUri!.AbsoluteUri;  // ToString() would un-escape
        Assert.Contains("plate=KDA%201", uri);
        Assert.Contains("minConfidence=0.75", uri);
        Assert.Contains("limit=50&offset=100", uri);
        Assert.DoesNotContain("verdict=", uri);
        Assert.DoesNotContain("gate=", uri);
    }

    [Fact]
    public async Task ErrorsUseTheBackendsOwnWords()
    {
        var h = new FakeHandler(_ =>
            FakeHandler.Json("{\"error\":\"this action needs the operator role\"}", HttpStatusCode.Forbidden));
        using var api = new GateApi(Base, "tok", h);
        var ex = await Assert.ThrowsAsync<ApiException>(() => api.CommandAsync("g1", "OPEN_GATE"));
        Assert.True(ex.IsForbidden);
        Assert.Equal("This action needs the operator role.", ex.Message);
    }

    [Fact]
    public async Task UnauthorizedRaisesSessionExpired()
    {
        var h = new FakeHandler(_ => FakeHandler.Json("{}", HttpStatusCode.Unauthorized));
        using var api = new GateApi(Base, "old", h);
        var expired = 0;
        api.SessionExpired += (_, _) => expired++;
        await Assert.ThrowsAsync<ApiException>(() => api.AlertsAsync(true, 10, 0));
        Assert.Equal(1, expired);
    }

    [Fact]
    public async Task WrongPasswordIsNotASessionExpiry()
    {
        var h = new FakeHandler(_ => FakeHandler.Json("{\"error\":\"invalid credentials\"}", HttpStatusCode.Unauthorized));
        using var api = new GateApi(Base, null, h);
        var expired = 0;
        api.SessionExpired += (_, _) => expired++;
        var ex = await Assert.ThrowsAsync<ApiException>(() => api.LoginAsync("u", "p"));
        Assert.Equal(0, expired);
        Assert.Contains("don't match", ex.Message);
    }

    [Fact]
    public async Task CommandReturnsTheReceiptForAckMatching()
    {
        var h = new FakeHandler(_ =>
            FakeHandler.Json("{\"ack\":{\"commandId\":\"c-42\",\"completed\":false}}"));
        using var api = new GateApi(Base, "tok", h);
        var ack = await api.CommandAsync("gate 1", "OPEN_GATE");
        Assert.Equal("c-42", ack.CommandId);
        Assert.EndsWith("api/gates/gate%201/command", h.Requests[0].RequestUri!.AbsoluteUri);
        Assert.Contains("\"kind\":\"OPEN_GATE\"", h.Bodies[0]);
    }

    [Fact]
    public async Task NewUsersArePostedExistingOnesPut()
    {
        var h = new FakeHandler(_ => FakeHandler.Json("{}"));
        using var api = new GateApi(Base, "tok", h);
        await api.SaveUserAsync(new UserAccount { Username = "guard1", Role = "operator" }, "long-password-1");
        await api.SaveUserAsync(new UserAccount { Id = 7, Username = "guard1", Role = "viewer" }, null);
        Assert.Equal(HttpMethod.Post, h.Requests[0].Method);
        Assert.Contains("\"password\":\"long-password-1\"", h.Bodies[0]);
        Assert.Equal(HttpMethod.Put, h.Requests[1].Method);
        Assert.EndsWith("api/users/7", h.Requests[1].RequestUri!.AbsoluteUri);
        Assert.DoesNotContain("password", h.Bodies[1]);
    }

    [Theory]
    [InlineData("192.168.1.50:8080", "http://192.168.1.50:8080/")]
    [InlineData("https://gate.example.org", "https://gate.example.org/")]
    [InlineData(" gate.local/console ", "http://gate.local/console/")]
    public void ServerAddressesAreNormalised(string input, string expected)
    {
        Assert.True(SessionHost.TryParseServer(input, out var uri));
        Assert.Equal(expected, uri.ToString());
    }

    [Theory]
    [InlineData("")]
    [InlineData("ftp://gate.local")]
    [InlineData("http://")]
    public void BadServerAddressesAreRejected(string input)
    {
        Assert.False(SessionHost.TryParseServer(input, out _));
    }

    [Fact]
    public void LiveFeedDropsReplayedFramesButKeepsTelemetry()
    {
        var feed = new LiveFeed(Base);
        var d = new LiveFrame { Type = "decision", EventId = 5, Decision = new LiveDecision { DecisionId = "u1" } };
        Assert.True(feed.FirstSighting(d));
        // After a gate-server restart the same decision may come back under a new event id.
        Assert.False(feed.FirstSighting(d with { EventId = 2 }));
        var t = new LiveFrame { Type = "telemetry", Telemetry = new Telemetry { GateId = "g" } };
        Assert.True(feed.FirstSighting(t));
        Assert.True(feed.FirstSighting(t));
    }
}
