using Microsoft.Extensions.Logging;
// RootPage.cs — the persistent shell (spec §2, §10): one page that lives
// for the whole signed-in session and hosts the five tab pages.
//
//   ┌ outer ─────────────────────────────────────────────────────────┐
//   │ ┌ canvas (radius 28) ────────────────────────────────────────┐ │
//   │ │ [brand]   [ Live Dashboard Reports Vehicles Alerts ]   [🔍🔔] [profile] [☾] │
//   │ │ [admin]┌ tab content (one of five, kept alive) ────────────┐ │ │
//   │ │        │                                                   │ │ │
//   │ │ [⚙ ⇥] └───────────────────────────────────────────────────┘ │ │
//   │ └────────────────────────────────────────────────────────────┘ │
//   └────────────────────────────────────────────────────────────────┘
//
// Phones (< 900 dip wide): no rail, compact header (brand mark, search,
// bell, avatar, theme), and the same NavCapsule floats at the bottom. The
// light/dark toggle is at the top-right end on every layout (user request;
// the reference has it on the rail).
//
// The header, rail and nav are created once, so the nav pill is one
// long-lived, interruptible motion (spec A8). Tab pages are built as
// before (DI, XAML, view models); their content is hosted here and their
// appearing/disappearing hooks are forwarded, so loading, refresh timers
// and the live feed behave exactly as under the old Shell tabs. A tab
// switch is a cut followed by the new tab's staggered entrance (A9).
// Pushed pages (plate history, account, admin) go on the NavigationPage
// stack above this page, with the platform back button.

using GateControl.Controls;
using GateControl.Core.Formatting;
using GateControl.Core.Services;
using GateControl.Core.ViewModels;
using GateControl.Motion;
using GateControl.Platform;
using Microsoft.Maui.Controls.Shapes;

namespace GateControl.Pages;

public sealed class RootPage : ContentPage
{
    private const double WideBreakpoint = 900;

    private sealed class TabEntry(string title, PageBase page, View view)
    {
        public string Title { get; } = title;
        public PageBase Page { get; } = page;
        public View View { get; } = view;
        // The wrapper in the host that makes a parked tab unreachable by
        // keyboard focus and UI Automation (Controls/ParkHost.cs).
        public ParkHost Host { get; } = new() { Content = view };
        // Laid out ahead of its first visit (see Prewarm).
        public bool Warm { get; set; }
        // HostAppearing has run for the current visit.
        public bool Appeared { get; set; }
        public bool Visited { get; set; }
    }

    private readonly IServiceProvider _services;
    private readonly AppearanceService _appearance;
    private readonly List<TabEntry> _tabs = [];

    private readonly Grid _window = new();
    private readonly Border _canvas = new() { StrokeThickness = 0 };
    private readonly Grid _canvasGrid = new()
    {
        RowDefinitions = [new RowDefinition(GridLength.Auto), new RowDefinition(GridLength.Star)],
        ColumnDefinitions = [new ColumnDefinition(GridLength.Auto), new ColumnDefinition(GridLength.Star)],
        RowSpacing = 16,
        ColumnSpacing = 16,
    };
    // Brand left, nav centred, actions right (each side its own column so
    // the centre stays centred).
    private readonly Grid _header = new()
    {
        ColumnDefinitions =
        [
            new ColumnDefinition(GridLength.Auto), new ColumnDefinition(GridLength.Star),
            new ColumnDefinition(GridLength.Auto),
        ],
        ColumnSpacing = 12,
    };
    // Clipped to its bounds during a theme reveal (ClipHostForSnapshot): the
    // parked tabs (20000 dip to the right, see Parking) otherwise stretch the
    // window's bounds, and the snapshot (a RenderTargetBitmap of the window)
    // came out ~21000 dip wide, squeezed into a strip. Not clipped otherwise:
    // the clip made tab entrances cost the compositor 3–5 ms more a frame.
    private readonly Grid _host = new();

    private void ClipHostForSnapshot(bool on) => _host.IsClippedToBounds = on;
    private readonly Grid _rail = new()
    {
        RowDefinitions =
        [
            new RowDefinition(GridLength.Auto), new RowDefinition(GridLength.Auto),
            new RowDefinition(GridLength.Star), new RowDefinition(GridLength.Auto),
        ],
        RowSpacing = 16,
    };
    private readonly NavCapsule _nav = new();
    private readonly RailCapsule _adminRail = new();
    private readonly RailCapsule _bottomRail = new();
    private readonly Label _brandName;
    private readonly View _profileText;
    private readonly Grid _themeButton;
    private readonly IconView _themeIcon = new() { Glyph = Icons.Moon, Size = 16 };
    private readonly Border _badge;
    private readonly Label _badgeText;
    private int _current = -1;
    private bool? _wide;
#if WINDOWS
    private Microsoft.UI.Xaml.Media.Imaging.RenderTargetBitmap? _warmBitmap;
#endif

