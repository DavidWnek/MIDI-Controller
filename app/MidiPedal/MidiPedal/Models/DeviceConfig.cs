using System.Collections.Generic;
using System.Text.Json.Serialization;

namespace MidiPedal.Models;

public static class Limits
{
    public const int Switches = 9;
    public const int MaxPages = 8;
    public const int MaxActions = 6;
    public const int MaxPresets = 8;
    public const int MaxNameLength = 16;
    public const int MaxDeviceNameLength = 24;
}

public static class SwitchModes
{
    public const string Momentary = "momentary";
    public const string Latch = "latch";
    public const string Page = "page";
    public const string Radio = "radio";
    public const string Bank = "bank";
    public static readonly string[] All = { Momentary, Latch, Radio, Bank, Page };
}

public static class MessageTypes
{
    public const string ControlChange = "cc";
    public const string Note = "note";
    public const string ProgramChange = "pc";
    public static readonly string[] All = { ControlChange, Note, ProgramChange };
}

public static class ActionTypes
{
    public const string ControlChange = "cc";
    public const string NoteOn = "noteon";
    public const string NoteOff = "noteoff";
    public const string ProgramChange = "pc";
    public const string Page = "page";
    public const string Preset = "preset";
    public static readonly string[] All = { ControlChange, NoteOn, NoteOff, ProgramChange, Page, Preset };
}

public static class PageTargets
{
    public const int Previous = -1;
    public const int Next = -2;
}

/// <summary>Output port bits, shared by per-switch routing and the thru matrix.</summary>
public static class Ports
{
    public const int Usb = 1;
    public const int Din = 2;
    public const int Net = 4;
    public const int All = 7;
}

public sealed class DeviceConfig
{
    [JsonPropertyName("v")] public int Version { get; set; } = 2;
    [JsonPropertyName("name")] public string Name { get; set; } = "MidiPedal";
    [JsonPropertyName("ch")] public int MidiChannel { get; set; } = 1;
    [JsonPropertyName("startPage")] public int StartPage { get; set; }
    [JsonPropertyName("brightness")] public int Brightness { get; set; } = 40;
    [JsonPropertyName("blePin")] public int BlePin { get; set; }
    [JsonPropertyName("thru")] public ThruConfig Thru { get; set; } = new();
    [JsonPropertyName("pages")] public List<PageConfig> Pages { get; set; } = new();

    public static readonly string[] DefaultColors =
    {
        "#FF4040", "#FF9000", "#FFE000", "#40FF40", "#00E0C0", "#4080FF", "#9040FF", "#FF40C0", "#FFFFFF",
    };

    public static DeviceConfig CreateDefault()
    {
        var cfg = new DeviceConfig();
        cfg.Pages.Add(PageConfig.CreateDefault(0));
        return cfg;
    }
}

/// <summary>For each input port, the bitmask of output ports its MIDI is forwarded to.</summary>
public sealed class ThruConfig
{
    [JsonPropertyName("usb")] public int Usb { get; set; } = Ports.Din;
    [JsonPropertyName("din")] public int Din { get; set; } = Ports.Usb;
    [JsonPropertyName("net")] public int Net { get; set; }
}

public sealed class PageConfig
{
    [JsonPropertyName("name")] public string Name { get; set; } = "Page 1";
    [JsonPropertyName("sw")] public List<SwitchConfig> Switches { get; set; } = new();

    public static PageConfig CreateDefault(int index)
    {
        var page = new PageConfig { Name = $"Page {index + 1}" };
        for (var i = 0; i < Limits.Switches; i++)
        {
            page.Switches.Add(new SwitchConfig
            {
                Name = $"SW{i + 1}",
                Number = 20 + i,
                Color = DeviceConfig.DefaultColors[i],
            });
        }
        return page;
    }
}

public sealed class MidiAction
{
    [JsonPropertyName("t")] public string Type { get; set; } = ActionTypes.ControlChange;
    [JsonPropertyName("ch")] public int Channel { get; set; }
    [JsonPropertyName("n")] public int Number { get; set; }
    [JsonPropertyName("v")] public int Value { get; set; } = 127;
}

public sealed class BankConfig
{
    [JsonPropertyName("step")] public int Step { get; set; } = 1;
    [JsonPropertyName("min")] public int Min { get; set; }
    [JsonPropertyName("max")] public int Max { get; set; } = 127;
    [JsonPropertyName("wrap")] public bool Wrap { get; set; } = true;
}

public sealed class SwitchConfig
{
    [JsonPropertyName("name")] public string Name { get; set; } = "SW";
    [JsonPropertyName("mode")] public string Mode { get; set; } = SwitchModes.Momentary;
    [JsonPropertyName("type")] public string Type { get; set; } = MessageTypes.ControlChange;
    [JsonPropertyName("ch")] public int Channel { get; set; }
    [JsonPropertyName("num")] public int Number { get; set; }
    [JsonPropertyName("on")] public int OnValue { get; set; } = 127;
    [JsonPropertyName("off")] public int OffValue { get; set; }
    [JsonPropertyName("page")] public int PageTarget { get; set; } = PageTargets.Next;
    [JsonPropertyName("color")] public string Color { get; set; } = "#FFFFFF";
    [JsonPropertyName("route")] public int Route { get; set; } = Ports.All;
    [JsonPropertyName("group")] public int Group { get; set; }
    [JsonPropertyName("bank")] public BankConfig Bank { get; set; } = new();
    [JsonPropertyName("extra")] public List<MidiAction> Extra { get; set; } = new();
    [JsonPropertyName("hold")] public List<MidiAction> Hold { get; set; } = new();
    [JsonPropertyName("tap2")] public List<MidiAction> DoubleTap { get; set; } = new();
}

public sealed class DeviceInfo
{
    [JsonPropertyName("fw")] public string Firmware { get; set; } = "";
    [JsonPropertyName("proto")] public int Protocol { get; set; }
    [JsonPropertyName("name")] public string Name { get; set; } = "";
    [JsonPropertyName("usb")] public bool UsbMounted { get; set; }
    [JsonPropertyName("eth")] public bool EthernetUp { get; set; }
    [JsonPropertyName("ip")] public string Ip { get; set; } = "";
    [JsonPropertyName("sessions")] public int RtpSessions { get; set; }
    [JsonPropertyName("ble")] public bool BleConnected { get; set; }
    [JsonPropertyName("bonds")] public int BleBonds { get; set; }
    [JsonPropertyName("secured")] public bool BleSecured { get; set; }
    [JsonPropertyName("page")] public int Page { get; set; }
    [JsonPropertyName("pages")] public int PageCount { get; set; }
    [JsonPropertyName("uptime")] public long UptimeSeconds { get; set; }
    [JsonPropertyName("heap")] public long FreeHeap { get; set; }
}

public sealed class PresetInfo
{
    [JsonPropertyName("slot")] public int Slot { get; set; }
    [JsonPropertyName("name")] public string Name { get; set; } = "";
    [JsonPropertyName("size")] public int Size { get; set; }
}
