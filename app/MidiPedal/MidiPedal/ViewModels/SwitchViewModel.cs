using System.Collections.ObjectModel;
using System.Collections.Specialized;
using System.ComponentModel;
using Avalonia.Media;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using MidiPedal.Models;

namespace MidiPedal.ViewModels;

public partial class SwitchViewModel : ViewModelBase
{
    public SwitchViewModel(int index)
    {
        Index = index;
        _name = $"SW{index + 1}";
        _number = 20 + index;
        _color = DeviceConfig.DefaultColors[index % DeviceConfig.DefaultColors.Length];
        Hook(Extra);
        Hook(Hold);
        Hook(DoubleTap);
    }

    public int Index { get; }
    public string Label => $"SW{Index + 1}";

    [ObservableProperty] private string _name;

    [ObservableProperty] private string _mode = SwitchModes.Momentary;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(Summary), nameof(IsControlChange), nameof(IsProgramChange), nameof(ShowOnValue), nameof(ShowOffValue))]
    private string _type = MessageTypes.ControlChange;

    [ObservableProperty] [NotifyPropertyChangedFor(nameof(Summary))] private int _channel;
    [ObservableProperty] [NotifyPropertyChangedFor(nameof(Summary))] private int _number;
    [ObservableProperty] private int _onValue = 127;
    [ObservableProperty] private int _offValue;
    [ObservableProperty] [NotifyPropertyChangedFor(nameof(Summary))] private int _pageTarget = PageTargets.Next;
    [ObservableProperty] [NotifyPropertyChangedFor(nameof(Brush))] private string _color;

    // Routing
    [ObservableProperty] [NotifyPropertyChangedFor(nameof(RouteSummary))] private bool _routeUsb = true;
    [ObservableProperty] [NotifyPropertyChangedFor(nameof(RouteSummary))] private bool _routeDin = true;
    [ObservableProperty] [NotifyPropertyChangedFor(nameof(RouteSummary))] private bool _routeNet = true;

    // Radio group / bank stepping
    [ObservableProperty] [NotifyPropertyChangedFor(nameof(Summary))] private int _group = 1;
    [ObservableProperty] [NotifyPropertyChangedFor(nameof(Summary))] private int _bankStep = 1;
    [ObservableProperty] private int _bankMin;
    [ObservableProperty] private int _bankMax = 127;
    [ObservableProperty] private bool _bankWrap = true;

    public ObservableCollection<ActionViewModel> Extra { get; } = new();
    public ObservableCollection<ActionViewModel> Hold { get; } = new();
    public ObservableCollection<ActionViewModel> DoubleTap { get; } = new();

    // Live state streamed from the pedal.
    [ObservableProperty] private bool _isDown;
    [ObservableProperty] private bool _isLatched;
    [ObservableProperty] private bool _isSelected;

    public bool IsPrimaryMode => Mode is SwitchModes.Momentary or SwitchModes.Latch or SwitchModes.Radio;
    public bool IsPageMode => Mode == SwitchModes.Page;
    public bool IsRadioMode => Mode == SwitchModes.Radio;
    public bool IsBankMode => Mode == SwitchModes.Bank;
    public bool IsControlChange => Type == MessageTypes.ControlChange;
    public bool IsProgramChange => Type == MessageTypes.ProgramChange;
    public bool ShowOnValue => IsPrimaryMode && !IsProgramChange;
    public bool ShowOffValue => IsPrimaryMode && IsControlChange;
    public bool CanAddExtra => Extra.Count < Limits.MaxActions;
    public bool CanAddHold => Hold.Count < Limits.MaxActions;
    public bool CanAddDoubleTap => DoubleTap.Count < Limits.MaxActions;
    public bool HasDoubleTap => DoubleTap.Count > 0;

    /// <summary>Bumped whenever an action list changes so the main view model can mark the config dirty.</summary>
    public int ActionsStamp { get; private set; }

    public IBrush Brush
    {
        get
        {
            try { return new SolidColorBrush(Avalonia.Media.Color.Parse(Color)); }
            catch { return Brushes.Gray; }
        }
    }

    public string RouteSummary
    {
        get
        {
            var parts = new System.Collections.Generic.List<string>(3);
            if (RouteUsb) parts.Add("USB");
            if (RouteDin) parts.Add("DIN");
            if (RouteNet) parts.Add("Net");
            return parts.Count == 0 ? "no outputs!" : parts.Count == 3 ? "" : string.Join("+", parts);
        }
    }

    public string Summary
    {
        get
        {
            var ch = Channel == 0 ? "global ch" : $"ch {Channel}";
            switch (Mode)
            {
                case SwitchModes.Page:
                    return PageTarget switch
                    {
                        PageTargets.Previous => "Previous page",
                        PageTargets.Next => "Next page",
                        _ => $"Go to page {PageTarget + 1}",
                    };
                case SwitchModes.Bank:
                    return $"PC {(BankStep >= 0 ? "+" : "")}{BankStep} · {ch}";
                default:
                {
                    var what = Type switch
                    {
                        MessageTypes.Note => $"Note {Number}",
                        MessageTypes.ProgramChange => $"PC {Number}",
                        _ => $"CC {Number}",
                    };
                    var mode = Mode == SwitchModes.Radio ? $"radio {Group}" : Mode;
                    var extras = Extra.Count > 0 ? $" +{Extra.Count}" : "";
                    return $"{what} · {ch} · {mode}{extras}";
                }
            }
        }
    }

    partial void OnModeChanged(string value)
    {
        OnPropertyChanged(nameof(IsPrimaryMode));
        OnPropertyChanged(nameof(IsPageMode));
        OnPropertyChanged(nameof(IsRadioMode));
        OnPropertyChanged(nameof(IsBankMode));
        OnPropertyChanged(nameof(ShowOnValue));
        OnPropertyChanged(nameof(ShowOffValue));
        OnPropertyChanged(nameof(Summary));
    }

    private void Hook(ObservableCollection<ActionViewModel> list)
    {
        list.CollectionChanged += (_, e) =>
        {
            if (e.NewItems != null) foreach (ActionViewModel a in e.NewItems) a.PropertyChanged += OnActionChanged;
            if (e.OldItems != null) foreach (ActionViewModel a in e.OldItems) a.PropertyChanged -= OnActionChanged;
            Touch();
            OnPropertyChanged(nameof(CanAddExtra));
            OnPropertyChanged(nameof(CanAddHold));
            OnPropertyChanged(nameof(CanAddDoubleTap));
            OnPropertyChanged(nameof(HasDoubleTap));
            OnPropertyChanged(nameof(Summary));
        };
    }

    private void OnActionChanged(object? sender, PropertyChangedEventArgs e) => Touch();

    private void Touch()
    {
        ActionsStamp++;
        OnPropertyChanged(nameof(ActionsStamp));
    }

    [RelayCommand] private void AddExtra() { if (CanAddExtra) Extra.Add(new ActionViewModel()); }
    [RelayCommand] private void AddHold() { if (CanAddHold) Hold.Add(new ActionViewModel()); }
    [RelayCommand] private void AddDoubleTap() { if (CanAddDoubleTap) DoubleTap.Add(new ActionViewModel()); }

    [RelayCommand]
    private void RemoveAction(ActionViewModel? a)
    {
        if (a == null) return;
        if (!Extra.Remove(a) && !Hold.Remove(a)) DoubleTap.Remove(a);
    }

    public void Load(SwitchConfig c)
    {
        Name = c.Name;
        Mode = c.Mode;
        Type = c.Type;
        Channel = c.Channel;
        Number = c.Number;
        OnValue = c.OnValue;
        OffValue = c.OffValue;
        PageTarget = c.PageTarget;
        Color = c.Color;
        RouteUsb = (c.Route & Ports.Usb) != 0;
        RouteDin = (c.Route & Ports.Din) != 0;
        RouteNet = (c.Route & Ports.Net) != 0;
        Group = c.Group == 0 ? 1 : c.Group;
        BankStep = c.Bank.Step;
        BankMin = c.Bank.Min;
        BankMax = c.Bank.Max;
        BankWrap = c.Bank.Wrap;
        LoadList(Extra, c.Extra);
        LoadList(Hold, c.Hold);
        LoadList(DoubleTap, c.DoubleTap);
        IsDown = false;
        IsLatched = false;
    }

    private static void LoadList(ObservableCollection<ActionViewModel> target, System.Collections.Generic.List<MidiAction> source)
    {
        target.Clear();
        foreach (var a in source)
        {
            var vm = new ActionViewModel();
            vm.Load(a);
            target.Add(vm);
        }
    }

    public SwitchConfig ToConfig()
    {
        var c = new SwitchConfig
        {
            Name = Truncate(Name, Limits.MaxNameLength),
            Mode = Mode,
            Type = Type,
            Channel = Channel,
            Number = Number,
            OnValue = OnValue,
            OffValue = OffValue,
            PageTarget = PageTarget,
            Color = Color,
            Route = (RouteUsb ? Ports.Usb : 0) | (RouteDin ? Ports.Din : 0) | (RouteNet ? Ports.Net : 0),
            Group = Mode == SwitchModes.Radio ? Group : 0,
            Bank = new BankConfig { Step = BankStep, Min = BankMin, Max = BankMax, Wrap = BankWrap },
        };
        foreach (var a in Extra) c.Extra.Add(a.ToModel());
        foreach (var a in Hold) c.Hold.Add(a.ToModel());
        foreach (var a in DoubleTap) c.DoubleTap.Add(a.ToModel());
        return c;
    }

    internal static string Truncate(string s, int max) => s.Length <= max ? s : s[..max];
}
