using System;
using System.Collections.Generic;
using System.Security.Cryptography;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using MidiPedal.Models;

namespace MidiPedal.Services;

public sealed class DeviceException : Exception
{
    public DeviceException(string message) : base(message) { }
}

/// <summary>Request/response and event plumbing for the MidiPedal JSON protocol on top of any link.</summary>
public sealed class DeviceSession : IAsyncDisposable
{
    public static readonly JsonSerializerOptions JsonOptions = new()
    {
        DefaultIgnoreCondition = System.Text.Json.Serialization.JsonIgnoreCondition.Never,
        WriteIndented = false,
    };

    private readonly List<(string Ev, TaskCompletionSource<JsonElement> Tcs)> _pending = new();
    private readonly object _gate = new();
    private readonly SemaphoreSlim _requestGate = new(1, 1);

    public DeviceSession(IDeviceLink link)
    {
        Link = link;
        link.LineReceived += OnLine;
    }

    public IDeviceLink Link { get; }

    /// <summary>Unsolicited events: switch, latch, page, long, tap2, bank, link, configChanged.</summary>
    public event Action<string, JsonElement>? EventReceived;
    /// <summary>Every raw line, for the log pane.</summary>
    public event Action<string>? RawLine;

    private void OnLine(string line)
    {
        RawLine?.Invoke(line);
        if (!line.StartsWith('{')) return;

        JsonElement root;
        try
        {
            using var doc = JsonDocument.Parse(line);
            root = doc.RootElement.Clone();
        }
        catch (JsonException) { return; }

        if (!root.TryGetProperty("ev", out var evProp)) return;
        var ev = evProp.GetString() ?? "";

        TaskCompletionSource<JsonElement>? match = null;
        lock (_gate)
        {
            var idx = ev == "error" ? (_pending.Count > 0 ? 0 : -1) : _pending.FindIndex(p => p.Ev == ev);
            if (idx >= 0)
            {
                match = _pending[idx].Tcs;
                _pending.RemoveAt(idx);
            }
        }

        if (match != null)
        {
            if (ev == "error")
                match.TrySetException(new DeviceException(root.TryGetProperty("msg", out var m) ? m.GetString() ?? "device error" : "device error"));
            else
                match.TrySetResult(root);
            return;
        }

        EventReceived?.Invoke(ev, root);
    }

    public async Task<JsonElement> RequestAsync(object command, string expectEv, int timeoutMs = 8000, CancellationToken ct = default)
    {
        await _requestGate.WaitAsync(ct);
        var tcs = new TaskCompletionSource<JsonElement>(TaskCreationOptions.RunContinuationsAsynchronously);
        lock (_gate) _pending.Add((expectEv, tcs));
        try
        {
            await Link.SendLineAsync(JsonSerializer.Serialize(command, JsonOptions), ct);
            using var cts = CancellationTokenSource.CreateLinkedTokenSource(ct);
            cts.CancelAfter(timeoutMs);
            await using var reg = cts.Token.Register(() => tcs.TrySetException(new TimeoutException($"No '{expectEv}' reply from the pedal.")));
            return await tcs.Task;
        }
        finally
        {
            lock (_gate) _pending.RemoveAll(p => p.Tcs == tcs);
            _requestGate.Release();
        }
    }

    public async Task<DeviceInfo> HelloAsync(CancellationToken ct = default)
        => (await RequestAsync(new { cmd = "hello" }, "hello", 4000, ct)).Deserialize<DeviceInfo>(JsonOptions)!;

    public async Task<DeviceInfo> GetInfoAsync(CancellationToken ct = default)
        => (await RequestAsync(new { cmd = "info" }, "info", 4000, ct)).Deserialize<DeviceInfo>(JsonOptions)!;

    public async Task<DeviceConfig> GetConfigAsync(CancellationToken ct = default)
    {
        var r = await RequestAsync(new { cmd = "get" }, "config", 10000, ct);
        return r.GetProperty("config").Deserialize<DeviceConfig>(JsonOptions)!;
    }

