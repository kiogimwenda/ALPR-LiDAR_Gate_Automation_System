// Models.cs — the dashboard backend's presentation schema.
//
// Mirrors dashboard/backend (json_mapping.cpp, history_store.cpp,
// controllers): camelCase fields, enum short names ("AUTHORIZED",
// "SEDAN"), timestamps as {ts: ISO-8601, tsMs: epoch millis}. Unknown
// fields are ignored, so a newer backend never breaks an older app.

using System.Text.Json.Serialization;

namespace GateControl.Core.Models;

public sealed record Stamp(string Ts, long TsMs)
{
    public DateTimeOffset Utc => DateTimeOffset.FromUnixTimeMilliseconds(TsMs);
    public DateTimeOffset Local => Utc.ToLocalTime();
}

// ---- live (WebSocket + /api/status) ----------------------------------------

public sealed record Telemetry
{
    public string GateId { get; init; } = "";
    public Stamp? Sent { get; init; }
    public string State { get; init; } = "UNKNOWN";
    public bool LimitOpen { get; init; }
    public bool LimitClosed { get; init; }
    public bool BeamClear { get; init; } = true;
    public long UptimeSec { get; init; }
    public int RssiDbm { get; init; }
    public double SupplyVoltage { get; init; }
    public double CoreTempC { get; init; }
    public string FwVersion { get; init; } = "";
}

public sealed record LiveDecision
{
    public string DecisionId { get; init; } = "";
    public Stamp? Decided { get; init; }
    public string GateId { get; init; } = "";
    public string Verdict { get; init; } = "";
    public string DenyReason { get; init; } = "";
    public string MatchedPlate { get; init; } = "";
    public string MatchedClass { get; init; } = "";
    public double Confidence { get; init; }
    public string Reason { get; init; } = "";
    public string Actor { get; init; } = "";
}

public sealed record Fault
{
    public string FaultId { get; init; } = "";
    public Stamp? Occurred { get; init; }
    public string GateId { get; init; } = "";
    public string Severity { get; init; } = "";
    public string Code { get; init; } = "";
    public string Description { get; init; } = "";
}

public sealed record GateCommand
{
    public string CommandId { get; init; } = "";
    public Stamp? Issued { get; init; }
    public string GateId { get; init; } = "";
    public string Kind { get; init; } = "";
    public string Actor { get; init; } = "";
}

public sealed record CommandAck
{
    public string CommandId { get; init; } = "";
    public bool Completed { get; init; }
    public bool Success { get; init; }
    public string Error { get; init; } = "";
    public string StateAfter { get; init; } = "";
}

// One WebSocket frame: {"eventId", "event": stamp, "type", <payload>}.
public sealed record LiveFrame
{
    public ulong EventId { get; init; }
    public Stamp? Event { get; init; }
    public string Type { get; init; } = "";
    public LiveDecision? Decision { get; init; }
    public Telemetry? Telemetry { get; init; }
    public Fault? Fault { get; init; }
    public GateCommand? Command { get; init; }
    public CommandAck? Ack { get; init; }
}

public sealed record GateSnapshot
{
    public Telemetry? Telemetry { get; init; }
    public Stamp? LastEvent { get; init; }
}

public sealed record StatusResponse
{
    public Dictionary<string, GateSnapshot> Gates { get; init; } = new();
    public bool StreamConnected { get; init; }
    public bool Upstream { get; init; }
}

public sealed record Health
{
    public string Status { get; init; } = "";
    public bool Upstream { get; init; }
    public string UpstreamAddr { get; init; } = "";
    public string SiteId { get; init; } = "";
}

// ---- auth ------------------------------------------------------------------

public sealed record LoginResponse
{
    public string Token { get; init; } = "";
    public string User { get; init; } = "";
    public string Role { get; init; } = "viewer";
    public string FullName { get; init; } = "";
    public int ExpiresInMin { get; init; }
}

public sealed record Me
{
    public string User { get; init; } = "";
    public string Role { get; init; } = "viewer";
    public string FullName { get; init; } = "";
    public string Email { get; init; } = "";
    public bool AuthRequired { get; init; }
}

// ---- history / reports ------------------------------------------------------

public sealed record Stats
{
    public long Captures { get; init; }
    public long UniqueVehicles { get; init; }
    public long Authorized { get; init; }
    public long Denied { get; init; }
    public long LowConfidence { get; init; }
    public long ManualReview { get; init; }
    public long Entries { get; init; }
    public long Exits { get; init; }
    public double AvgConfidence { get; init; }
    public int? PeakHour { get; init; }
    public long CurrentlyOnSite { get; init; }
    public double? AvgDwellMinutes { get; init; }
    public long UnreadAlerts { get; init; }
    public long Gates { get; init; }
    public long GatesOnline { get; init; }
}

public sealed record HourBucket
{
    public int Hour { get; init; }
    public long Captures { get; init; }
    public long UniqueVehicles { get; init; }
    public long Authorized { get; init; }
    public long Denied { get; init; }
    public long Entries { get; init; }
    public long Exits { get; init; }
}

public sealed record DayBucket
{
    public string Date { get; init; } = "";
    public long Captures { get; init; }
    public long UniqueVehicles { get; init; }
    public long Authorized { get; init; }
    public long Denied { get; init; }
}

public sealed record ClassCount(string VehicleClass, long Count);

public sealed record VerdictBreakdown
{
    public List<VerdictCount> ByVerdict { get; init; } = [];
    public List<ReasonCount> ByReason { get; init; } = [];
}

public sealed record VerdictCount(string Verdict, long Count);

