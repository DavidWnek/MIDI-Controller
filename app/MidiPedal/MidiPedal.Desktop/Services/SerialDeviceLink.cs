using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Ports;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using MidiPedal.Services;

namespace MidiPedal.Desktop.Services;

/// <summary>USB CDC serial transport for bench work on Windows / macOS / Linux.</summary>
public sealed class SerialLinkProvider : IDeviceLinkProvider
{
    public string Transport => "USB";

    public Task<IReadOnlyList<DeviceEndpoint>> ScanAsync(TimeSpan timeout, CancellationToken ct = default)
    {
        var ports = SerialPort.GetPortNames()
            // macOS lists /dev/tty.* but the call-out /dev/cu.* device is the one to open.
            .Select(p => p.StartsWith("/dev/tty.", StringComparison.Ordinal) && File.Exists("/dev/cu." + p[9..]) ? "/dev/cu." + p[9..] : p)
            // Hide ports that are clearly not a pedal (Bluetooth, debug consoles).
            .Where(p => !p.Contains("Bluetooth", StringComparison.OrdinalIgnoreCase) && !p.Contains("debug-console", StringComparison.OrdinalIgnoreCase))
            .Distinct()
            .OrderBy(p => p.Length)
            .ThenBy(p => p, StringComparer.OrdinalIgnoreCase)
            .Select(p => new DeviceEndpoint(p, p, Transport))
            .ToList();
        return Task.FromResult<IReadOnlyList<DeviceEndpoint>>(ports);
    }

    public IDeviceLink Create(DeviceEndpoint endpoint) => new SerialDeviceLink(endpoint);
}

public sealed class SerialDeviceLink : IDeviceLink
{
    private SerialPort? _port;
    private Task? _reader;
    private readonly object _writeGate = new();

    public SerialDeviceLink(DeviceEndpoint endpoint) => Endpoint = endpoint;

    public DeviceEndpoint Endpoint { get; }
    public bool IsConnected => _port?.IsOpen == true;
    public event Action<string>? LineReceived;
    public event Action? Disconnected;

    public Task ConnectAsync(CancellationToken ct = default)
    {
        var port = new SerialPort(Endpoint.Id, 115200, Parity.None, 8, StopBits.One)
        {
            // The ESP32-S3 CDC stack only treats the host as attached when DTR and RTS are both
            // asserted (the bootloader reset needs a specific toggle sequence, so this is safe).
            DtrEnable = true,
            RtsEnable = true,
            Encoding = Encoding.UTF8,
            ReadTimeout = SerialPort.InfiniteTimeout,
            WriteTimeout = 2000,
        };
        port.Open();
        _port = port;
        _reader = Task.Run(() => ReadLoop(port));
        return Task.CompletedTask;
    }

    private void ReadLoop(SerialPort port)
    {
        var stream = port.BaseStream;
        var buffer = new byte[4096];
        var pending = new StringBuilder();
        try
        {
            while (port.IsOpen)
            {
                var n = stream.Read(buffer, 0, buffer.Length);
                if (n <= 0) break;
                for (var i = 0; i < n; i++)
                {
                    var b = buffer[i];
                    if (b == (byte)'\n')
                    {
                        var line = pending.ToString();
                        pending.Clear();
                        if (line.Length > 0) LineReceived?.Invoke(line);
                    }
                    else if (b != (byte)'\r')
                    {
                        pending.Append((char)b);
                    }
                }
            }
        }
        catch (Exception) when (!port.IsOpen)
        {
            // Closed by DisconnectAsync.
        }
        catch (Exception)
        {
            Disconnected?.Invoke();
        }
    }

    public Task SendLineAsync(string line, CancellationToken ct = default)
    {
        var port = _port ?? throw new InvalidOperationException("Not connected.");
        var bytes = Encoding.UTF8.GetBytes(line + "\n");
        lock (_writeGate)
        {
            port.BaseStream.Write(bytes, 0, bytes.Length);
            port.BaseStream.Flush();
        }
        return Task.CompletedTask;
    }

    public async Task DisconnectAsync()
    {
        var port = _port;
        _port = null;
        if (port == null) return;
        try { port.Close(); } catch (IOException) { }
        port.Dispose();
        if (_reader != null)
        {
            try { await _reader.WaitAsync(TimeSpan.FromSeconds(2)); } catch { }
        }
    }

    public async ValueTask DisposeAsync() => await DisconnectAsync();
}
