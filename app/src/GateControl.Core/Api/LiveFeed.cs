// LiveFeed.cs — the /ws/events stream with automatic reconnect.
//
// The backend replays its recent ring on every connect and then streams
// live frames. The feed de-duplicates by event id (a reconnect replays
// frames the app already showed) and backs off 1 s → 15 s while the
// backend is unreachable. Frames are raised on a background thread;
// view models marshal to the UI through IUiDispatcher.

using System.Net.WebSockets;
using System.Text;
using System.Text.Json;
using GateControl.Core.Models;

namespace GateControl.Core.Api;

public enum FeedState
{
    Connecting,
    Live,
    Reconnecting,
    Stopped,
}

public sealed class LiveFeed : IAsyncDisposable
{
    private readonly Uri _wsUri;
    private readonly Func<ClientWebSocket> _socketFactory;
    private readonly HashSet<string> _seen = new(StringComparer.Ordinal);
    private readonly Queue<string> _seenOrder = new();
    private CancellationTokenSource? _cts;
    private Task? _loop;

    public LiveFeed(Uri httpBase, Func<ClientWebSocket>? socketFactory = null)
    {
        var b = new UriBuilder(httpBase)
        {
            Scheme = httpBase.Scheme == Uri.UriSchemeHttps ? "wss" : "ws",
            Path = httpBase.AbsolutePath.TrimEnd('/') + "/ws/events",
        };
        _wsUri = b.Uri;
        _socketFactory = socketFactory ?? (() => new ClientWebSocket());
    }

    public FeedState State { get; private set; } = FeedState.Stopped;

    public event EventHandler<LiveFrame>? FrameReceived;
    public event EventHandler<FeedState>? StateChanged;

    public void Start()
    {
        if (_loop is not null)
        {
            return;
        }
        _cts = new CancellationTokenSource();
        _loop = Task.Run(() => RunAsync(_cts.Token));
    }

    public async Task StopAsync()
    {
        if (_cts is null)
        {
            return;
        }
        await _cts.CancelAsync().ConfigureAwait(false);
        try
        {
            if (_loop is not null)
            {
                await _loop.ConfigureAwait(false);
            }
        }
        catch (OperationCanceledException)
        {
        }
        _cts.Dispose();
        _cts = null;
        _loop = null;
        SetState(FeedState.Stopped);
    }

    public ValueTask DisposeAsync() => new(StopAsync());

    // The frame key used for de-duplication across reconnect replays: event
    // ids restart with gate-server, so the payload's own UUID is preferred.
    internal static string KeyOf(LiveFrame f) => f.Type switch
    {
        "decision" when f.Decision is { DecisionId.Length: > 0 } d => "d:" + d.DecisionId,
        "fault" when f.Fault is { FaultId.Length: > 0 } x => "f:" + x.FaultId,
        "command" when f.Command is { CommandId.Length: > 0 } c => "c:" + c.CommandId,
        "ack" when f.Ack is { CommandId.Length: > 0 } a => $"a:{a.CommandId}:{a.Completed}",
        _ => $"e:{f.EventId}:{f.Event?.TsMs}",
    };

    // True the first time a key is seen (bounded memory: last 2048 keys).
    internal bool FirstSighting(LiveFrame f)
    {
        if (f.Type == "telemetry")
        {
            return true;  // state snapshots: always apply the latest
        }
        var key = KeyOf(f);
        if (!_seen.Add(key))
        {
            return false;
        }
        _seenOrder.Enqueue(key);
        while (_seenOrder.Count > 2048)
        {
            _seen.Remove(_seenOrder.Dequeue());
        }
        return true;
    }

    private async Task RunAsync(CancellationToken ct)
    {
        var backoff = TimeSpan.FromSeconds(1);
        SetState(FeedState.Connecting);
        while (!ct.IsCancellationRequested)
        {
            using var ws = _socketFactory();
            try
            {
                ws.Options.KeepAliveInterval = TimeSpan.FromSeconds(20);
                await ws.ConnectAsync(_wsUri, ct).ConfigureAwait(false);
                SetState(FeedState.Live);
                backoff = TimeSpan.FromSeconds(1);
                await ReceiveAsync(ws, ct).ConfigureAwait(false);
            }
            catch (OperationCanceledException) when (ct.IsCancellationRequested)
            {
                break;
            }
            catch (Exception ex) when (ex is WebSocketException or IOException or HttpRequestException)
            {
                // fall through to the backoff
            }
            if (ct.IsCancellationRequested)
            {
                break;
            }
            SetState(FeedState.Reconnecting);
            try
            {
                await Task.Delay(backoff, ct).ConfigureAwait(false);
            }
            catch (OperationCanceledException)
            {
                break;
            }
            backoff = TimeSpan.FromSeconds(Math.Min(15, backoff.TotalSeconds * 2));
        }
    }

    private async Task ReceiveAsync(ClientWebSocket ws, CancellationToken ct)
    {
        var buffer = new byte[16 * 1024];
        using var message = new MemoryStream();
        while (ws.State == WebSocketState.Open && !ct.IsCancellationRequested)
        {
            var result = await ws.ReceiveAsync(buffer, ct).ConfigureAwait(false);
            if (result.MessageType == WebSocketMessageType.Close)
            {
                return;
            }
            message.Write(buffer, 0, result.Count);
            if (!result.EndOfMessage)
            {
                continue;
            }
            var text = Encoding.UTF8.GetString(message.GetBuffer(), 0, (int)message.Length);
            message.SetLength(0);
            if (text == "pong")
            {
                continue;
            }
            LiveFrame? frame;
            try
            {
                frame = JsonSerializer.Deserialize<LiveFrame>(text, GateApi.Json);
            }
            catch (JsonException)
            {
                continue;  // a malformed frame must not kill the feed
            }
            if (frame is not null && FirstSighting(frame))
            {
                FrameReceived?.Invoke(this, frame);
            }
        }
    }

    private void SetState(FeedState s)
    {
        if (State == s)
        {
            return;
        }
        State = s;
        StateChanged?.Invoke(this, s);
    }
}