    public RootPage(IServiceProvider services, Session session, AppearanceService appearance,
                    LivePage live, DashboardPage dashboard, ReportsPage reports,
                    VehiclesPage vehicles, AlertsPage alerts)
    {
        _services = services;
        _appearance = appearance;
        NavigationPage.SetHasNavigationBar(this, false);
        Title = "Gate Control";
        var res = Application.Current!.Resources;

        Add("Live", live);
        Add("Dashboard", dashboard);
        Add("Reports", reports);
        Add("Vehicles", vehicles);
        Add("Alerts", alerts);

        // ---- header ----
        _brandName = new Label { Text = "Gate Control", FontFamily = "InterSemiBold", FontSize = 14, VerticalOptions = LayoutOptions.Center };
        var brand = new Border
        {
            Style = (Style)res["Capsule"],
            Padding = new Thickness(4, 4, 14, 4),
            Content = new HorizontalStackLayout { Spacing = 8, Children = { BrandMark(), _brandName } },
            VerticalOptions = LayoutOptions.Center,
        };

        _themeButton = IconButton(_themeIcon, "Light or dark", () => ToggleTheme(_themeButton!, null));
        // The press starts the reveal (PrepareTheme); a press that will not
        // become a tap — the pointer leaves the button or drags away — is
        // undone at once, so the window is never left as a frozen snapshot
        // (review round 3). The 1 s timeout in ThemeReveal stays as the
        // backstop for a press that is simply held.
        var themeDown = new PointerGestureRecognizer();
        Point? pressedAt = null;
        themeDown.PointerPressed += (_, e) =>
        {
            pressedAt = e.GetPosition(_themeButton);
            PrepareTheme();
        };
        themeDown.PointerMoved += (_, e) =>
        {
            if (pressedAt is { } d && e.GetPosition(_themeButton) is { } p &&
                Math.Abs(p.X - d.X) + Math.Abs(p.Y - d.Y) > GateControl.Core.Motion.Gesture.DragThreshold)
            {
                pressedAt = null;
                CancelThemePress("drag");
            }
        };
        themeDown.PointerExited += (_, _) =>
        {
            pressedAt = null;
            CancelThemePress("exit");
        };
        themeDown.PointerReleased += (_, _) => pressedAt = null;
        _themeButton.GestureRecognizers.Add(themeDown);
        var search = IconButton(new IconView { Glyph = Icons.Search, Size = 16 }, "Search reports", () => Select(2));
        _badgeText = new Label
        {
            FontFamily = "InterSemiBold",
            FontSize = 9,
            TextColor = Colors.White,
            HorizontalTextAlignment = TextAlignment.Center,
            VerticalTextAlignment = TextAlignment.Center,
        };
        _badge = new Border
        {
            StrokeThickness = 0,
            StrokeShape = new RoundRectangle { CornerRadius = 7 },
            HeightRequest = 14,
            MinimumWidthRequest = 14,
            Padding = new Thickness(3, 0),
            HorizontalOptions = LayoutOptions.End,
            VerticalOptions = LayoutOptions.Start,
            Margin = new Thickness(0, 1, 1, 0),
            InputTransparent = true,
            IsVisible = false,
            Content = _badgeText,
            BackgroundColor = (Color)res["BrandLight"],
        };
        var bell = IconButton(new IconView { Glyph = Icons.Bell, Size = 16 }, "Alerts", () => Select(4));
        bell.Children.Add(_badge);
        var icons = new Border
        {
            Style = (Style)res["Capsule"],
            Padding = 4,
            VerticalOptions = LayoutOptions.Center,
            Content = new HorizontalStackLayout { Spacing = 2, Children = { search, bell } },
        };

        var initials = string.Concat(session.DisplayName.Split(' ', StringSplitOptions.RemoveEmptyEntries)
            .Take(2).Select(w => char.ToUpperInvariant(w[0])));
        var avatar = new Grid
        {
            WidthRequest = 32,
            HeightRequest = 32,
            Children =
            {
                Themed(new Border { StrokeThickness = 0, StrokeShape = new Ellipse() }, VisualElement.BackgroundColorProperty, "Well"),
                new Label
                {
                    Text = initials, FontFamily = "InterSemiBold", FontSize = 12,
                    HorizontalOptions = LayoutOptions.Center, VerticalOptions = LayoutOptions.Center,
                },
            },
        };
        _profileText = new VerticalStackLayout
        {
            VerticalOptions = LayoutOptions.Center,
            Children =
            {
                new Label { Text = session.DisplayName, FontFamily = "InterSemiBold", FontSize = 13, LineBreakMode = LineBreakMode.TailTruncation, MaximumWidthRequest = 160 },
                new Label { Text = Display.Role(session.Me.Role), Style = (Style)res["Caption"] },
            },
        };
        var profile = new Border
        {
            Style = (Style)res["Capsule"],
            Padding = new Thickness(4, 4, 12, 4),
            VerticalOptions = LayoutOptions.Center,
            Content = new HorizontalStackLayout
            {
                Spacing = 10,
                Children = { avatar, _profileText, new IconView { Glyph = Icons.ChevronDown, Size = 14 } },
            },
        };
        SemanticProperties.SetDescription(profile, "Account");
        Pressable.SetCommand(profile, new Command(async () => await Push<SettingsPage>()));

        brand.HorizontalOptions = LayoutOptions.Start;
        _header.Add(brand, 0);
        _header.Add(new HorizontalStackLayout
        {
            Spacing = 12,
            HorizontalOptions = LayoutOptions.End,
            // The light/dark toggle sits at the top-right end on every layout
            // (the user's choice; the reference has it on the rail).
            Children = { icons, profile, new Border
            {
                Style = (Style)res["Capsule"],
                Padding = 4,
                VerticalOptions = LayoutOptions.Center,
                Content = _themeButton,
            } },
        }, 2);

        _nav.ItemTapped += (_, i) => Select(i);

        // ---- rail (wide) ----
        _adminRail.SetItems(
        [
            new RailCapsule.Item(Icons.Users, "Users and roles", async () => await Push<UsersPage>()),
            new RailCapsule.Item(Icons.DoorOpen, "Gates", async () => await Push<GatesPage>()),
            new RailCapsule.Item(Icons.Database, "Data and retention", async () => await Push<DataPage>()),
        ]);
        _adminRail.IsVisible = session.IsAdmin;
        _bottomRail.SetItems(
        [
            new RailCapsule.Item(Icons.Settings, "Account and settings", async () => await Push<SettingsPage>()),
            new RailCapsule.Item(Icons.LogOut, "Sign out", () =>
                _services.GetRequiredService<SettingsViewModel>().SignOutCommand.Execute(null)),
        ]);
        _rail.Add(_adminRail, 0, 0);
        _rail.Add(_bottomRail, 0, 3);

        // ---- assembly ----
        foreach (var tab in _tabs)
        {
            tab.View.IsVisible = false;
            _host.Children.Add(tab.Host);
        }
        _canvasGrid.Add(_header, 0, 0);
        Grid.SetColumnSpan(_header, 2);
        _canvasGrid.Add(_rail, 0, 1);
        _canvasGrid.Add(_host, 1, 1);
        // The canvas Border is only the rounded backdrop; the shell grid is
        // its sibling. (As a Border's content, the grid was measured
        // unconstrained on Windows and overflowed the right and bottom.)
        _window.Children.Add(_canvas);
        _window.Children.Add(_canvasGrid);
        Themed(_window, VisualElement.BackgroundColorProperty, "Outer");
        Themed(_canvas, VisualElement.BackgroundColorProperty, "Background");
        Themed(this, VisualElement.BackgroundColorProperty, "Outer");
        Content = _window;
        _header.SizeChanged += (_, _) => FitTabs();
        SizeChanged += (_, _) => Arrange();
        // Hosted tabs get the host's exact size: as plain stretched
        // children, WinUI kept the width they were first measured at (wider
        // than the host after the layout switched) and cards ran past the
        // right edge.
        _host.SizeChanged += (_, _) =>
        {
            FitTabs();
            // A resize (or maximise / restore) re-lays out every parked tab:
            // run WinUI's memory check at idle again once it has settled
            // (review round 3, item 5).
            if (_prewarmScheduled)
            {
                var generation = ++_resizeGeneration;
                Dispatcher.DispatchDelayed(TimeSpan.FromMilliseconds(600), () =>
                {
                    if (generation == _resizeGeneration)
                    {
                        WhenIdle(SettleXamlCleanup);
                    }
                });
            }
        };
        // Alt-Tab / Esc to another window mid-press: undo the press.
        var deactivationHooked = false;
        Loaded += (_, _) =>
        {
            if (!deactivationHooked && Window is { } w)
            {
                deactivationHooked = true;
                w.Deactivated += (_, _) => CancelThemePress("deactivated");
            }
        };
        // A wipe's snapshot is the old window size: cut it short on a resize.
        _window.SizeChanged += (_, _) => ThemeReveal.Finish(_window);

        // Alerts: the open count rides on the bell (brand dot).
        if (alerts.BindingContext is AlertsViewModel avm)
        {
            avm.PropertyChanged += (_, e) =>
            {
                if (e.PropertyName == nameof(AlertsViewModel.Unread))
                {
                    ShowUnread(avm.Unread);
                }
            };
            _ = avm.RefreshUnreadAsync();
        }
        Application.Current.RequestedThemeChanged += (_, _) => PaintTheme();
        PaintTheme();
        Arrange();
    }

