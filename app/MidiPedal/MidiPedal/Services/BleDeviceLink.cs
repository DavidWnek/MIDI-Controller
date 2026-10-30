using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Plugin.BLE;
using Plugin.BLE.Abstractions;
using Plugin.BLE.Abstractions.Contracts;
using Plugin.BLE.Abstractions.EventArgs;

namespace MidiPedal.Services;

/// <summary>BLE transport via Plugin.BLE. Used by the Android and iOS heads.</summary>
public sealed class BleLinkProvider : IDeviceLinkProvider
{
    public static readonly Guid ServiceUuid = Guid.Parse("7a1c0001-5e4d-4c1b-9a2f-0d6e8b3f1a10");
    public static readonly Guid RxUuid = Guid.Parse("7a1c0002-5e4d-4c1b-9a2f-0d6e8b3f1a10");
    public static readonly Guid TxUuid = Guid.Parse("7a1c0003-5e4d-4c1b-9a2f-0d6e8b3f1a10");

    private readonly ConcurrentDictionary<string, IDevice> _seen = new();

    public string Transport => "BLE";

    public async Task<IReadOnlyList<DeviceEndpoint>> ScanAsync(TimeSpan timeout, CancellationToken ct = default)
    {
        var adapter = CrossBluetoothLE.Current.Adapter;
        var found = new List<DeviceEndpoint>();

        void OnDiscovered(object? s, DeviceEventArgs e)
        {
            var id = e.Device.Id.ToString();
            _seen[id] = e.Device;
            var name = string.IsNullOrWhiteSpace(e.Device.Name) ? "MidiPedal" : e.Device.Name;
            lock (found)
            {
                if (!found.Exists(f => f.Id == id)) found.Add(new DeviceEndpoint(id, name, Transport));
            }
        }

        adapter.DeviceDiscovered += OnDiscovered;
        try
        {
            adapter.ScanTimeout = (int)timeout.TotalMilliseconds;
            var filter = new ScanFilterOptions { ServiceUuids = new[] { ServiceUuid } };
            await adapter.StartScanningForDevicesAsync(filter, cancellationToken: ct);
        }
        finally
        {
            adapter.DeviceDiscovered -= OnDiscovered;
        }
        lock (found) return found.ToArray();
    }

    public IDeviceLink Create(DeviceEndpoint endpoint)
    {
        if (!_seen.TryGetValue(endpoint.Id, out var device))
            throw new InvalidOperationException("Scan again before connecting.");
        return new BleDeviceLink(endpoint, device);
    }
}

public sealed class BleDeviceLink : IDeviceLink
{
    private readonly IDevice _device;
    private ICharacteristic? _rx;
    private ICharacteristic? _tx;
    private readonly StringBuilder _pending = new();
    private int _mtu = 23;

    public BleDeviceLink(DeviceEndpoint endpoint, IDevice device)
    {
        Endpoint = endpoint;
        _device = device;
    }

    public DeviceEndpoint Endpoint { get; }
    public bool IsConnected => _device.State == DeviceState.Connected && _rx != null;
    public event Action<string>? LineReceived;
    public event Action? Disconnected;

    public async Task ConnectAsync(CancellationToken ct = default)
    {
        var adapter = CrossBluetoothLE.Current.Adapter;
        adapter.DeviceConnectionLost += OnLost;
        adapter.DeviceDisconnected += OnLost;
        await adapter.ConnectToDeviceAsync(_device, new ConnectParameters(autoConnect: false, forceBleTransport: true), ct);
        try { _mtu = await _device.RequestMtuAsync(517); } catch { _mtu = 23; }

        var service = await _device.GetServiceAsync(BleLinkProvider.ServiceUuid, ct)
                      ?? throw new InvalidOperationException("MidiPedal service not found on this device.");
        _rx = await service.GetCharacteristicAsync(BleLinkProvider.RxUuid)
              ?? throw new InvalidOperationException("RX characteristic missing.");
        _tx = await service.GetCharacteristicAsync(BleLinkProvider.TxUuid)
              ?? throw new InvalidOperationException("TX characteristic missing.");
        _rx.WriteType = CharacteristicWriteType.WithResponse;
        _tx.ValueUpdated += OnValueUpdated;
        await _tx.StartUpdatesAsync(ct);
    }

    private void OnLost(object? sender, DeviceEventArgs e)
    {
        if (e.Device.Id == _device.Id) Disconnected?.Invoke();
    }

    private void OnValueUpdated(object? sender, CharacteristicUpdatedEventArgs e)
    {
        var bytes = e.Characteristic.Value;
        if (bytes == null) return;
        foreach (var b in bytes)
        {
            if (b == (byte)'\n')
            {
                var line = _pending.ToString();
                _pending.Clear();
                if (line.Length > 0) LineReceived?.Invoke(line);
            }
            else if (b != (byte)'\r')
            {
                _pending.Append((char)b);
            }
        }
    }

    public async Task SendLineAsync(string line, CancellationToken ct = default)
    {
        if (_rx == null) throw new InvalidOperationException("Not connected.");
        var bytes = Encoding.UTF8.GetBytes(line + "\n");
        var chunk = Math.Clamp(_mtu - 3, 20, 244);
        for (var offset = 0; offset < bytes.Length; offset += chunk)
        {
            var n = Math.Min(chunk, bytes.Length - offset);
            var part = new byte[n];
            Array.Copy(bytes, offset, part, 0, n);
            await _rx.WriteAsync(part, ct);
        }
    }

    public async Task DisconnectAsync()
    {
        var adapter = CrossBluetoothLE.Current.Adapter;
        adapter.DeviceConnectionLost -= OnLost;
        adapter.DeviceDisconnected -= OnLost;
        if (_tx != null)
        {
            _tx.ValueUpdated -= OnValueUpdated;
            try { await _tx.StopUpdatesAsync(); } catch { }
        }
        _rx = null;
        _tx = null;
        try { await adapter.DisconnectDeviceAsync(_device); } catch { }
    }

    public async ValueTask DisposeAsync() => await DisconnectAsync();
}
