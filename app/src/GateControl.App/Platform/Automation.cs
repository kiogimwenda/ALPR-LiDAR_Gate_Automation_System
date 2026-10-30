// Automation.cs — DEBUG-only verification harness (spec §12 step 10).
//
// Drives the app without touching the mouse, so motion can be filmed with
// an external frame grabber while someone keeps using the machine. Off
// unless GC_AUTOMATION_FILE names a command file; the app polls it every
// 15 ms, runs each line, deletes it, and appends "<unix ms> <command>" to
// <file>.log so frames can be lined up with the moment a command ran.
//
//   tab <0-4>            switch tab (as tapping the nav capsule)
//   theme light|dark     theme reveal from the rail button
//   themepress           pointer-down on a theme button (snapshot early)
//   reduce on|off|os     force Reduce Motion on/off, or back to the OS
//   push settings|users|gates|data   push a page; "pop" goes back
//   plate <PLATE>        open a plate's history
//   size <w> <h>         resize the window (dip)
//   info                 log window / page sizes
//   gate <id> <open|close|hold|lockdown|release>   a gate tile's command
//   signout              sign out at once (no confirmation)
//   period <0-2>         Dashboard period (Today / 7 days / 30 days)
//   trace <ms> <name>    frame-exact trace of the next entrance (see StartTrace)
//   tree <depth>         dump the shell's layout tree

#if DEBUG || GC_HARNESS
using GateControl.Core.Services;
using GateControl.Motion;
using GateControl.Pages;

namespace GateControl.Platform;

public static class Automation
{
    public static void Start(Window window, IServiceProvider services)
    {
#if WINDOWS
        // Measurements stand in for a person looking at the app, i.e. a
        // foreground window. Windows throttles the execution speed of a
        // background process (EcoQoS), which slowed the in-process render
        // thread to 80–95 ms frames while the test window sat behind other
        // apps. The harness opts its own process out (harness builds only;
        // nothing system-wide changes).
        OptOutOfPowerThrottling();
#endif
        // Allocate the trace buffers now, not at the first trace.
        _ = ItemBuf.Capacity + FrameBuf.Capacity + TickBuf.Capacity + EventBuf.Capacity;
        var path = Environment.GetEnvironmentVariable("GC_AUTOMATION_FILE");
        if (string.IsNullOrEmpty(path) || Application.Current?.Dispatcher is not { } dispatcher)
        {
            return;
        }
        // Crash forensics while driving the app.
        AppDomain.CurrentDomain.FirstChanceException += (_, e) =>
        {
            try
            {
                File.AppendAllText(path + ".exceptions", $"{DateTimeOffset.UtcNow.ToUnixTimeMilliseconds()} {e.Exception.GetType().Name}: {e.Exception.Message}\n{e.Exception.StackTrace}\n\n");
            }
            catch (IOException)
            {
            }
        };
        var timer = dispatcher.CreateTimer();
        timer.Interval = TimeSpan.FromMilliseconds(15);
        timer.Tick += async (_, _) =>
        {
            if (!File.Exists(path))
            {
                return;
            }
            string[] lines;
            try
            {
                lines = File.ReadAllLines(path);
                File.Delete(path);
            }
            catch (IOException)
            {
                return;  // still being written; next tick
            }
            foreach (var line in lines.Where(l => !string.IsNullOrWhiteSpace(l)))
            {
                File.AppendAllText(path + ".log", $"{DateTimeOffset.UtcNow.ToUnixTimeMilliseconds()} {line}\n");
                try
                {
                    await RunAsync(window, services, line.Trim().Split(' '));
                }
                catch (Exception e)
                {
                    File.AppendAllText(path + ".log", $"  error: {e.Message}\n");
                }
            }
        };
        timer.Start();
    }

#if WINDOWS
    [System.Runtime.InteropServices.StructLayout(System.Runtime.InteropServices.LayoutKind.Sequential)]
    private struct PowerThrottlingState
    {
        public uint Version;
        public uint ControlMask;
        public uint StateMask;
    }