    // For verification scripts and tests: which tab is showing.
    public int CurrentTab => _current;

    // The tab page showing now (for the plate pane routing).
    public PageBase? CurrentPage => _current >= 0 ? _tabs[_current].Page : null;

    public bool IsWide => _wide == true;

    internal string DebugSizes =>
        $"canvasGrid {_canvasGrid.Bounds} header {_header.Bounds} rail {_rail.Bounds} host {_host.Bounds} " +
        $"bottomRail {_bottomRail.Bounds} headerKids {string.Join(" ", _header.Children.OfType<VisualElement>().Select(c => $"{c.GetType().Name}:{c.Bounds}/{c.DesiredSize}"))} rows {string.Join(",", _canvasGrid.RowDefinitions.Select(r => r.Height))}";

    private void Add(string title, PageBase page)
    {
        _tabs.Add(new TabEntry(title, page, Detach(page)));
    }

    // Moves a page's content into the shell. The content keeps the page's
    // view model and resources; bindings to the page itself
    // ({x:Reference Page}) are unaffected.
    private static View Detach(PageBase page)
    {
        var view = page.Content ?? new ContentView();
        // Pin the view model on the content BEFORE detaching: otherwise the
        // bindings briefly see a null context, and two-way pickers write
        // null back into the view model (Reports crashed on that).
        view.BindingContext = page.BindingContext;
        page.Content = null;
        ScrollFit.Apply(view);
        foreach (var md in page.Resources.MergedDictionaries)
        {
            view.Resources.MergedDictionaries.Add(md);
        }
        foreach (var (key, value) in page.Resources)
        {
            if (!view.Resources.ContainsKey(key))
            {
                view.Resources[key] = value;
            }
        }
        return view;
    }

