// PlateDetailViewModel.cs — everything known about one plate.

using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using GateControl.Core.Formatting;
using GateControl.Core.Models;
using GateControl.Core.Services;

namespace GateControl.Core.ViewModels;

public sealed partial class PlateDetailViewModel : ViewModelBase
{
    private readonly Session _session;

    public PlateDetailViewModel(Session session, string plate)
    {
        _session = session;
        RawPlate = plate;
        Plate = Display.Plate(plate);
    }

    public string RawPlate { get; }
    public string Plate { get; }

    public ObservableCollection<Metric> Facts { get; } = [];
    public ObservableCollection<DecisionRow> History { get; } = [];
    public ObservableCollection<SessionRow> Visits { get; } = [];

    [ObservableProperty] private string _registration = "";
    [ObservableProperty] private bool _isRegistered;

    [RelayCommand]
    public async Task LoadAsync()
    {
        await RunAsync(async () =>
        {
            var h = await _session.Api.PlateAsync(RawPlate);
            Facts.ReplaceWith(
            [
                new Metric("Visits", Display.Count(h.Visits), $"on {h.Days} days"),
                new Metric("Allowed", Display.Count(h.Authorized), "", Tone.Positive),
                new Metric("First seen", h.FirstSeen?.Local.ToString("d MMM yyyy") ?? "—", ""),
                new Metric("Last seen", h.LastSeen?.Local.ToString("g") ?? "—", ""),
            ]);
            History.ReplaceWith(h.History.Select(DecisionRow.From));
            Visits.ReplaceWith(h.Sessions.OrderByDescending(s => s.Entered?.TsMs).Take(20));
        });
        // Registration needs the operator role (the allowlist is PII).
        if (_session.CanOperate)
        {
            try
            {
                var token = "";
                AllowlistEntry? hit = null;
                do
                {
                    var page = await _session.Api.AllowlistAsync(500, token);
                    hit = page.Entries.FirstOrDefault(e => e.Plate == RawPlate);
                    token = page.NextPageToken;
                } while (hit is null && token.Length > 0);
                IsRegistered = hit is not null;
                Registration = hit is null
                    ? "Not registered"
                    : string.Join(" · ", new[] { hit.OwnerName, hit.OwnerUnit,
                        hit.AllowedClasses.Count == 0 ? "any vehicle"
                            : string.Join(", ", hit.AllowedClasses.Select(Display.VehicleClass)) }
                        .Where(s => s.Length > 0));
            }
            catch (Api.ApiException)
            {
                Registration = "";
            }
        }
    }
}