    /// <summary>Returns true when the pedal needs a reboot for the change (BLE name / passkey).</summary>
    public async Task<bool> SetConfigAsync(DeviceConfig config, CancellationToken ct = default)
    {
        var r = await RequestAsync(new { cmd = "set", config }, "ok", 15000, ct);
        return r.TryGetProperty("rebootNeeded", out var rb) && rb.ValueKind == JsonValueKind.True;
    }

    public Task<JsonElement> GetStateAsync(CancellationToken ct = default)
        => RequestAsync(new { cmd = "state" }, "state", 4000, ct);

    public Task SetPageAsync(int index, CancellationToken ct = default)
        => RequestAsync(new { cmd = "page", index }, "ok", 4000, ct);

    public Task PressAsync(int i, CancellationToken ct = default)
        => RequestAsync(new { cmd = "press", i }, "ok", 4000, ct);

    public Task ReleaseAsync(int i, CancellationToken ct = default)
        => RequestAsync(new { cmd = "release", i }, "ok", 4000, ct);

    public Task SendMidiAsync(string type, int ch, int n, int v, CancellationToken ct = default)
        => RequestAsync(new { cmd = "midi", type, ch, n, v }, "ok", 4000, ct);

    public Task FactoryResetAsync(CancellationToken ct = default)
        => RequestAsync(new { cmd = "reset" }, "ok", 8000, ct);

    public Task RebootAsync(CancellationToken ct = default)
        => RequestAsync(new { cmd = "reboot" }, "ok", 4000, ct);

    public Task UnpairAsync(CancellationToken ct = default)
        => RequestAsync(new { cmd = "unpair" }, "ok", 4000, ct);

    // ---- presets --------------------------------------------------------------------------

    public async Task<List<PresetInfo>> GetPresetsAsync(CancellationToken ct = default)
    {
        var r = await RequestAsync(new { cmd = "presets" }, "presets", 6000, ct);
        return r.GetProperty("items").Deserialize<List<PresetInfo>>(JsonOptions) ?? new List<PresetInfo>();
    }

    public Task SavePresetAsync(int slot, string name, CancellationToken ct = default)
        => RequestAsync(new { cmd = "presetSave", slot, name }, "ok", 8000, ct);

    public Task LoadPresetAsync(int slot, CancellationToken ct = default)
        => RequestAsync(new { cmd = "presetLoad", slot }, "ok", 8000, ct);

    public Task DeletePresetAsync(int slot, CancellationToken ct = default)
        => RequestAsync(new { cmd = "presetDelete", slot }, "ok", 4000, ct);

    // ---- firmware update ------------------------------------------------------------------

    /// <summary>Streams a firmware .bin to the pedal. The pedal reboots on success.</summary>
    public async Task UpdateFirmwareAsync(byte[] image, IProgress<double>? progress, CancellationToken ct = default)
    {
        var md5 = Convert.ToHexString(MD5.HashData(image)).ToLowerInvariant();
        await RequestAsync(new { cmd = "otaBegin", size = image.Length, md5 }, "ok", 15000, ct);
        const int chunk = 3072;   // 4 KB of base64 per line
        try
        {
            for (var offset = 0; offset < image.Length; offset += chunk)
            {
                var n = Math.Min(chunk, image.Length - offset);
                var d = Convert.ToBase64String(image, offset, n);
                await RequestAsync(new { cmd = "otaData", d }, "ok", 15000, ct);
                progress?.Report((offset + n) / (double)image.Length);
            }
            await RequestAsync(new { cmd = "otaEnd" }, "ok", 30000, ct);
        }
        catch
        {
            try { await RequestAsync(new { cmd = "otaAbort" }, "ok", 3000, CancellationToken.None); } catch { }
            throw;
        }
    }

    public async ValueTask DisposeAsync()
    {
        Link.LineReceived -= OnLine;
        await Link.DisposeAsync();
    }
}