    protected override void OnAppearing()
    {
        base.OnAppearing();
        if (_current < 0)
        {
            Select(0);
            Dispatcher.Dispatch(ParkInitialFocus);
        }
        else if (!_tabs[_current].Appeared)
        {
            _tabs[_current].Appeared = true;
            _tabs[_current].Page.HostAppearing();
        }
    }

    protected override void OnDisappearing()
    {
        base.OnDisappearing();
        if (_current >= 0 && _tabs[_current].Appeared)
        {
            _tabs[_current].Appeared = false;
            _tabs[_current].Page.HostDisappearing();
        }
    }

    public void Select(int index)
    {
        if (index == _current || index < 0 || index >= _tabs.Count)
        {
            return;
        }
        FrameTrace.Mark("tab.select");
        var first = _current < 0;
        if (_current >= 0)
        {
            // A9: the old tab is cut, not animated out. It stays laid out
            // (parked: invisible and untouchable),
            // so coming back to it costs no layout pass.
            Park(_tabs[_current]);
            if (_tabs[_current].Appeared)
            {
                _tabs[_current].Appeared = false;
                _tabs[_current].Page.HostDisappearing();
            }
        }
        _current = index;
        var tab = _tabs[index];
        tab.Visited = true;
        FrameTrace.Mark("tab.unpark");
        Unpark(tab);
        _nav.SelectedIndex = index;
        if (!first)
        {
            // Not for the initial tab: on Windows, Announce throws while the
            // window is still being realised (the shell just replaced the
            // launch page).
            try
            {
                SemanticScreenReader.Announce(tab.Title);
            }
            catch (Exception e) when (e is NullReferenceException or InvalidOperationException or System.Runtime.InteropServices.COMException)
            {
            }
        }
        Reveal.Page(tab.View);
        if (tab.Page.HasLoaded)
        {
            // Its data is already on screen: refresh after the entrance has
            // played, so new data never re-lays the page out mid-reveal.
            Dispatcher.DispatchDelayed(TimeSpan.FromMilliseconds(1200), () =>
            {
                if (_current == index && !tab.Appeared)
                {
                    FrameTrace.Mark("tab.refresh");
                    tab.Appeared = true;
                    tab.Page.HostAppearing();
                }
            });
        }
        else
        {
            tab.Appeared = true;
            tab.Page.HostAppearing();
        }
        if (!_prewarmScheduled)
        {
            _prewarmScheduled = true;
            Prewarm();
        }
    }

