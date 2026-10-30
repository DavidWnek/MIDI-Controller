using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;

namespace MidiPedal.Services;

/// <summary>Something we can talk to: a BLE peripheral or a serial port.</summary>
public sealed record DeviceEndpoint(string Id, string Name, string Transport)
{
    public override string ToString() => $"{Name} ({Transport})";
}

/// <summary>A raw line-oriented channel to one pedal. Lines never include the trailing newline.</summary>
public interface IDeviceLink : IAsyncDisposable
{
    DeviceEndpoint Endpoint { get; }
    bool IsConnected { get; }
    event Action<string>? LineReceived;
    event Action? Disconnected;
    Task ConnectAsync(CancellationToken ct = default);
    Task SendLineAsync(string line, CancellationToken ct = default);
    Task DisconnectAsync();
}

/// <summary>Per-platform discovery. Heads register one or more providers at startup.</summary>
public interface IDeviceLinkProvider
{
    string Transport { get; }
    Task<IReadOnlyList<DeviceEndpoint>> ScanAsync(TimeSpan timeout, CancellationToken ct = default);
    IDeviceLink Create(DeviceEndpoint endpoint);
}

public static class LinkProviders
{
    public static List<IDeviceLinkProvider> All { get; } = new();
}
