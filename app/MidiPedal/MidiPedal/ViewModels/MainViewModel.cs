using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Linq;
using System.Text.Json;
using System.Threading.Tasks;
using Avalonia.Threading;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using MidiPedal.Models;
using MidiPedal.Services;

namespace MidiPedal.ViewModels;

public sealed record PageTargetOption(string Label, int Value);
public sealed record ColorOption(string Name, string Hex);

public partial class PresetSlotViewModel : ViewModelBase
{
    public PresetSlotViewModel(int slot) => Slot = slot;
    public int Slot { get; }
    [ObservableProperty] [NotifyPropertyChangedFor(nameof(Title), nameof(IsEmpty))] private string? _name;
    public bool IsEmpty => Name == null;
    public string Title => Name == null ? $"{Slot + 1}: (empty)" : $"{Slot + 1}: {Name}";
}

public partial class MainViewModel : ViewModelBase
{
    private DeviceSession? _session;
    private bool _loading;

    public MainViewModel()
    {
        Transports = LinkProviders.All.Count == 0
            ? "no transport on this platform"
            : string.Join(" / ", LinkProviders.All.Select(p => p.Transport));
        for (var i = 0; i < Limits.MaxPresets; i++) Presets.Add(new PresetSlotViewModel(i));
        LoadConfig(DeviceConfig.CreateDefault());
        IsDirty = false;
    }

    // ------------------------------------------------------------------ connection

    public string Transports { get; }
    public ObservableCollection<DeviceEndpoint> Devices { get; } = new();

