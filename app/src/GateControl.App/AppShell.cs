// AppShell.cs — five tabs; account and settings from the person button.
//
// Phones and tablets get a bottom tab bar (one translucent chrome layer
// over the content); Windows renders the same tabs as a top navigation
// bar. Settings lives behind the person button on every tab, as in
// Apple's own apps, so the tab bar stays at five.

using GateControl.Core.ViewModels;
using GateControl.Pages;

namespace GateControl;

public sealed class AppShell : Shell
{
    public AppShell(IServiceProvider services, LivePage live, DashboardPage dashboard, ReportsPage reports,
                    VehiclesPage vehicles, AlertsPage alerts)
    {
        FlyoutBehavior = FlyoutBehavior.Disabled;
        var bar = new TabBar();
        bar.Items.Add(Tab("Live", "tab_live.png", live, services));
        bar.Items.Add(Tab("Dashboard", "tab_dashboard.png", dashboard, services));
        bar.Items.Add(Tab("Reports", "tab_reports.png", reports, services));
        bar.Items.Add(Tab("Vehicles", "tab_vehicles.png", vehicles, services));
        var alertsTab = Tab("Alerts", "tab_alerts.png", alerts, services);
        bar.Items.Add(alertsTab);
        Items.Add(bar);

        // The alerts badge: the tab title carries the open count.
        if (alerts.BindingContext is AlertsViewModel avm)
        {
            avm.PropertyChanged += (_, e) =>
            {
                if (e.PropertyName == nameof(AlertsViewModel.Unread))
                {
                    alertsTab.Title = avm.Unread > 0 ? $"Alerts ({avm.Unread})" : "Alerts";
                }
            };
            _ = avm.RefreshUnreadAsync();
        }
    }

    private static Tab Tab(string title, string icon, ContentPage page, IServiceProvider services)
    {
        page.Title = title;
        page.ToolbarItems.Add(new ToolbarItem
        {
            Text = "Account",
            IconImageSource = "person.png",
            Order = ToolbarItemOrder.Primary,
            Command = new Command(async () =>
                await Current.Navigation.PushAsync(services.GetRequiredService<SettingsPage>())),
        });
        var tab = new Tab { Title = title, Icon = icon };
        tab.Items.Add(new ShellContent { Content = page, Title = title });
        return tab;
    }
}
