# Bench test for the MidiPedal firmware: drives the pedal over its USB serial port, captures its
# USB MIDI output with winmm, injects MIDI for the state-sync check, exercises presets and
# (with -Ota) streams firmware/build/MidiPedal.ino.bin through the protocol.
#   .ench-test.ps1 -Port COM6 [-Ota]
param([string]$Port = "COM6", [switch]$Ota)
$ErrorActionPreference = "Continue"

Add-Type -TypeDefinition @'
using System; using System.Runtime.InteropServices; using System.Collections.Concurrent;
public static class MidiT {
  [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)] public struct MIDIINCAPS { public ushort wMid; public ushort wPid; public uint vDriverVersion; [MarshalAs(UnmanagedType.ByValTStr, SizeConst=32)] public string szPname; public uint dwSupport; }
  [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)] public struct MIDIOUTCAPS { public ushort wMid; public ushort wPid; public uint vDriverVersion; [MarshalAs(UnmanagedType.ByValTStr, SizeConst=32)] public string szPname; public ushort wTechnology; public ushort wVoices; public ushort wNotes; public ushort wChannelMask; public uint dwSupport; }
  public delegate void MidiInProc(IntPtr hMidiIn, uint wMsg, IntPtr dwInstance, IntPtr dwParam1, IntPtr dwParam2);
  [DllImport("winmm.dll")] public static extern uint midiInGetNumDevs();
  [DllImport("winmm.dll", CharSet=CharSet.Unicode)] public static extern uint midiInGetDevCapsW(uint id, out MIDIINCAPS caps, uint size);
  [DllImport("winmm.dll")] public static extern uint midiInOpen(out IntPtr h, uint id, MidiInProc cb, IntPtr inst, uint flags);
  [DllImport("winmm.dll")] public static extern uint midiInStart(IntPtr h);
  [DllImport("winmm.dll")] public static extern uint midiInStop(IntPtr h);
  [DllImport("winmm.dll")] public static extern uint midiInClose(IntPtr h);
  [DllImport("winmm.dll")] public static extern uint midiOutGetNumDevs();
  [DllImport("winmm.dll", CharSet=CharSet.Unicode)] public static extern uint midiOutGetDevCapsW(uint id, out MIDIOUTCAPS caps, uint size);
  [DllImport("winmm.dll")] public static extern uint midiOutOpen(out IntPtr h, uint id, IntPtr cb, IntPtr inst, uint flags);
  [DllImport("winmm.dll")] public static extern uint midiOutShortMsg(IntPtr h, uint msg);
  [DllImport("winmm.dll")] public static extern uint midiOutClose(IntPtr h);
  public static ConcurrentQueue<string> Messages = new ConcurrentQueue<string>();
  static MidiInProc keep;
  public static IntPtr OpenIn(uint id) {
    keep = (hh, msg, inst, p1, p2) => { if (msg == 0x3C3) { long v = p1.ToInt64(); Messages.Enqueue(string.Format("{0:X2} {1:X2} {2:X2}", v & 0xFF, (v >> 8) & 0xFF, (v >> 16) & 0xFF)); } };
    IntPtr handle; uint r = midiInOpen(out handle, id, keep, IntPtr.Zero, 0x30000); if (r != 0) throw new Exception("midiInOpen " + r); midiInStart(handle); return handle;
  }
  public static int FindIn(string name) { uint n = midiInGetNumDevs(); for (uint i = 0; i < n; i++) { MIDIINCAPS c; midiInGetDevCapsW(i, out c, (uint)Marshal.SizeOf(typeof(MIDIINCAPS))); if (c.szPname.Contains(name)) return (int)i; } return -1; }
  public static int FindOut(string name) { uint n = midiOutGetNumDevs(); for (uint i = 0; i < n; i++) { MIDIOUTCAPS c; midiOutGetDevCapsW(i, out c, (uint)Marshal.SizeOf(typeof(MIDIOUTCAPS))); if (c.szPname.Contains(name)) return (int)i; } return -1; }
}
'@