    // Parking (review 2b, round 2): every tab that isn't showing stays laid
    // out at the host's exact size (so showing it needs no layout pass) and
    // keeps its render data: it stays at opacity 1 but sits 20000 dip
    // outside the window, untouchable and out of the accessibility tree, so
    // nothing of it is drawn inside the window. (Collapsing visited tabs made
    // their next entrance start with a full layout pass; drawing them at
    // 1/255 inside the window let layers that ignore a parent's opacity show
    // through as ghosts; parking at opacity 0 made WinUI drop and rebuild the
    // tab's render data, a 25–29 ms first frame on every entrance.)
    private const double ParkedOffset = 20000;

    private static void Park(TabEntry tab)
    {
        tab.View.InputTransparent = true;
        AutomationProperties.SetExcludedWithChildren(tab.View, true);
        tab.Host.Parked = true;
        tab.View.TranslationX = ParkedOffset;
        tab.View.Opacity = 1;
        tab.View.IsVisible = true;
        tab.Warm = true;
    }

    private static void Unpark(TabEntry tab)
    {
        tab.View.InputTransparent = false;
        AutomationProperties.SetExcludedWithChildren(tab.View, false);
        tab.Host.Parked = false;
        tab.View.TranslationX = 0;
        tab.View.Opacity = 1;
        tab.View.IsVisible = true;
    }

    private bool _prewarmScheduled;
    private double _prewarmStartedAt;

    // The first visit of a heavy tab used to stall 100–250 ms while its
    // whole tree was laid out. After the first tab has settled, lay the
    // others out ahead of time, one per idle slot, at low priority, parked.
    private void Prewarm()
    {
        _prewarmStartedAt = Spr.Now;
        GateControl.Motion.BlobClip.Prewarm();
        var pending = new Queue<TabEntry>();
        void Next()
        {
            while (pending.Count > 0 && pending.Peek().Warm)
            {
                pending.Dequeue();
            }
            if (pending.Count == 0)
            {
                // Every tab is laid out, loaded and rendered once. (No forced
                // collection here or on tab switch: review round 2 — it froze
                // the tap frame.)
                SettleXamlCleanup();
                AppLog.For("RootPage").LogInformation("tabs warmed {Seconds:0.0} s after sign-in", Spr.Now - _prewarmStartedAt);
                FrameTrace.Mark("prewarm.done");
                return;
            }
            var tab = pending.Dequeue();
            if (_tabs.IndexOf(tab) == _current)
            {
                Next();
                return;
            }
            Park(tab);
            _ = WarmAsync(tab, () => WhenIdle(Next));
        }
        foreach (var t in _tabs.Where((t, i) => i != _current && !t.Warm))
        {
            pending.Enqueue(t);
        }
        Dispatcher.DispatchDelayed(TimeSpan.FromMilliseconds(1200), () => WhenIdle(Next));
    }

    // WinUI 1.8 (CCoreServices::TrackMemory, xcpcore.cpp) checks its native
    // heap every 50 frames that need a render walk and, when it has grown
    // by over 50 MB since the last low point, asks .NET for a *blocking* full
    // collection (IReferenceTrackerHost.DisconnectUnusedReferenceSources).
    // Launch plus the tab warm-up grows it past that, but an idle app draws
    // almost no frames, so the check came due 70–250 ms into whichever tab
    // was entered first (EventPipe: GCTriggered Induced, a 45–80 ms frame).
    // Here, with the tabs warm and nothing visible moving, the parked tabs
    // (drawn outside the window, see Parking) are nudged by half a dip for 120
    // frames, so every frame needs a render walk: their render data is created
    // now and the check (two of them), with its collection if due, happens at
    // idle instead of in the first entrance.
    private const int XamlMemoryCheckFrames = 120;
    private int _settleRun;

    private void SettleXamlCleanup()
    {
        var parked = _tabs.Where((t, i) => i != _current && t.Warm).ToList();
        if (parked.Count == 0)
        {
            return;
        }
        FrameTrace.Mark("prewarm.settle");
        var frames = 0;
        var run = ++_settleRun;
        Spr.OnFrame(_ =>
        {
            // A newer run replaces this one (it restarts the 120 frames).
            var done = ++frames >= XamlMemoryCheckFrames || run != _settleRun;
            foreach (var t in parked)
            {
                if (_tabs.IndexOf(t) == _current)
                {
                    continue;  // selected meanwhile: Unpark has set it up
                }
                t.View.TranslationX = ParkedOffset + (done || frames % 2 == 0 ? 0 : 0.5);
            }
            return !done;
        }, Dispatcher);
    }

