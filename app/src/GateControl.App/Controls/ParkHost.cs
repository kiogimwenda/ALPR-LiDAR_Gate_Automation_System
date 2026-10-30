// ParkHost.cs — the wrapper around each hosted tab (RootPage). While its tab
// is parked (laid out and drawn 20000 dip outside the window, see RootPage
// Parking) the tab must be unreachable, not only invisible:
//
//   * Windows keyboard focus: InputTransparent only affects hit-testing, so
//     Tab / arrow keys moved focus onto a parked tab's buttons (traced: a
//     focus walk from the Dashboard landed in the parked Live tab). While
//     parked, the panel's controls are not tab stops (restored on unpark;
//     no restyle, unlike IsEnabled), any remaining focus move into it is
//     cancelled (GettingFocus: controls added while parked, programmatic
//     focus), and focus inside it is moved out when it is parked.
//   * Windows UI Automation: MAUI's ExcludedWithChildren did not prune the
//     tree (a UIA dump with the Dashboard showing listed the parked Live
//     tab's Open / Close buttons). The panel's automation peer reports no
//     children while parked, so screen readers never reach that content.
//
// Elsewhere it is a plain ContentView (Android / iOS use RootPage's
// InputTransparent + ExcludedWithChildren, not run-tested here).

namespace GateControl.Controls;

public sealed class ParkHost : ContentView
{
    public static readonly BindableProperty ParkedProperty = BindableProperty.Create(
        nameof(Parked), typeof(bool), typeof(ParkHost), false,
        propertyChanged: (b, _, _) => ((ParkHost)b).Sync());

    public bool Parked
    {
        get => (bool)GetValue(ParkedProperty);
        set => SetValue(ParkedProperty, value);
    }

    protected override void OnHandlerChanged()
    {
        base.OnHandlerChanged();
        Sync();
    }

    private void Sync()
    {
#if WINDOWS
        if (Handler?.PlatformView is ParkPanel panel)
        {
            panel.SetParked(Parked);
        }
#endif
    }
}

#if WINDOWS
public sealed class ParkPanel : Microsoft.Maui.Platform.ContentPanel
{
    private bool _parked;

    public ParkPanel()
    {
        GettingFocus += (_, e) =>
        {
            if (!_parked)
            {
                return;
            }
            // A control that became a tab stop after the tab was parked (its
            // template applied later, or a row added): out of navigation from
            // now on, so the next Tab press moves past it. (Redirecting focus
            // from in here — FindNextElement / TrySetNewFocusedElement during
            // GettingFocus — crashed WinUI inside TryMoveFocusAsync.)
            DependencyObjectHelpers.Drop(e.NewFocusedElement, _stops);
            // TryCancel, not Cancel = true: some focus changes (window
            // activation) cannot be cancelled, and setting Cancel throws.
            e.TryCancel();
            e.Handled = true;
        };
    }

    private static class DependencyObjectHelpers
    {
        public static void Drop(Microsoft.UI.Xaml.DependencyObject? e, List<Microsoft.UI.Xaml.UIElement> into)
        {
            if (e is Microsoft.UI.Xaml.UIElement { IsTabStop: true } c)
            {
                c.IsTabStop = false;
                into.Add(c);
            }
        }
    }

    // Called once a parked tab has been laid out and rendered (its controls'
    // templates exist now).
    public void Refresh()
    {
        if (_parked)
        {
            Collect(this);
        }
    }

    public bool IsParked => _parked;

    // Controls whose IsTabStop this panel turned off while parked.
    private readonly List<Microsoft.UI.Xaml.UIElement> _stops = [];

    private int _version;