$script:p = $null
function Open-Pedal {
  $script:p = New-Object System.IO.Ports.SerialPort $Port,115200,'None',8,'One'
  $script:p.DtrEnable = $true; $script:p.RtsEnable = $true; $script:p.ReadTimeout = 15000; $script:p.NewLine = "`n"
  $script:p.Open(); Start-Sleep -Milliseconds 300; $script:p.DiscardInBuffer()
}
function Drain([string]$label) {
  $got = @()
  while ($script:p.BytesToRead -gt 0) { try { $got += $script:p.ReadLine() } catch { break } }
  foreach ($l in $got) { $show = $l; if ($show.Length -gt 170) { $show = $show.Substring(0,170) + "..." }; Write-Host "    < $show" }
  return $got
}
function Send([string]$cmd, [int]$waitMs = 250) { $script:p.WriteLine($cmd); Start-Sleep -Milliseconds $waitMs }
function WaitFor([string]$needle, [int]$timeoutMs = 15000) {
  $deadline = [DateTime]::Now.AddMilliseconds($timeoutMs)
  while ([DateTime]::Now -lt $deadline) {
    try { $l = $script:p.ReadLine() } catch { continue }
    if ($l -match $needle) { return $l }
    if ($l -match '"ev":"error"') { throw "device error: $l" }
  }
  throw "timeout waiting for $needle"
}
function Midi([string]$label) {
  $m = $null; $got = @()
  while ([MidiT]::Messages.TryDequeue([ref]$m)) { $got += $m }
  Write-Output ("    MIDI[$label]: " + $(if ($got.Count) { $got -join ' | ' } else { '(none)' }))
}
function Step([string]$title) { Write-Output ""; Write-Output "== $title" }

$inId = [MidiT]::FindIn("MidiPedal"); $outId = [MidiT]::FindOut("MidiPedal")
if ($inId -lt 0) { Write-Output "no MidiPedal MIDI input"; exit 1 }
$hin = [MidiT]::OpenIn([uint32]$inId)
$hout = [IntPtr]::Zero; [void][MidiT]::midiOutOpen([ref]$hout, [uint32]$outId, [IntPtr]::Zero, [IntPtr]::Zero, 0)
Open-Pedal

Step "info"
Send '{"cmd":"info"}'; [void](Drain)

Step "set v2 test config"
$cfg = @'
{"cmd":"set","config":{"v":2,"name":"MidiPedal","ch":1,"startPage":0,"brightness":40,"blePin":0,"thru":{"usb":2,"din":1,"net":0},"pages":[{"name":"Test","sw":[
{"name":"Latch+PC","mode":"latch","type":"cc","ch":0,"num":20,"on":127,"off":0,"extra":[{"t":"pc","ch":0,"n":5,"v":0}]},
{"name":"Amp A","mode":"radio","group":1,"type":"cc","ch":0,"num":30,"on":127,"off":0},
{"name":"Amp B","mode":"radio","group":1,"type":"cc","ch":0,"num":31,"on":127,"off":0},
{"name":"Bank+","mode":"bank","ch":2,"bank":{"step":1,"min":0,"max":3,"wrap":true}},
{"name":"Taps","mode":"momentary","type":"cc","ch":0,"num":40,"on":127,"off":0,"tap2":[{"t":"cc","ch":0,"n":41,"v":127}],"hold":[{"t":"cc","ch":0,"n":42,"v":127}]},
{"name":"DIN only","mode":"momentary","type":"cc","ch":0,"num":50,"on":127,"off":0,"route":2},
{"name":"PC ch3","mode":"momentary","type":"pc","ch":3,"num":10},
{"name":"Next","mode":"page","page":-2},
{"name":"Preset1","mode":"momentary","type":"cc","num":60,"hold":[{"t":"preset","n":0,"v":0}]}
]}]}}
'@ -replace "`r?`n", ""
Send $cfg 600; [void](Drain)
Midi "after set (should be none)"

Step "latch + macro: press SW1 twice"
Send '{"cmd":"press","i":0}' 150; Send '{"cmd":"release","i":0}' 250; [void](Drain); Midi "1st press -> CC20=127, PC5"
Send '{"cmd":"press","i":0}' 150; Send '{"cmd":"release","i":0}' 250; [void](Drain); Midi "2nd press -> CC20=0, PC5"

Step "radio group: SW2 then SW3 then SW3 again"
Send '{"cmd":"press","i":1}' 100; Send '{"cmd":"release","i":1}' 200; [void](Drain); Midi "SW2 -> CC30=127"
Send '{"cmd":"press","i":2}' 100; Send '{"cmd":"release","i":2}' 200; [void](Drain); Midi "SW3 -> CC30=0, CC31=127"
Send '{"cmd":"press","i":2}' 100; Send '{"cmd":"release","i":2}' 200; [void](Drain); Midi "SW3 again -> CC31=127 only (stays selected)"

Step "bank: press SW4 five times (ch2, 0..3 wrap)"
for ($k = 0; $k -lt 5; $k++) { Send '{"cmd":"press","i":3}' 60; Send '{"cmd":"release","i":3}' 120 }
[void](Drain); Midi "expect C1 00, C1 01, C1 02, C1 03, C1 00"

Step "single tap on a double-tap switch (SW5): deferred ~320 ms"
Send '{"cmd":"press","i":4}' 40; Send '{"cmd":"release","i":4}' 700; [void](Drain); Midi "expect CC40=127, CC40=0 (after deferral)"