public sealed record ReasonCount(string Reason, long Count);

public sealed record TopPlate
{
    public string Plate { get; init; } = "";
    public long Visits { get; init; }
    public Stamp? FirstSeen { get; init; }
    public Stamp? LastSeen { get; init; }
    public long Days { get; init; }
    public string LastVerdict { get; init; } = "";
}

public sealed record DwellCategory(string Category, long Count);

public sealed record SessionRow
{
    public string Plate { get; init; } = "";
    public Stamp? Entered { get; init; }
    public Stamp? Exited { get; init; }
    public long Minutes { get; init; }
    public long MinutesOnSite { get; init; }
    public string EntryGate { get; init; } = "";
    public string ExitGate { get; init; } = "";
    public bool OnSite { get; init; }

    // On-site rows report time so far; completed visits their length.
    public string DurationText =>
        Formatting.Display.Minutes(MinutesOnSite > 0 || Exited is null ? MinutesOnSite : Minutes);
}

public sealed record SessionsReport
{
    public long Completed { get; init; }
    public double? AvgMinutes { get; init; }
    public List<DwellCategory> Categories { get; init; } = [];
    public List<SessionRow> Longest { get; init; } = [];
    public List<SessionRow> OnSite { get; init; } = [];
}

public sealed record Decision
{
    public long Id { get; init; }
    public string DecisionId { get; init; } = "";
    public Stamp? Decided { get; init; }
    public string GateId { get; init; } = "";
    public string GateName { get; init; } = "";
    public string Plate { get; init; } = "";
    public string Verdict { get; init; } = "";
    public string DenyReason { get; init; } = "";
    public string VehicleClass { get; init; } = "";
    public double Confidence { get; init; }
    public string Reason { get; init; } = "";
    public string Actor { get; init; } = "";
    public string Direction { get; init; } = "both";
}

public sealed record DecisionPage
{
    public long Total { get; init; }
    public List<Decision> Items { get; init; } = [];
    public int Offset { get; init; }
}

public sealed record RejectedReport
{
    public long Total { get; init; }
    public List<ReasonCount> Breakdown { get; init; } = [];
    public List<Decision> Items { get; init; } = [];
}

public sealed record PlateHistory
{
    public string Plate { get; init; } = "";
    public long Visits { get; init; }
    public Stamp? FirstSeen { get; init; }
    public Stamp? LastSeen { get; init; }
    public long Authorized { get; init; }
    public long Days { get; init; }
    public List<Decision> History { get; init; } = [];
    public List<SessionRow> Sessions { get; init; } = [];
}

// ---- alerts / gates / users / settings ----------------------------------------

public sealed record Alert
{
    public long Id { get; init; }
    public Stamp? Raised { get; init; }
    public string Kind { get; init; } = "";
    public string Severity { get; init; } = "";
    public string GateId { get; init; } = "";
    public string Plate { get; init; } = "";
    public string Message { get; init; } = "";
    public string? AcknowledgedBy { get; init; }
    public Stamp? Acknowledged { get; init; }
    public bool IsAcknowledged => Acknowledged is not null;
}

public sealed record AlertPage
{
    public long Total { get; init; }
    public long Unread { get; init; }
    public List<Alert> Items { get; init; } = [];
}

public sealed record GateInfo
{
    public string GateId { get; init; } = "";
    public string Name { get; init; } = "";
    public string Location { get; init; } = "";
    public string Direction { get; init; } = "both";
    public Stamp? LastSeen { get; init; }
    public bool Online { get; init; }
    public string LastState { get; init; } = "";
    public string FwVersion { get; init; } = "";
    public string DisplayName => string.IsNullOrWhiteSpace(Name) ? GateId : Name;
}

public sealed record UserAccount
{
    public long Id { get; init; }
    public string Username { get; init; } = "";
    public string FullName { get; init; } = "";
    public string Email { get; init; } = "";
    public string Role { get; init; } = "viewer";
    public bool Active { get; init; } = true;
    public long CreatedMs { get; init; }
    public long? LastLoginMs { get; init; }
}

public sealed record SiteSettings
{
    public int RetentionDays { get; init; } = 90;
    public int SessionTimeoutMinutes { get; init; } = 720;
}

// ---- allowlist -------------------------------------------------------------------

public sealed record TimeWindow(int StartMinute, int EndMinute, int DaysMask);

public sealed record AllowlistEntry
{
    public string Plate { get; init; } = "";
    public List<string> AllowedClasses { get; init; } = [];
    public List<TimeWindow> TimeWindows { get; init; } = [];
    public string OwnerName { get; init; } = "";
    public string OwnerUnit { get; init; } = "";
    public Stamp? ValidFrom { get; init; }
    public Stamp? ValidUntil { get; init; }
    public string Notes { get; init; } = "";
    public string AddedBy { get; init; } = "";
    public Stamp? Added { get; init; }
}

public sealed record AllowlistPage
{
    public List<AllowlistEntry> Entries { get; init; } = [];
    public string NextPageToken { get; init; } = "";
}

public sealed record UpsertResult(int Inserted, int Updated);

// Request body for POST /api/allowlist (validFromMs / validUntilMs are
// write-side names on the backend).
public sealed record AllowlistUpsert
{
    public string Plate { get; init; } = "";
    public List<string> AllowedClasses { get; init; } = [];
    public List<TimeWindow> TimeWindows { get; init; } = [];
    public string OwnerName { get; init; } = "";
    public string OwnerUnit { get; init; } = "";
    public string Notes { get; init; } = "";
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public long? ValidFromMs { get; init; }
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public long? ValidUntilMs { get; init; }
}
