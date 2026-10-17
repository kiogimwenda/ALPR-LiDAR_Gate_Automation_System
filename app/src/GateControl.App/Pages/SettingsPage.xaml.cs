using System.Windows.Input;
using GateControl.Core.ViewModels;

namespace GateControl.Pages;

public partial class SettingsPage : ContentPage
{
    private readonly IServiceProvider _services;

    public SettingsPage(SettingsViewModel vm, IServiceProvider services)
    {
        _services = services;
        OpenUsersCommand = new Command(async () => await Push<UsersPage>());
        OpenGatesCommand = new Command(async () => await Push<GatesPage>());
        OpenDataCommand = new Command(async () => await Push<DataPage>());
        InitializeComponent();
        BindingContext = vm;
        AppearancePicker.Items = ["Automatic", "Light", "Dark"];
        VersionLabel.Text = $"Gate Control {AppInfo.Current.VersionString} ({AppInfo.Current.BuildString})";
    }

    public ICommand OpenUsersCommand { get; }
    public ICommand OpenGatesCommand { get; }
    public ICommand OpenDataCommand { get; }

    private Task Push<T>() where T : Page => Navigation.PushAsync(_services.GetRequiredService<T>());
}