Step "double tap SW5"
Send '{"cmd":"press","i":4}' 40; Send '{"cmd":"release","i":4}' 60; Send '{"cmd":"press","i":4}' 40; Send '{"cmd":"release","i":4}' 700; [void](Drain); Midi "expect CC41=127 only"

Step "hold SW5 for 1 s"
Send '{"cmd":"press","i":4}' 1000; Send '{"cmd":"release","i":4}' 300; [void](Drain); Midi "expect CC40=127, CC42=127 (hold), CC40=0"

Step "routing: SW6 is DIN-only, SW7 is PC 10 on ch3"
Send '{"cmd":"press","i":5}' 80; Send '{"cmd":"release","i":5}' 200; [void](Drain); Midi "SW6 -> nothing on USB"
Send '{"cmd":"press","i":6}' 80; Send '{"cmd":"release","i":6}' 200; [void](Drain); Midi "SW7 -> C2 0A"

Step "MIDI-in state sync: host sends CC20=127 / CC20=0 on ch1, PC2 on ch2"
[void][MidiT]::midiOutShortMsg($hout, 0x7F14B0); Start-Sleep -Milliseconds 300; [void](Drain)
[void][MidiT]::midiOutShortMsg($hout, 0x0014B0); Start-Sleep -Milliseconds 300; [void](Drain)
[void][MidiT]::midiOutShortMsg($hout, 0x0002C1); Start-Sleep -Milliseconds 300; [void](Drain)
Send '{"cmd":"press","i":3}' 80; Send '{"cmd":"release","i":3}' 200; [void](Drain); Midi "bank after PC2 came in -> C1 03"
Midi "(thru usb->din only, so nothing echoed to USB)"

Step "state snapshot"
Send '{"cmd":"state"}' 300; [void](Drain)

Step "presets: save slot 0, factory reset, list, load slot 0, verify, delete"
Send '{"cmd":"presetSave","slot":0,"name":"Bench"}' 500; [void](Drain)
Send '{"cmd":"reset"}' 800; [void](Drain)
Send '{"cmd":"get"}' 500; $g = Drain; Write-Output ("    after reset first switch name: " + $(if (($g -join '') -match '"name":"(SW1)"') { $Matches[1] } else { '?' }))
Send '{"cmd":"presets"}' 400; [void](Drain)
Send '{"cmd":"presetLoad","slot":0}' 800; [void](Drain)
Send '{"cmd":"get"}' 500; $g = Drain; Write-Output ("    after load, first switch name: " + $(if (($g -join '') -match '"name":"(Latch\+PC)"') { $Matches[1] } else { '?' }))
Send '{"cmd":"presetDelete","slot":0}' 300; [void](Drain)
Send '{"cmd":"presets"}' 300; [void](Drain)

Step "preset action from a switch: hold SW9 (slot 0 is now empty -> expect log, no crash)"
Send '{"cmd":"press","i":8}' 1000; Send '{"cmd":"release","i":8}' 300; [void](Drain)

Step "restore factory defaults"
Send '{"cmd":"reset"}' 800; [void](Drain)

if ($Ota) {
  Step "OTA over the protocol"
  $bin = [IO.File]::ReadAllBytes((Join-Path $PSScriptRoot "..\build\MidiPedal.ino.bin"))
  $md5 = ([System.Security.Cryptography.MD5]::Create().ComputeHash($bin) | ForEach-Object { $_.ToString("x2") }) -join ''
  Write-Output "    image $($bin.Length) bytes md5 $md5"
  $script:p.WriteLine(('{"cmd":"otaBegin","size":' + $bin.Length + ',"md5":"' + $md5 + '"}')); [void](WaitFor '"cmd":"otaBegin"')
  $sw = [Diagnostics.Stopwatch]::StartNew(); $chunk = 3072; $off = 0
  while ($off -lt $bin.Length) {
    $n = [Math]::Min($chunk, $bin.Length - $off)
    $d = [Convert]::ToBase64String($bin, $off, $n)
    $script:p.WriteLine('{"cmd":"otaData","d":"' + $d + '"}')
    [void](WaitFor '"cmd":"otaData"')
    $off += $n
  }
  $script:p.WriteLine('{"cmd":"otaEnd"}'); $r = WaitFor '"cmd":"otaEnd"'
  Write-Output ("    otaEnd: $r  (" + [int]$sw.Elapsed.TotalSeconds + " s, " + [int]($bin.Length / 1024 / $sw.Elapsed.TotalSeconds) + " KB/s)")
  $script:p.Close(); Write-Output "    waiting for reboot..."; Start-Sleep -Seconds 8
  Open-Pedal; Send '{"cmd":"info"}' 400; [void](Drain)
}

$script:p.Close(); [void][MidiT]::midiInStop($hin); [void][MidiT]::midiInClose($hin); [void][MidiT]::midiOutClose($hout)
Write-Output ""; Write-Output "done"