    // Parking and unparking happen in the tab switch's frame, so the tree
    // walk and the tab-stop changes wait for an idle slot (until then the
    // GettingFocus check still keeps focus out of a parked tab; a freshly
    // shown tab is Tab-reachable a moment later). Focus already inside a tab
    // being parked is moved out at once.
    public void SetParked(bool parked)
    {
        if (_parked == parked)
        {
            return;
        }
        _parked = parked;
        var version = ++_version;
        if (parked && XamlRoot is { } root &&
            Microsoft.UI.Xaml.Input.FocusManager.GetFocusedElement(root) is Microsoft.UI.Xaml.DependencyObject focused &&
            Contains(focused))
        {
            FocusOutside();
        }
        DispatcherQueue?.TryEnqueue(Microsoft.UI.Dispatching.DispatcherQueuePriority.Low, () =>
        {
            if (version != _version)
            {
                return;  // parked / unparked again meanwhile: that call does it
            }
            if (_parked)
            {
                // Out of Tab / arrow-key navigation: a cancelled GettingFocus
                // alone left keyboard users stuck (Tab past the active tab's
                // last control landed on a parked one and went nowhere).
                Collect(this);
            }
            else
            {
                foreach (var c in _stops)
                {
                    c.IsTabStop = true;
                }
                _stops.Clear();
            }
            if (Microsoft.UI.Xaml.Automation.Peers.FrameworkElementAutomationPeer.FromElement(this) is { } peer)
            {
                peer.RaiseStructureChangedEvent(Microsoft.UI.Xaml.Automation.Peers.AutomationStructureChangeType.ChildrenInvalidated, peer);
            }
        });
    }

    // The tab's native controls, found through the MAUI tree (managed, cheap)
    // rather than a native visual-tree walk (one WinRT call per element:
    // ~80 ms on a Reports tab with 100 rows, a dropped-frame stall).
    private void Collect(Microsoft.UI.Xaml.DependencyObject _)
    {
        if (CrossPlatformLayout is IVisualTreeElement root)
        {
            Collect(root);
        }
    }

    private void Collect(IVisualTreeElement e)
    {
        // Native controls, and the pressables made keyboard stops (Pressable,
        // on the container view when there is one).
        if (e is IView { Handler: { } h })
        {
            foreach (var native in new[] { h.ContainerView as Microsoft.UI.Xaml.UIElement, h.PlatformView as Microsoft.UI.Xaml.UIElement })
            {
                if (native is { IsTabStop: true } c && !_stops.Contains(c))
                {
                    c.IsTabStop = false;
                    _stops.Add(c);
                }
            }
        }
        foreach (var child in e.GetVisualChildren())
        {
            Collect(child);
        }
    }

    // Focus was inside the tab being parked: park it on the nearest control
    // above this panel (as RootPage does at start), outside the tab.
    private void FocusOutside()
    {
        for (var e = Microsoft.UI.Xaml.Media.VisualTreeHelper.GetParent(this); e is not null;
             e = Microsoft.UI.Xaml.Media.VisualTreeHelper.GetParent(e))
        {
            if (e is Microsoft.UI.Xaml.Controls.Control control)
            {
                var was = control.IsTabStop;
                control.IsTabStop = true;
                control.Focus(Microsoft.UI.Xaml.FocusState.Programmatic);
                control.IsTabStop = was;
                return;
            }
        }
    }

    private bool Contains(Microsoft.UI.Xaml.DependencyObject e)
    {
        for (var cur = e; cur is not null; cur = Microsoft.UI.Xaml.Media.VisualTreeHelper.GetParent(cur))
        {
            if (ReferenceEquals(cur, this))
            {
                return true;
            }
        }
        return false;
    }

    protected override Microsoft.UI.Xaml.Automation.Peers.AutomationPeer OnCreateAutomationPeer() => new Peer(this);

    private sealed class Peer(ParkPanel owner) : Microsoft.UI.Xaml.Automation.Peers.FrameworkElementAutomationPeer(owner)
    {
        protected override IList<Microsoft.UI.Xaml.Automation.Peers.AutomationPeer>? GetChildrenCore() =>
            owner._parked ? [] : base.GetChildrenCore();

        // A wrapper, not a control: not listed itself in the control view.
        protected override bool IsControlElementCore() => false;

        protected override bool IsContentElementCore() => false;
    }
}

public sealed class ParkHostHandler : Microsoft.Maui.Handlers.ContentViewHandler
{
    protected override Microsoft.Maui.Platform.ContentPanel CreatePlatformView() =>
        new ParkPanel { CrossPlatformLayout = VirtualView };
}
#endif