    // Lay the parked tab out with its data, then render it once off screen
    // (Windows: into one RenderTargetBitmap reused for every tab, so the
    // warm-up holds one surface, not one per tab), so its first
    // visit does not pay for creating its text and surfaces — traced, that
    // first presented frame took 40 ms on Reports. The render call runs at
    // idle priority; parked content is never drawn inside the window.
    private async Task WarmAsync(TabEntry tab, Action next)
    {
        try
        {
            // A slow server must not hold up the tabs after this one: wait
            // at most 2 s for the data (traced: with the wait unbounded, two
            // of five launches reached Reports and Vehicles before they had
            // loaded, and laid them out mid-entrance).
            await Task.WhenAny(tab.Page.PreloadAsync(), Task.Delay(2000));
            // Give the layout pass its own frames.
            await Task.Delay(250);
#if WINDOWS
            if (_tabs.IndexOf(tab) != _current &&
                tab.View.Handler?.PlatformView is Microsoft.UI.Xaml.UIElement element)
            {
                FrameTrace.Mark("tab.warm");
                Reveal.Warm(tab.View);
                _warmBitmap ??= new Microsoft.UI.Xaml.Media.Imaging.RenderTargetBitmap();
                await _warmBitmap.RenderAsync(element);
                if (_tabs.IndexOf(tab) != _current && tab.Host.Handler?.PlatformView is ParkPanel panel)
                {
                    panel.Refresh();
                }
            }
#endif
        }
        catch (Exception e)
        {
            AppLog.For("RootPage").LogWarning(e, "tab warm failed");
        }
        next();
    }

    private void WhenIdle(Action action)
    {
#if WINDOWS
        if (Microsoft.UI.Dispatching.DispatcherQueue.GetForCurrentThread() is { } queue &&
            queue.TryEnqueue(Microsoft.UI.Dispatching.DispatcherQueuePriority.Low, () => action()))
        {
            return;
        }
#endif
        Dispatcher.Dispatch(action);
    }

    // Tabs get an exact size, capped on phones to what is visible above the
    // floating nav bar.
    private void FitTabs()
    {
        var height = _host.Height;
        if (_wide == false && _header.Height > 0)
        {
            var visible = _canvasGrid.HeightRequest - _header.Height - _canvasGrid.RowSpacing - _host.Margin.Bottom;
            height = Math.Min(height, Math.Max(0, visible));
        }
        foreach (var tab in _tabs)
        {
            if (Math.Abs(tab.View.WidthRequest - _host.Width) > 0.5 || Math.Abs(tab.View.HeightRequest - height) > 0.5)
            {
                tab.View.WidthRequest = _host.Width;
                tab.View.HeightRequest = height;
            }
        }
    }

    private void Arrange()
    {
        if (Width <= 0)
        {
            return;
        }
        var wide = Width >= WideBreakpoint;
        // The shell grid gets an explicit size: as a stretched child (with a
        // Margin, a Padding, or inside the Border) WinUI arranged its star
        // row and column past the right and bottom edges.
        var inset = wide ? new Thickness(32, 28, 32, 12) : new Thickness(12, 8, 12, 0);
        _canvasGrid.Margin = inset;
        _canvasGrid.HorizontalOptions = LayoutOptions.Start;
        _canvasGrid.VerticalOptions = LayoutOptions.Start;
        _canvasGrid.WidthRequest = Math.Max(0, Width - inset.HorizontalThickness);
        _canvasGrid.HeightRequest = Math.Max(0, Height - inset.VerticalThickness);
        if (_wide == wide)
        {
            FitTabs();
            return;
        }
        _wide = wide;
        if (_nav.Parent is Layout old)
        {
            old.Remove(_nav);
        }
        _nav.Compact = !wide;
        _nav.SetItems(_tabs.Select(t => t.Title));
        _nav.SelectedIndex = _current;
        if (wide)
        {
            _canvas.Margin = 12;
            _canvas.StrokeShape = new RoundRectangle { CornerRadius = 28 };
            _rail.Margin = new Thickness(0, 0, 0, 20);
            _rail.IsVisible = true;
            _canvasGrid.ColumnSpacing = 16;
            _brandName.IsVisible = true;
            _profileText.IsVisible = true;
            _host.Margin = 0;
            _nav.HorizontalOptions = LayoutOptions.Center;
            _nav.VerticalOptions = LayoutOptions.Center;
            _nav.Margin = 0;
            _header.Add(_nav, 1);
        }
        else
        {
            _canvas.Margin = 0;
            _canvas.StrokeShape = new RoundRectangle { CornerRadius = 0 };
            _rail.IsVisible = false;
            _canvasGrid.ColumnSpacing = 0;
            _brandName.IsVisible = Width >= 520;
            _profileText.IsVisible = false;
            _host.Margin = new Thickness(0, 0, 0, 64);
            _nav.HorizontalOptions = LayoutOptions.Center;
            _nav.VerticalOptions = LayoutOptions.End;
            _nav.Margin = new Thickness(0, 0, 0, 12);
            // A sibling of the shell grid, not a child: on Android the grid's
            // star row overflowed the window and took a bottom-aligned bar
            // off screen.
            _window.Children.Add(_nav);
        }
    }

