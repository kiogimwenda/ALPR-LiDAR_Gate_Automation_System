// trace-parser — GC events from an EventPipe trace (dotnet-trace, Microsoft-Windows-DotNETRuntime:0x1:5):
// GCTriggered with the triggering thread's stack, GCStart reason / depth / type, heap stats.
// Usage: dotnet run --project trace-parser -c Release -- <file.nettrace>
using Microsoft.Diagnostics.Tracing;
using Microsoft.Diagnostics.Tracing.Etlx;
using Microsoft.Diagnostics.Tracing.Parsers.Clr;
var etlx = TraceLog.CreateFromEventPipeDataFile(args[0]);
using var log = new TraceLog(etlx);
var src = log.Events.GetSource();
long allocSinceGc = 0;
var ticks = new List<(double T, string Type, long Bytes, string Stack)>();
src.Clr.GCAllocationTick += e => {
    allocSinceGc += e.AllocationAmount64;
    ticks.Add((e.TimeStampRelativeMSec, e.TypeName, e.AllocationAmount64, Stack(e)));
};
src.Clr.GCStart += e => {
    Console.WriteLine($"{e.TimeStampRelativeMSec,10:F1} ms GCStart #{e.Count} depth {e.Depth} reason {e.Reason} type {e.Type}  alloc-ticks since last GC {allocSinceGc / 1024} KB");
    if (e.Depth == 2) {
        Console.WriteLine("   stack: " + Stack(e).Replace("\n", "\n          "));
        foreach (var t in ticks.TakeLast(0)) Console.WriteLine($"   tick {t.T,10:F1} {t.Type} {t.Bytes/1024} KB\n          {t.Stack.Replace("\n", "\n          ")}");
    }
    allocSinceGc = 0; ticks.Clear();
};
src.Clr.GCStop += e => Console.WriteLine($"{e.TimeStampRelativeMSec,10:F1} ms GCEnd   #{e.Count} depth {e.Depth}");
src.Clr.GCHeapStats += e => Console.WriteLine($"            heap gen0 {e.GenerationSize0/1048576.0:F1} gen1 {e.GenerationSize1/1048576.0:F1} gen2 {e.GenerationSize2/1048576.0:F1} LOH {e.GenerationSize3/1048576.0:F1} POH {e.GenerationSize4/1048576.0:F1} MB, pinned {e.PinnedObjectCount}");
src.Clr.GCGlobalHeapHistory += e => Console.WriteLine($"            global: condemned gen {e.CondemnedGeneration} gen0 reduction {e.Gen0ReductionCount} reason {e.Reason} mode {e.PauseMode} mem pressure {e.MemoryPressure}%");
src.Clr.GCTriggered += e => Console.WriteLine($"{e.TimeStampRelativeMSec,10:F1} ms GCTriggered reason {e.Reason} thread {e.ThreadID}\n   stack: " + Stack(e).Replace("\n", "\n          "));
src.Process();
static string Stack(TraceEvent e) {
    var cs = e.CallStack(); var lines = new List<string>(); int n = 0;
    while (cs is not null && n++ < 40) { lines.Add(cs.CodeAddress.FullMethodName is { Length: > 0 } m ? m : $"{cs.CodeAddress.ModuleName}!0x{cs.CodeAddress.Address:x}"); cs = cs.Caller; }
    return lines.Count == 0 ? "(no stack)" : string.Join("\n", lines);
}
