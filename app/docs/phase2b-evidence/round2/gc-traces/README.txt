EventPipe GC traces of the first tab entrance after launch (dotnet-trace, Microsoft-Windows-DotNETRuntime:0x1:5),
parsed with app/tools/motion-capture/trace-parser (GCTriggered = the triggering thread's stack).

Before (each one a blocking or background gen-2 inside the entrance):
  (2026-10-24, not kept as a file) GCTriggered InducedNotForced from MAUI TransformationExtensions.UpdateTransformation
             (a new WinUI CompositeTransform per write, native memory pressure). Fixed: Platform/WinTransform.cs.
  fix2.txt   GCTriggered Induced from IReferenceTrackerHost.DisconnectUnusedReferenceSources (WinUI), with the MAUI
             blob clip rebuilding WrapperView geometry every frame. Fixed: Motion/BlobClip.cs native composition clip.
  wa.txt     InducedNotForced from W2DCanvas.DrawString -> new WinUI FontFamily per label per frame (chart axis labels),
             plus the WinUI-induced blocking gen-2. Fixed: Controls/ChartView.cs Text() with cached CanvasTextFormat.
  x1.txt     no in-app frame trace attached: the WinUI-induced gen-2 still happens (not caused by the harness).
  r2.txt     Reduce Motion on: still happens (not motion-specific).
  Cause of the remaining one: WinUI 1.8 CCoreServices::TrackMemory (xcpcore.cpp, release/1.8.8) checks native XAML
  memory every 50 render-walk frames and calls a blocking GC when it grew > 50 MB since the last low point; an idle
  app renders almost no frames, so the check fell inside the first entrance. Fixed: RootPage.SettleXamlCleanup
  (draw the parked tabs off-window for 120 frames at idle after warm-up).
After: u1..u4 (SustainedLowLatency still on), z1, z2 (SustainedLowLatency removed): the files are empty because the
8 s window holds no GC event at all; frame traces: no frame over 28 ms.
FR3.txt    full acceptance-style run (40 s): one background gen-2, GCTriggered InducedNotForced from the
           CompositionTarget.Rendering event's per-frame WinRT event-args wrapper (CreateRcwForComObject).
           The harness's own second Rendering handler was removed (it now samples on the app ticker);
           production keeps one handler, so this pressure source remains at half the old rate.