    // When the shell replaces the sign-in page, WinUI moves focus to the
    // next tab stop (the first gate's Open button) with the focus state of
    // the element that had it, which draws a focus ring at rest. Park focus
    // programmatically on the page's frame instead: no ring until the user
    // presses Tab, and Tab then starts from the top as usual.
    private void ParkInitialFocus()
    {
#if WINDOWS
        for (var e = _window.Handler?.PlatformView as Microsoft.UI.Xaml.DependencyObject; e is not null;
             e = Microsoft.UI.Xaml.Media.VisualTreeHelper.GetParent(e))
        {
            if (e is Microsoft.UI.Xaml.Controls.Control control)
            {
                var wasTabStop = control.IsTabStop;
                control.IsTabStop = true;
                control.Focus(Microsoft.UI.Xaml.FocusState.Programmatic);
                control.IsTabStop = wasTabStop;
                return;
            }
        }
#endif
    }

    // ---- theme ----------------------------------------------------------------------

    private static AppTheme Effective =>
        Application.Current?.RequestedTheme == AppTheme.Dark ? AppTheme.Dark : AppTheme.Light;

    private void PaintTheme()
    {
        _themeIcon.Glyph = Effective == AppTheme.Dark ? Icons.Sun : Icons.Moon;
    }

    // Pointer-down on a theme button: snapshot now (spec A11 start latency).
    // Undo a press in progress (pointer left / dragged away / window
    // deactivated: Alt-Tab, Esc to another window).
    public void CancelThemePress(string why) => ThemeReveal.CancelPress(_window, why);

    public void PrepareTheme()
    {
        var target = Effective == AppTheme.Dark ? AppTheme.Light : AppTheme.Dark;
        var previous = _appearance.Current;
        // Parked tabs out of rendering first: the snapshot (a render of the
        // whole window element) then skips them, and so does the restyle.
        HideParkedForRestyle();
        ClipHostForSnapshot(true);
        ThemeReveal.Prepare(_window, _window, target, () => ApplyTheme(target), () =>
        {
            // In memory only: the press never touched the saved preference.
            _appearance.Preview(previous);
            RestoreParkedAfterRestyle();
        });
    }

    // Switches the theme in memory (UserAppTheme) — not saved, and not seen by
    // the appearance setting's listeners, until the tap's wipe has run
    // (CommitTheme).
    private void ApplyTheme(AppTheme target)
    {
        HideParkedForRestyle();
        ClipHostForSnapshot(true);
        _appearance.Preview(target == AppTheme.Dark ? Appearance.Dark : Appearance.Light);
    }

    private void CommitTheme(AppTheme target) =>
        _appearance.Current = target == AppTheme.Dark ? Appearance.Dark : Appearance.Light;

    // The theme reveal from the rail's sun / moon (or the header toggle on
    // phones), as if that button was tapped.
    public void ToggleTheme(AppTheme to) => ToggleTheme(_themeButton, to);

    private async void ToggleTheme(VisualElement from, AppTheme? to)
    {
        FrameTrace.Mark("theme.tap");
        // After a pointer-down the new theme is already applied under the
        // snapshot (ThemeReveal.Prepare): the press decided the target.
        var pressed = ThemeReveal.PendingKey as AppTheme?;
        var target = to ?? pressed ?? (Effective == AppTheme.Dark ? AppTheme.Light : AppTheme.Dark);
        if (target == Effective && pressed != target)
        {
            return;
        }
        var origin = Centre(from, _window);

        ClipHostForSnapshot(true);
        var res = Application.Current!.Resources;
        var newOuter = (Color)res[target == AppTheme.Dark ? "OuterDark" : "OuterLight"];
        await ThemeReveal.RunAsync(_window, _window, origin, target, () => ApplyTheme(target), newOuter, () =>
        {
            // The wipe has run (or been cut short by a newer one): saved now.
            CommitTheme(target);
            RestoreParkedAfterRestyle();
        }, () => App.ApplyCaption(target));
    }