    [System.Runtime.InteropServices.DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool SetProcessInformation(IntPtr process, int infoClass, ref PowerThrottlingState info, uint size);

    [System.Runtime.InteropServices.DllImport("kernel32.dll")]
    private static extern IntPtr GetCurrentProcess();

    private static void OptOutOfPowerThrottling()
    {
        // ProcessPowerThrottling = 4; PROCESS_POWER_THROTTLING_EXECUTION_SPEED = 1; state 0 = never throttle.
        var state = new PowerThrottlingState { Version = 1, ControlMask = 1, StateMask = 0 };
        var ok = SetProcessInformation(GetCurrentProcess(), 4, ref state,
            (uint)System.Runtime.InteropServices.Marshal.SizeOf<PowerThrottlingState>());
        var log = Environment.GetEnvironmentVariable("GC_AUTOMATION_FILE");
        if (log is not null)
        {
            File.AppendAllText(log + ".log", $"{DateTimeOffset.UtcNow.ToUnixTimeMilliseconds()} power throttling opt-out: {ok}\n");
        }
    }
#endif

    // Trace buffers, allocated once when the harness starts and reused. A
    // fresh ~10 MB buffer per trace went to the large-object heap and tipped
    // the GC into a full collection during the first traced entrance of each
    // launch: the harness, not the app, caused that long frame.
    private static readonly List<(double T, int G0, int G1, int G2)> FrameBuf = new(4096);
    private static readonly List<(int Id, double T, double O, double Y, double S, bool C)> ItemBuf = new(200_000);
    private static readonly System.Text.StringBuilder TickBuf = new(1 << 17);
    private static readonly System.Text.StringBuilder EventBuf = new(1 << 18);

    // Frame-exact trace of an entrance: every rendered frame's time, and the
    // opacity / rise / scale of every element the entrance animates.
    // Writes %TEMP%\gc-<name>-frames.csv and gc-<name>-items.csv.
    private static void StartTrace(Window window, int ms, string name)
    {
        var dir = Path.GetTempPath();
        var frames = new System.Text.StringBuilder("frame,ms,gc0,gc1,gc2\n");
        var items = new System.Text.StringBuilder("item,kind,delay_ms,frame_ms,opacity,translation_y,scale,clip\n");
        var tracked = new List<(VisualElement View, string Kind, double Delay, int Id)>();
        var clock = System.Diagnostics.Stopwatch.StartNew();
        var n = 0;
        var ticks = TickBuf.Clear().Append("ms,tick_ms,views,callbacks,slowest_ms,slowest\n");
        var events = EventBuf.Clear().Append("ms,dur_ms,event\n");
        FrameTrace.Begin(clock, events);
#if WINDOWS
        // WinUI layout passes and every element added under the shell.
        var rootElement = (window.Handler?.PlatformView as Microsoft.UI.Xaml.Window)?.Content as Microsoft.UI.Xaml.FrameworkElement;
        EventHandler<object> onLayout = (_, _) => FrameTrace.Mark("winui.layout");
        if (rootElement is not null)
        {
            rootElement.LayoutUpdated += onLayout;
        }
#endif
        EventHandler<ElementEventArgs> onAdded = (_, _) => FrameTrace.Mark("element.added");
        window.DescendantAdded += onAdded;
        Spr.TickTrace = (d, views, callbacks) => ticks.Append(
            $"{clock.Elapsed.TotalMilliseconds:F2},{d:F3},{views},{callbacks},{Spr.SlowestCallbackMs:F3},{Spr.SlowestCallback}\n");
        Reveal.Traced = (v, delay, kind) =>
        {
            if (tracked.All(t => !ReferenceEquals(t.View, v)))
            {
                tracked.Add((v, kind, delay, tracked.Count));
            }
        };
        // Samples go into preallocated buffers and are formatted at the end:
        // building strings every frame made the trace itself trigger
        // collections mid-entrance.
        var frameBuf = FrameBuf;
        var itemBuf = ItemBuf;
        frameBuf.Clear();
        itemBuf.Clear();
        void Sample()
        {
            var t = clock.Elapsed.TotalMilliseconds;
            frameBuf.Add((t, GC.CollectionCount(0), GC.CollectionCount(1), GC.CollectionCount(2)));
            foreach (var (v, _, _, id) in tracked)
            {
                itemBuf.Add((id, t, v.Opacity, v.TranslationY, v.Scale, GateControl.Motion.BlobClip.IsClipped(v)));
            }
        }
        void Format()
        {
            foreach (var f in frameBuf)
            {
                frames.Append($"{n++},{f.T:F2},{f.G0},{f.G1},{f.G2}\n");
            }
            foreach (var it in itemBuf)
            {
                var (_, kind, delay, _) = tracked[it.Id];
                items.Append($"{it.Id},{kind},{delay * 1000:F0},{it.T:F2},{it.O:F4},{it.Y:F3},{it.S:F4},{(it.C ? 1 : 0)}\n");
            }
        }
        void Finish()
        {
            Format();
            Reveal.Traced = null;
            Spr.TickTrace = null;
            FrameTrace.End();
            window.DescendantAdded -= onAdded;
#if WINDOWS
            if (rootElement is not null)
            {
                rootElement.LayoutUpdated -= onLayout;
            }
#endif
            File.WriteAllText(Path.Combine(dir, $"gc-{name}-events.csv"), events.ToString());
            File.WriteAllText(Path.Combine(dir, $"gc-{name}-ticks.csv"), ticks.ToString());
            File.WriteAllText(Path.Combine(dir, $"gc-{name}-frames.csv"), frames.ToString());
            File.WriteAllText(Path.Combine(dir, $"gc-{name}-items.csv"), items.ToString());
        }
        // Sampled on the app's own frame ticker (Spr: CompositionTarget.Rendering
        // on Windows), kept running for the trace: a second Rendering handler
        // doubled the per-frame WinRT event-args wrappers whose memory
        // pressure induces background gen-2 collections (EventPipe:
        // GCTriggered InducedNotForced from the Rendering event's
        // CreateRcwForComObject).
        Spr.OnFrame(_ =>
        {
            Sample();
            if (clock.ElapsedMilliseconds <= ms)
            {
                return true;
            }
            Finish();
            return false;
        });
    }

    private static async Task RunAsync(Window window, IServiceProvider services, string[] cmd)
    {
        var nav = window.Page as NavigationPage;
        var root = nav?.RootPage as RootPage;
        switch (cmd[0])
        {
            case "tab":
                root?.Select(int.Parse(cmd[1], System.Globalization.CultureInfo.InvariantCulture));
                break;
#if WINDOWS
            case "parity" when nav is not null:
                {
                    // WinTransform vs stock MAUI (review round 3, items 3–4): the same
                    // page built with either path, for screenshots to compare.
                    WinTransform.Enabled = cmd.Length < 2 || cmd[1] != "maui";
                    await nav.PushAsync(ParityPage.Create(), false);
                    break;
                }
            case "parityclip" when nav?.CurrentPage is ContentPage { Content: Layout pl }:
                {
                    // The late container: the moving box gets a Clip (a WrapperView)
                    // after its first animated write, then moves again.
                    if (pl.Children.OfType<BoxView>().FirstOrDefault(b => b.AutomationId == "late") is { } late)
                    {
                        late.Clip = new Microsoft.Maui.Controls.Shapes.RoundRectangleGeometry(new CornerRadius(12), new Rect(0, 0, 80, 80));
                        Spr.Set(late, Prop.TranslationY, 15);
                        Spr.Set(late, Prop.TranslationX, 40);
                    }
                    break;
                }
            case "tabstops":
                {
                    // Every control Tab / arrow keys could land on (IsTabStop, enabled,
                    // visible), split by whether it lies in a parked tab.
                    var log = Environment.GetEnvironmentVariable("GC_AUTOMATION_FILE")! + ".log";
                    var top = (window.Handler?.PlatformView as Microsoft.UI.Xaml.Window)?.Content;
                    int active = 0, parked = 0;
                    var names = new List<string>();
                    void Walk(Microsoft.UI.Xaml.DependencyObject e, Controls.ParkPanel? panel)
                    {
                        if (e is Controls.ParkPanel pp) { panel = pp; }
                        if (e is Microsoft.UI.Xaml.UIElement { Visibility: Microsoft.UI.Xaml.Visibility.Collapsed }) { return; }
                        if (e is Microsoft.UI.Xaml.UIElement { IsTabStop: true } c &&
                            (e is not Microsoft.UI.Xaml.Controls.Control ctl || ctl.IsEnabled))
                        {
                            if (panel?.IsParked == true) { parked++; names.Add(c.GetType().Name); } else { active++; }
                        }
                        var n = Microsoft.UI.Xaml.Media.VisualTreeHelper.GetChildrenCount(e);
                        for (var i = 0; i < n; i++) { Walk(Microsoft.UI.Xaml.Media.VisualTreeHelper.GetChild(e, i), panel); }
                    }
                    if (top is not null) { Walk(top, null); }
                    File.AppendAllText(log, $"  tabstops (tab {root?.CurrentTab}): {active} reachable outside parked tabs, {parked} inside parked tabs {string.Join(",", names.Take(12))}\n");
                    break;
                }
            case "focuswalk":
                {
                    // Tab order from the current focus, N steps, forward (Tab) and
                    // then backward (Shift+Tab): each focused element, and whether
                    // it lies inside a parked tab (it must never).
                    var log = Environment.GetEnvironmentVariable("GC_AUTOMATION_FILE")! + ".log";
                    var xr = (window.Handler?.PlatformView as Microsoft.UI.Xaml.Window)?.Content?.XamlRoot;
                    var steps = int.Parse(cmd.Length > 1 ? cmd[1] : "200", System.Globalization.CultureInfo.InvariantCulture);
                    var inParked = 0;
                    var seen = new HashSet<object>();
                    var sw = System.Diagnostics.Stopwatch.StartNew();
                    // Start where a keyboard user starts: the first focusable control.
                    if (xr?.Content is { } content && Microsoft.UI.Xaml.Input.FocusManager.FindFirstFocusableElement(content) is Microsoft.UI.Xaml.UIElement first0)
                    {
                        (first0 as Microsoft.UI.Xaml.Controls.Control)?.Focus(Microsoft.UI.Xaml.FocusState.Keyboard);
                    }
                    foreach (var dir in new[] { Microsoft.UI.Xaml.Input.FocusNavigationDirection.Next, Microsoft.UI.Xaml.Input.FocusNavigationDirection.Previous })
                    {
                        for (var i = 0; i < steps && xr is not null; i++)
                        {
                            Microsoft.UI.Xaml.Input.FocusManager.TryMoveFocus(dir, new Microsoft.UI.Xaml.Input.FindNextElementOptions { SearchRoot = xr.Content });
                            if (Microsoft.UI.Xaml.Input.FocusManager.GetFocusedElement(xr) is not Microsoft.UI.Xaml.DependencyObject f)
                            {
                                continue;
                            }
                            seen.Add(f);
                            Controls.ParkPanel? panel = null;
                            for (var cur = f; cur is not null; cur = Microsoft.UI.Xaml.Media.VisualTreeHelper.GetParent(cur))
                            {
                                if (cur is Controls.ParkPanel pp) { panel = pp; break; }
                            }
                            if (panel?.IsParked == true)
                            {
                                inParked++;
                                var chain = new List<string>();
                                for (var cur = f; cur is not null && chain.Count < 40; cur = Microsoft.UI.Xaml.Media.VisualTreeHelper.GetParent(cur))
                                {
                                    chain.Add(cur.GetType().Name);
                                }
                                File.AppendAllText(log, $"  focus IN PARKED TAB: {f.GetType().Name} {Microsoft.UI.Xaml.Automation.AutomationProperties.GetName(f)} via {string.Join(" < ", chain)}\n");
                                break;
                            }
                        }
                    }
                    File.AppendAllText(log, $"  focuswalk {steps}x2 steps: {seen.Count} distinct elements, {inParked} in parked tabs (tab {root?.CurrentTab}), {sw.ElapsedMilliseconds} ms\n");
                    break;
                }
#endif
            case "themeinfo":
                {
                    var host = root?.Content as Layout;
                    var overlays = host?.Children.OfType<Image>().Count(i => i.InputTransparent) ?? -1;
                    File.AppendAllText(Environment.GetEnvironmentVariable("GC_AUTOMATION_FILE")! + ".log",
                        $"  themeinfo requested {Application.Current?.RequestedTheme} user {Application.Current?.UserAppTheme} " +
                        $"busy {ThemeReveal.Busy} overlays {overlays} caption {(window.TitleBar as TitleBar)?.BackgroundColor?.ToHex()} " +
                        $"saved {(GateControl.Core.ViewModels.Appearance)Preferences.Default.Get("appearance", (int)GateControl.Core.ViewModels.Appearance.System)}\n");
                    break;
                }
            case "themecancel":
                // The pointer events / window deactivation a cancelled press raises.
                root?.CancelThemePress(cmd.Length > 1 ? cmd[1] : "test");
                break;
            case "themepress":
                root?.PrepareTheme();
                break;
            case "theme":
                root?.ToggleTheme(cmd[1] == "dark" ? AppTheme.Dark : AppTheme.Light);
                break;
            case "reduce":
                MotionSettings.Override = cmd[1] switch { "on" => true, "off" => false, _ => null };
                break;
            case "push" when nav is not null:
                await nav.PushAsync((cmd.Length > 1 ? cmd[1] : "settings") switch
                {
                    "users" => services.GetRequiredService<UsersPage>(),
                    "gates" => services.GetRequiredService<GatesPage>(),
                    "data" => (Page)services.GetRequiredService<DataPage>(),
                    _ => services.GetRequiredService<SettingsPage>(),
                });
                break;
            case "pop" when nav is not null && nav.Navigation.NavigationStack.Count > 1:
                await nav.PopAsync();
                break;
            case "poproot" when nav is not null:
                await nav.PopToRootAsync();
                break;
            case "plate":
                await services.GetRequiredService<INavigator>().ShowPlateAsync(cmd[1]);
                break;
            case "info":
                var path = Environment.GetEnvironmentVariable("GC_AUTOMATION_FILE")!;
                File.AppendAllText(path + ".log",
                    $"  window {window.Width}x{window.Height} page {window.Page?.Width}x{window.Page?.Height} " +
                    $"root {root?.Width}x{root?.Height} content {(root?.Content as VisualElement)?.Height} tab {root?.CurrentTab}\n  {root?.DebugSizes}\n");
                File.AppendAllText(path + ".log",
                    $"  gc mode {System.Runtime.GCSettings.LatencyMode} server {System.Runtime.GCSettings.IsServerGC} " +
                    $"allocated {GC.GetTotalAllocatedBytes() / 1048576} MB counts {GC.CollectionCount(0)}/{GC.CollectionCount(1)}/{GC.CollectionCount(2)}\n");
                foreach (var kind in new[] { GCKind.Ephemeral, GCKind.FullBlocking, GCKind.Background })
                {
                    var g = GC.GetGCMemoryInfo(kind);
                    File.AppendAllText(path + ".log",
                        $"  gc {kind}: #{g.Index} gen{g.Generation} compacted {g.Compacted} concurrent {g.Concurrent} " +
                        $"pause {string.Join("/", g.PauseDurations.ToArray().Select(d => d.TotalMilliseconds.ToString("0.0", System.Globalization.CultureInfo.InvariantCulture)))} ms " +
                        $"heap {g.HeapSizeBytes / 1048576} MB committed {g.TotalCommittedBytes / 1048576} MB load {g.MemoryLoadBytes / 1048576}/{g.HighMemoryLoadThresholdBytes / 1048576} MB\n");
                }
                break;
            case "tree":
                {
                    var log = Environment.GetEnvironmentVariable("GC_AUTOMATION_FILE")! + ".log";
                    void Dump(IVisualTreeElement e, int depth)
                    {
                        if (depth > int.Parse(cmd.Length > 1 ? cmd[1] : "6", System.Globalization.CultureInfo.InvariantCulture))
                        {
                            return;
                        }
                        if (e is VisualElement v)
                        {
                            File.AppendAllText(log, $"{new string(' ', depth * 2)}{v.GetType().Name} {v.Bounds} desired {v.DesiredSize} wr {v.WidthRequest}\n");
                        }
                        foreach (var c in e.GetVisualChildren())
                        {
                            Dump(c, depth + 1);
                        }
                    }
                    if (root?.CurrentPage is { } pageNow && root.Content is IVisualTreeElement)
                    {
                        // the hosted content of the current tab
                        foreach (var c in ((IVisualTreeElement)root).GetVisualChildren())
                        {
                            Dump(c, 0);
                        }
                    }
                    break;
                }
            case "trace":
                StartTrace(window, int.Parse(cmd.Length > 1 ? cmd[1] : "1500", System.Globalization.CultureInfo.InvariantCulture),
                           cmd.Length > 2 ? cmd[2] : "trace");
                break;
            case "gate" when root?.CurrentPage is LivePage live:
                {
                    // gate <id> open|close|hold|lockdown|release — the tile's own commands
                    var tile = live.Vm.Gates.First(g => g.GateId == cmd[1]);
                    var task = cmd[2] switch
                    {
                        "open" => tile.OpenCommand.ExecuteAsync(null),
                        "close" => tile.CloseCommand.ExecuteAsync(null),
                        "hold" => tile.HoldOpenCommand.ExecuteAsync(null),
                        "lockdown" => tile.LockdownCommand.ExecuteAsync(null),
                        _ => tile.ReleaseCommand.ExecuteAsync(null),
                    };
                    await task;
                    File.AppendAllText(Environment.GetEnvironmentVariable("GC_AUTOMATION_FILE")! + ".log",
                        $"  gate {cmd[1]} {cmd[2]}: {tile.CommandStatus}\n");
                    break;
                }
            case "signout":
                await services.GetRequiredService<GateControl.Core.ViewModels.SessionHost>().SignOutAsync();
                break;
            case "replay" when root?.CurrentPage is DashboardPage dashboard:
                dashboard.FindByName<GateControl.Controls.ChartView>("HourlyChart")?.Replay();
                break;
            case "period" when root?.CurrentPage is DashboardPage dash:
                dash.Vm.Period = (GateControl.Core.ViewModels.Period)int.Parse(cmd[1], System.Globalization.CultureInfo.InvariantCulture);
                break;
            case "size":
                window.Width = double.Parse(cmd[1], System.Globalization.CultureInfo.InvariantCulture);
                window.Height = double.Parse(cmd[2], System.Globalization.CultureInfo.InvariantCulture);
                break;
        }
    }
}
#endif

#if WINDOWS && (DEBUG || GC_HARNESS)
// The parity page: boxes at AnchorX/Y (0,0), (0.5,0.5), (1,1) with Scale 0.96 and a reveal rise
// (TranslationY 8, written through Spr like a reveal), a box under a right-to-left parent moved
// by TranslationX 20, a Reduce-Motion-style instant Set, and the "late" box that gets a Clip
// afterwards (parityclip).
internal static class ParityPage
{
    public static ContentPage Create()
    {
        var grid = new Grid { BackgroundColor = Colors.White, Padding = 40 };
        BoxView Box(string id, double x, double y, Color c) => new()
        {
            AutomationId = id, Color = c, WidthRequest = 80, HeightRequest = 80,
            HorizontalOptions = LayoutOptions.Start, VerticalOptions = LayoutOptions.Start,
            Margin = new Thickness(x, y, 0, 0),
        };
        var a0 = Box("a0", 0, 0, Colors.Red); a0.AnchorX = 0; a0.AnchorY = 0;
        var a5 = Box("a5", 140, 0, Colors.Green); a5.AnchorX = 0.5; a5.AnchorY = 0.5;
        var a1 = Box("a1", 280, 0, Colors.Blue); a1.AnchorX = 1; a1.AnchorY = 1;
        var late = Box("late", 420, 0, Colors.Orange);
        var rm = Box("rm", 560, 0, Colors.Purple);
        grid.Children.Add(a0); grid.Children.Add(a5); grid.Children.Add(a1); grid.Children.Add(late); grid.Children.Add(rm);
        var rtl = new Grid { FlowDirection = FlowDirection.RightToLeft, Margin = new Thickness(0, 160, 0, 0), HeightRequest = 100, BackgroundColor = Color.FromArgb("#EEEEEE") };
        var rtlBox = new BoxView { Color = Colors.Black, WidthRequest = 60, HeightRequest = 60, HorizontalOptions = LayoutOptions.Start, VerticalOptions = LayoutOptions.Start };
        rtl.Children.Add(rtlBox);
        grid.Children.Add(rtl);
        var page = new ContentPage { Content = grid };
        page.Appearing += (_, _) => page.Dispatcher.DispatchDelayed(TimeSpan.FromMilliseconds(300), () =>
        {
            foreach (var b in new[] { a0, a5, a1 })
            {
                Spr.Set(b, Prop.Scale, 0.96);
                Spr.Set(b, Prop.TranslationY, 8);
            }
            Spr.Set(late, Prop.TranslationY, 30);
            Spr.Set(rtlBox, Prop.TranslationX, 20);
            // Reduce Motion writes: instant Sets, the same mapper path.
            Spr.Set(rm, Prop.Scale, 0.9);
            Spr.Set(rm, Prop.TranslationY, 12);
        });
        return page;
    }
}
#endif