    [ObservableProperty] private DeviceEndpoint? _selectedDevice;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(IsDisconnected))]
    [NotifyCanExecuteChangedFor(nameof(SaveCommand), nameof(ReloadCommand), nameof(FactoryResetCommand),
        nameof(ActivatePageCommand), nameof(TestSwitchCommand), nameof(RefreshInfoCommand), nameof(RebootCommand),
        nameof(UnpairCommand), nameof(RefreshPresetsCommand), nameof(SavePresetCommand), nameof(LoadPresetCommand),
        nameof(DeletePresetCommand))]
    private bool _isConnected;

    [ObservableProperty] private bool _isBusy;
    [ObservableProperty] private string _statusText = "Not connected";
    [ObservableProperty] private bool _isDirty;
    [ObservableProperty] private bool _rebootNeeded;
    [ObservableProperty] private bool _followPedalPage = true;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(UsbText), nameof(EthText), nameof(SessionsText), nameof(BleText), nameof(FirmwareText))]
    private DeviceInfo? _info;

    public bool IsDisconnected => !IsConnected;
    public string UsbText => Info == null ? "" : Info.UsbMounted ? "USB MIDI: host connected" : "USB MIDI: no host";
    public string EthText => Info == null ? "" : Info.EthernetUp ? $"Ethernet: {Info.Ip}" : "Ethernet: no link";
    public string SessionsText => Info == null ? "" : $"RTP-MIDI sessions: {Info.RtpSessions}";
    public string BleText => Info == null ? "" : (Info.BleConnected ? "BLE: app connected" : "BLE: advertising") + (Info.BleSecured ? $" · {Info.BleBonds} paired" : " · open");
    public string FirmwareText => Info == null ? "" : $"fw {Info.Firmware}";

    // ------------------------------------------------------------------ configuration

    [ObservableProperty] private string _deviceName = "MidiPedal";
    [ObservableProperty] private int _midiChannel = 1;
    [ObservableProperty] private int _brightness = 40;
    [ObservableProperty] private int _blePin;

    // Thru matrix: source -> destination
    [ObservableProperty] private bool _thruUsbDin = true;
    [ObservableProperty] private bool _thruUsbNet;
    [ObservableProperty] private bool _thruDinUsb = true;
    [ObservableProperty] private bool _thruDinNet;
    [ObservableProperty] private bool _thruNetUsb;
    [ObservableProperty] private bool _thruNetDin;

    public ObservableCollection<PageViewModel> Pages { get; } = new();

    [ObservableProperty]
    [NotifyCanExecuteChangedFor(nameof(RemovePageCommand), nameof(ActivatePageCommand))]
    private PageViewModel? _selectedPage;

    [ObservableProperty] private SwitchViewModel? _selectedSwitch;

    public ObservableCollection<string> Log { get; } = new();
    public ObservableCollection<PresetSlotViewModel> Presets { get; } = new();
    [ObservableProperty] private PresetSlotViewModel? _selectedPreset;
    [ObservableProperty] private string _presetName = "";
    [ObservableProperty] private string _bankText = "";

    // Firmware update
    [ObservableProperty] private bool _isUpdating;
    [ObservableProperty] private double _updateProgress;

    public string[] ModeOptions => SwitchModes.All;
    public string[] TypeOptions => MessageTypes.All;
    public string[] ActionTypeOptions => ActionTypes.All;

    public PageTargetOption[] PageTargetOptions { get; } =
        new[] { new PageTargetOption("Next page", PageTargets.Next), new PageTargetOption("Previous page", PageTargets.Previous) }
            .Concat(Enumerable.Range(0, Limits.MaxPages).Select(i => new PageTargetOption($"Page {i + 1}", i)))
            .ToArray();

    public ColorOption[] ColorOptions { get; } =
    {
        new("Red", "#FF4040"), new("Orange", "#FF9000"), new("Yellow", "#FFE000"), new("Green", "#40FF40"),
        new("Teal", "#00E0C0"), new("Blue", "#4080FF"), new("Purple", "#9040FF"), new("Pink", "#FF40C0"),
        new("White", "#FFFFFF"),
    };

    partial void OnDeviceNameChanged(string value) => MarkDirty();
    partial void OnMidiChannelChanged(int value) => MarkDirty();
    partial void OnBrightnessChanged(int value) => MarkDirty();
    partial void OnBlePinChanged(int value) => MarkDirty();
    partial void OnThruUsbDinChanged(bool value) => MarkDirty();
    partial void OnThruUsbNetChanged(bool value) => MarkDirty();
    partial void OnThruDinUsbChanged(bool value) => MarkDirty();
    partial void OnThruDinNetChanged(bool value) => MarkDirty();
    partial void OnThruNetUsbChanged(bool value) => MarkDirty();
    partial void OnThruNetDinChanged(bool value) => MarkDirty();

    partial void OnSelectedPageChanged(PageViewModel? oldValue, PageViewModel? newValue)
    {
        if (oldValue != null) oldValue.IsSelected = false;
        if (newValue != null) newValue.IsSelected = true;
        SelectedSwitch = newValue?.Switches.FirstOrDefault(s => s.IsSelected) ?? newValue?.Switches.FirstOrDefault();
    }

    partial void OnSelectedSwitchChanged(SwitchViewModel? oldValue, SwitchViewModel? newValue)
    {
        if (oldValue != null) oldValue.IsSelected = false;
        if (newValue != null) newValue.IsSelected = true;
    }

    private static readonly HashSet<string> LiveOnlyProperties = new()
    {
        "IsDown", "IsLatched", "IsSelected", "IsActive", "Brush", "Summary", "Title", "RouteSummary",
        "IsPageMode", "IsPrimaryMode", "IsRadioMode", "IsBankMode", "IsControlChange", "IsProgramChange",
        "ShowOnValue", "ShowOffValue", "CanAddExtra", "CanAddHold", "CanAddDoubleTap", "HasDoubleTap",
    };

    private void OnChildChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName != null && LiveOnlyProperties.Contains(e.PropertyName)) return;
        MarkDirty();
    }

    private void MarkDirty()
    {
        if (!_loading) IsDirty = true;
    }

    private void LoadConfig(DeviceConfig cfg)
    {
        _loading = true;
        try
        {
            foreach (var p in Pages) Detach(p);
            Pages.Clear();
            DeviceName = cfg.Name;
            MidiChannel = cfg.MidiChannel;
            Brightness = cfg.Brightness;
            BlePin = cfg.BlePin;
            ThruUsbDin = (cfg.Thru.Usb & Ports.Din) != 0;
            ThruUsbNet = (cfg.Thru.Usb & Ports.Net) != 0;
            ThruDinUsb = (cfg.Thru.Din & Ports.Usb) != 0;
            ThruDinNet = (cfg.Thru.Din & Ports.Net) != 0;
            ThruNetUsb = (cfg.Thru.Net & Ports.Usb) != 0;
            ThruNetDin = (cfg.Thru.Net & Ports.Din) != 0;
            for (var i = 0; i < cfg.Pages.Count && i < Limits.MaxPages; i++)
            {
                var vm = new PageViewModel(i);
                vm.Load(cfg.Pages[i]);
                Attach(vm);
                Pages.Add(vm);
            }
            if (Pages.Count == 0)
            {
                var vm = new PageViewModel(0);
                Attach(vm);
                Pages.Add(vm);
            }
            SelectedPage = Pages[Math.Clamp(cfg.StartPage, 0, Pages.Count - 1)];
            SetActivePage(cfg.StartPage);
            IsDirty = false;
            RebootNeeded = false;
        }
        finally
        {
            _loading = false;
        }
    }

    private void Attach(PageViewModel vm)
    {
        vm.PropertyChanged += OnChildChanged;
        foreach (var s in vm.Switches) s.PropertyChanged += OnChildChanged;
    }

    private void Detach(PageViewModel vm)
    {
        vm.PropertyChanged -= OnChildChanged;
        foreach (var s in vm.Switches) s.PropertyChanged -= OnChildChanged;
    }

    public DeviceConfig BuildConfig()
    {
        var cfg = new DeviceConfig
        {
            Name = SwitchViewModel.Truncate(DeviceName.Trim().Length == 0 ? "MidiPedal" : DeviceName.Trim(), Limits.MaxDeviceNameLength),
            MidiChannel = Math.Clamp(MidiChannel, 1, 16),
            Brightness = Math.Clamp(Brightness, 0, 255),
            BlePin = Math.Clamp(BlePin, 0, 999999),
            StartPage = Math.Clamp(Pages.FirstOrDefault(p => p.IsActive)?.Index ?? 0, 0, Math.Max(0, Pages.Count - 1)),
            Thru = new ThruConfig
            {
                Usb = (ThruUsbDin ? Ports.Din : 0) | (ThruUsbNet ? Ports.Net : 0),
                Din = (ThruDinUsb ? Ports.Usb : 0) | (ThruDinNet ? Ports.Net : 0),
                Net = (ThruNetUsb ? Ports.Usb : 0) | (ThruNetDin ? Ports.Din : 0),
            },
        };
        foreach (var p in Pages) cfg.Pages.Add(p.ToConfig());
        return cfg;
    }

    private void SetActivePage(int index)
    {
        foreach (var p in Pages) p.IsActive = p.Index == index;
        if (FollowPedalPage && index >= 0 && index < Pages.Count) SelectedPage = Pages[index];
    }

    private void Renumber()
    {
        for (var i = 0; i < Pages.Count; i++) Pages[i].Index = i;
    }

    // ------------------------------------------------------------------ import / export

    public string ExportJson()
        => JsonSerializer.Serialize(BuildConfig(), new JsonSerializerOptions(DeviceSession.JsonOptions) { WriteIndented = true });

    public bool ImportJson(string json, out string error)
    {
        error = "";
        try
        {
            var cfg = JsonSerializer.Deserialize<DeviceConfig>(json, DeviceSession.JsonOptions);
            if (cfg == null || cfg.Pages.Count == 0) { error = "Not a MidiPedal configuration."; return false; }
            LoadConfig(cfg);
            IsDirty = true;
            StatusText = "Configuration imported, not yet saved to the pedal";
            return true;
        }
        catch (JsonException ex)
        {
            error = ex.Message;
            return false;
        }
    }

    // ------------------------------------------------------------------ commands: connection

    [RelayCommand]
    private async Task ScanAsync()
    {
        IsBusy = true;
        StatusText = "Scanning…";
        try
        {
            Devices.Clear();
            foreach (var provider in LinkProviders.All)
            {
                try
                {
                    foreach (var ep in await provider.ScanAsync(TimeSpan.FromSeconds(5))) Devices.Add(ep);
                }
                catch (Exception ex)
                {
                    AddLog($"{provider.Transport} scan failed: {ex.Message}");
                }
            }
            SelectedDevice ??= Devices.FirstOrDefault();
            StatusText = Devices.Count == 0 ? $"Nothing found ({Transports})" : $"{Devices.Count} device(s) found";
        }
        finally
        {
            IsBusy = false;
        }
    }

    [RelayCommand]
    private async Task ConnectAsync()
    {
        if (SelectedDevice == null)
        {
            await ScanAsync();
            if (SelectedDevice == null) return;
        }
        var provider = LinkProviders.All.FirstOrDefault(p => p.Transport == SelectedDevice.Transport);
        if (provider == null) { StatusText = "No provider for " + SelectedDevice.Transport; return; }

        IsBusy = true;
        StatusText = $"Connecting to {SelectedDevice.Name}…";
        DeviceSession? session = null;
        try
        {
            var link = provider.Create(SelectedDevice);
            session = new DeviceSession(link);
            session.RawLine += line => Dispatcher.UIThread.Post(() => AddLog("< " + line));
            session.EventReceived += (ev, payload) => Dispatcher.UIThread.Post(() => OnDeviceEvent(ev, payload));
            link.Disconnected += () => Dispatcher.UIThread.Post(OnLinkLost);
            await link.ConnectAsync();
            await Task.Delay(250);

            var info = await session.HelloAsync();
            if (info.Protocol > 2) AddLog($"Pedal speaks protocol {info.Protocol}; this app knows 2. Update the app.");
            var cfg = await session.GetConfigAsync();
            _session = session;
            Info = info;
            LoadConfig(cfg);
            SetActivePage(info.Page);
            IsConnected = true;
            StatusText = $"Connected to {info.Name}";
            await ApplyStateSnapshotAsync();
            await RefreshPresetsAsync();
        }
        catch (Exception ex)
        {
            StatusText = "Connect failed: " + ex.Message;
            AddLog(StatusText);
            if (session != null) await session.DisposeAsync();
        }
        finally
        {
            IsBusy = false;
        }
    }

    private async Task ApplyStateSnapshotAsync()
    {
        if (_session == null) return;
        try
        {
            var st = await _session.GetStateAsync();
            if (st.TryGetProperty("latched", out var latched))
            {
                foreach (var e in latched.EnumerateArray())
                {
                    var p = e.GetProperty("page").GetInt32();
                    var i = e.GetProperty("i").GetInt32();
                    if (p < Pages.Count && i < Limits.Switches) Pages[p].Switches[i].IsLatched = true;
                }
            }
            if (st.TryGetProperty("banks", out var banks))
            {
                var parts = banks.EnumerateArray().Select(b => $"ch {b.GetProperty("ch").GetInt32()}: PC {b.GetProperty("value").GetInt32()}");
                BankText = string.Join("   ", parts);
            }
        }
        catch (Exception ex)
        {
            AddLog("state: " + ex.Message);
        }
    }

    [RelayCommand]
    private async Task DisconnectAsync()
    {
        var s = _session;
        _session = null;
        IsConnected = false;
        Info = null;
        StatusText = "Not connected";
        if (s != null) await s.DisposeAsync();
    }

    private void OnLinkLost()
    {
        if (_session == null) return;
        _ = DisconnectAsync();
        StatusText = "Connection lost";
    }

    // ------------------------------------------------------------------ commands: config

    private bool CanTalk() => IsConnected && _session != null && !IsUpdating;

    [RelayCommand(CanExecute = nameof(CanTalk))]
    private async Task SaveAsync()
    {
        if (_session == null) return;
        IsBusy = true;
        try
        {
            var needsReboot = await _session.SetConfigAsync(BuildConfig());
            IsDirty = false;
            RebootNeeded = needsReboot;
            Info = await _session.GetInfoAsync();
            StatusText = needsReboot ? "Saved. Reboot the pedal to apply the new name / passkey." : "Saved to pedal";
        }
        catch (Exception ex)
        {
            StatusText = "Save failed: " + ex.Message;
            AddLog(StatusText);
        }
        finally
        {
            IsBusy = false;
        }
    }

    [RelayCommand(CanExecute = nameof(CanTalk))]
    private async Task ReloadAsync()
    {
        if (_session == null) return;
        IsBusy = true;
        try
        {
            var info = await _session.GetInfoAsync();
            LoadConfig(await _session.GetConfigAsync());
            Info = info;
            SetActivePage(info.Page);
            await ApplyStateSnapshotAsync();
            StatusText = "Reloaded from pedal";
        }
        catch (Exception ex)
        {
            StatusText = "Reload failed: " + ex.Message;
        }
        finally
        {
            IsBusy = false;
        }
    }

    [RelayCommand(CanExecute = nameof(CanTalk))]
    private async Task FactoryResetAsync()
    {
        if (_session == null) return;
        try
        {
            await _session.FactoryResetAsync();
            await ReloadAsync();
            StatusText = "Factory defaults restored";
        }
        catch (Exception ex)
        {
            StatusText = "Reset failed: " + ex.Message;
        }
    }

    [RelayCommand(CanExecute = nameof(CanTalk))]
    private async Task RebootAsync()
    {
        if (_session == null) return;
        try
        {
            await _session.RebootAsync();
            StatusText = "Pedal rebooting, reconnect in a few seconds";
            await DisconnectAsync();
        }
        catch (Exception ex)
        {
            StatusText = ex.Message;
        }
    }

    [RelayCommand(CanExecute = nameof(CanTalk))]
    private async Task UnpairAsync()
    {
        if (_session == null) return;
        try
        {
            await _session.UnpairAsync();
            Info = await _session.GetInfoAsync();
            StatusText = "All BLE pairings forgotten";
        }
        catch (Exception ex)
        {
            StatusText = ex.Message;
        }
    }

    [RelayCommand(CanExecute = nameof(CanTalk))]
    private async Task RefreshInfoAsync()
    {
        if (_session == null) return;
        try { Info = await _session.GetInfoAsync(); }
        catch (Exception ex) { StatusText = ex.Message; }
    }

    private bool CanActivatePage() => CanTalk() && SelectedPage != null;

    [RelayCommand(CanExecute = nameof(CanActivatePage))]
    private async Task ActivatePageAsync()
    {
        if (_session == null || SelectedPage == null) return;
        try { await _session.SetPageAsync(SelectedPage.Index); }
        catch (Exception ex) { StatusText = ex.Message; }
    }

    [RelayCommand(CanExecute = nameof(CanTalk))]
    private async Task TestSwitchAsync(SwitchViewModel? sw)
    {
        if (_session == null || sw == null) return;
        try
        {
            await _session.PressAsync(sw.Index);
            await Task.Delay(150);
            await _session.ReleaseAsync(sw.Index);
        }
        catch (Exception ex) { StatusText = ex.Message; }
    }

    [RelayCommand]
    private void SelectPage(PageViewModel? page)
    {
        if (page != null) SelectedPage = page;
    }

    [RelayCommand]
    private void SelectSwitch(SwitchViewModel? sw)
    {
        if (sw != null) SelectedSwitch = sw;
    }

    private bool CanAddPage() => Pages.Count < Limits.MaxPages;

    [RelayCommand(CanExecute = nameof(CanAddPage))]
    private void AddPage()
    {
        var vm = new PageViewModel(Pages.Count);
        Attach(vm);
        Pages.Add(vm);
        SelectedPage = vm;
        MarkDirty();
        AddPageCommand.NotifyCanExecuteChanged();
        RemovePageCommand.NotifyCanExecuteChanged();
    }

    private bool CanRemovePage() => SelectedPage != null && Pages.Count > 1;

    [RelayCommand(CanExecute = nameof(CanRemovePage))]
    private void RemovePage()
    {
        if (SelectedPage == null || Pages.Count <= 1) return;
        var idx = SelectedPage.Index;
        Detach(SelectedPage);
        Pages.RemoveAt(idx);
        Renumber();
        SelectedPage = Pages[Math.Min(idx, Pages.Count - 1)];
        if (!Pages.Any(p => p.IsActive)) Pages[0].IsActive = true;
        MarkDirty();
        AddPageCommand.NotifyCanExecuteChanged();
        RemovePageCommand.NotifyCanExecuteChanged();
    }

    [RelayCommand]
    private void ClearLog() => Log.Clear();

    // ------------------------------------------------------------------ presets

    [RelayCommand(CanExecute = nameof(CanTalk))]
    private async Task RefreshPresetsAsync()
    {
        if (_session == null) return;
        try
        {
            var list = await _session.GetPresetsAsync();
            foreach (var slot in Presets) slot.Name = list.FirstOrDefault(p => p.Slot == slot.Slot)?.Name;
        }
        catch (Exception ex)
        {
            AddLog("presets: " + ex.Message);
        }
    }

    private bool CanUsePreset() => CanTalk() && SelectedPreset != null;

    [RelayCommand(CanExecute = nameof(CanTalk))]
    private async Task SavePresetAsync()
    {
        if (_session == null || SelectedPreset == null) return;
        try
        {
            if (IsDirty) await SaveAsync();
            var name = string.IsNullOrWhiteSpace(PresetName) ? $"Preset {SelectedPreset.Slot + 1}" : PresetName.Trim();
            await _session.SavePresetAsync(SelectedPreset.Slot, SwitchViewModel.Truncate(name, Limits.MaxNameLength));
            await RefreshPresetsAsync();
            StatusText = $"Saved preset {SelectedPreset.Slot + 1}";
        }
        catch (Exception ex)
        {
            StatusText = "Preset save failed: " + ex.Message;
        }
    }

    [RelayCommand(CanExecute = nameof(CanTalk))]
    private async Task LoadPresetAsync()
    {
        if (_session == null || SelectedPreset == null || SelectedPreset.IsEmpty) return;
        try
        {
            await _session.LoadPresetAsync(SelectedPreset.Slot);
            await ReloadAsync();
            StatusText = $"Loaded preset {SelectedPreset.Slot + 1}";
        }
        catch (Exception ex)
        {
            StatusText = "Preset load failed: " + ex.Message;
        }
    }

    [RelayCommand(CanExecute = nameof(CanTalk))]
    private async Task DeletePresetAsync()
    {
        if (_session == null || SelectedPreset == null || SelectedPreset.IsEmpty) return;
        try
        {
            await _session.DeletePresetAsync(SelectedPreset.Slot);
            await RefreshPresetsAsync();
        }
        catch (Exception ex)
        {
            StatusText = "Preset delete failed: " + ex.Message;
        }
    }

    partial void OnSelectedPresetChanged(PresetSlotViewModel? value)
    {
        if (value?.Name != null) PresetName = value.Name;
    }

    // ------------------------------------------------------------------ firmware

    public async Task UpdateFirmwareAsync(byte[] image)
    {
        if (_session == null) { StatusText = "Connect to the pedal first"; return; }
        IsUpdating = true;
        UpdateProgress = 0;
        SaveCommand.NotifyCanExecuteChanged();
        try
        {
            var progress = new Progress<double>(p => UpdateProgress = p * 100);
            StatusText = $"Sending firmware ({image.Length / 1024} KB)…";
            await _session.UpdateFirmwareAsync(image, progress);
            StatusText = "Firmware written, pedal is rebooting";
            await DisconnectAsync();
        }
        catch (Exception ex)
        {
            StatusText = "Firmware update failed: " + ex.Message;
            AddLog(StatusText);
        }
        finally
        {
            IsUpdating = false;
            SaveCommand.NotifyCanExecuteChanged();
        }
    }

    // ------------------------------------------------------------------ events from the pedal

    private void OnDeviceEvent(string ev, JsonElement payload)
    {
        switch (ev)
        {
            case "switch":
            {
                var i = payload.GetProperty("i").GetInt32();
                var down = payload.GetProperty("down").GetBoolean();
                var page = Pages.FirstOrDefault(p => p.IsActive);
                if (page != null && i >= 0 && i < page.Switches.Count) page.Switches[i].IsDown = down;
                break;
            }
            case "latch":
            {
                var p = payload.GetProperty("page").GetInt32();
                var i = payload.GetProperty("i").GetInt32();
                var on = payload.GetProperty("on").GetBoolean();
                if (p >= 0 && p < Pages.Count && i >= 0 && i < Limits.Switches) Pages[p].Switches[i].IsLatched = on;
                break;
            }
            case "page":
                SetActivePage(payload.GetProperty("index").GetInt32());
                break;
            case "bank":
                BankText = $"ch {payload.GetProperty("ch").GetInt32()}: PC {payload.GetProperty("value").GetInt32()}";
                break;
            case "link":
                _ = RefreshInfoAsync();
                break;
            case "configChanged":
                StatusText = "Config changed on the pedal, reloading";
                _ = ReloadAsync();
                break;
        }
    }

    private void AddLog(string line)
    {
        Log.Add(line.Length > 300 ? line[..300] + "…" : line);
        while (Log.Count > 200) Log.RemoveAt(0);
    }
}