    // A theme switch restyles every element of every tab. With the parked tabs
    // drawn (off-window, see Parking) their whole render data was rebuilt in
    // the same frame: a 110–134 ms frame before the first wipe frame. They
    // are dropped from rendering (opacity 0) just before the switch and
    // brought back one per idle slot after the wipe, then WinUI's memory
    // check is run at idle again (SettleXamlCleanup).
    private int _restyleGeneration;
    private int _resizeGeneration;

    private void HideParkedForRestyle()
    {
        _restyleGeneration++;
        for (var i = 0; i < _tabs.Count; i++)
        {
            if (i != _current && _tabs[i].Warm)
            {
                _tabs[i].View.Opacity = 0;
            }
        }
    }

    private void RestoreParkedAfterRestyle()
    {
        var generation = _restyleGeneration;
        if (!ThemeReveal.Busy)
        {
            ClipHostForSnapshot(false);
        }
        var pending = new Queue<TabEntry>(_tabs.Where((t, i) => i != _current && t.Warm && t.View.Opacity == 0));
        void Next()
        {
            if (generation != _restyleGeneration)
            {
                return;  // another switch started: it restores them
            }
            while (pending.Count > 0)
            {
                var tab = pending.Dequeue();
                if (_tabs.IndexOf(tab) != _current)
                {
                    FrameTrace.Mark("theme.restore");
                    tab.View.Opacity = 1;
                    Dispatcher.DispatchDelayed(TimeSpan.FromMilliseconds(150), () => WhenIdle(Next));
                    return;
                }
            }
            SettleXamlCleanup();
        }
        Dispatcher.DispatchDelayed(TimeSpan.FromMilliseconds(300), () => WhenIdle(Next));
    }

    private static Point PositionIn(VisualElement e, VisualElement ancestor)
    {
        double x = 0, y = 0;
        for (Element? cur = e; cur is VisualElement v && !ReferenceEquals(v, ancestor); cur = cur.Parent)
        {
            x += v.X + v.TranslationX;
            y += v.Y + v.TranslationY;
        }
        return new Point(x, y);
    }

    private static Point Centre(VisualElement e, VisualElement ancestor)
    {
        var p = PositionIn(e, ancestor);
        return new Point(p.X + e.Width / 2, p.Y + e.Height / 2);
    }

    // ---- pieces --------------------------------------------------------------------

    private void ShowUnread(long unread)
    {
        _badge.IsVisible = unread > 0;
        _badgeText.Text = unread > 99 ? "99+" : unread.ToString(System.Globalization.CultureInfo.CurrentCulture);
        SemanticProperties.SetDescription(_badge, unread > 0 ? $"{unread} open alerts" : "No open alerts");
    }

    private Task Push<T>() where T : Page =>
        Navigation.PushAsync(_services.GetRequiredService<T>());

    private static Grid IconButton(IconView icon, string name, Action invoke)
    {
        var cell = new Grid { WidthRequest = 32, HeightRequest = 32, BackgroundColor = Colors.Transparent, Children = { icon } };
        SemanticProperties.SetDescription(cell, name);
        ToolTipProperties.SetText(cell, name);
        Pressable.SetCommand(cell, new Command(invoke));
        return cell;
    }

    private static View BrandMark()
    {
        var res = Application.Current!.Resources;
        return new Grid
        {
            WidthRequest = 28,
            HeightRequest = 28,
            Children =
            {
                new Border { StrokeThickness = 0, StrokeShape = new Ellipse(), BackgroundColor = (Color)res["BrandLight"] },
                new IconView { Glyph = Icons.DoorOpen, Size = 15, Tint = Colors.White },
            },
        };
    }

    private static T Themed<T>(T view, BindableProperty property, string key) where T : BindableObject
    {
        var res = Application.Current!.Resources;
        if (property == Shape.FillProperty)
        {
            view.SetAppTheme<Brush>(property, new SolidColorBrush((Color)res[key + "Light"]), new SolidColorBrush((Color)res[key + "Dark"]));
        }
        else
        {
            view.SetAppThemeColor(property, (Color)res[key + "Light"], (Color)res[key + "Dark"]);
        }
        return view;
    }
}
