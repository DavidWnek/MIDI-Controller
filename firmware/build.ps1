# Build / flash / monitor the MidiPedal firmware with arduino-cli.
#   .\build.ps1                 compile
#   .\build.ps1 upload COM6     compile + flash (pass the pedal's current serial port)
#   .\build.ps1 monitor COM6    serial console
#
# Flashing quirk: when the pedal is running TinyUSB firmware, opening its CDC port for
# upload drops it into the ROM bootloader, which re-enumerates as the chip's JTAG/serial
# device on a different COM port (COM4 on the bench machine). esptool then fails with
# "port busy or doesn't exist". The script detects that and retries on the bootloader port.
param(
  [ValidateSet("compile", "upload", "monitor")] [string]$Action = "compile",
  [string]$Port = "COM6"
)
$ErrorActionPreference = "Continue"
$Cli = "$env:LOCALAPPDATA\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe"
if (-not (Test-Path $Cli)) { $Cli = "arduino-cli" }
$Sketch = Join-Path $PSScriptRoot "MidiPedal"
$Build  = Join-Path $PSScriptRoot "build"
$Fqbn = "esp32:esp32:esp32s3:USBMode=default,CDCOnBoot=default,PSRAM=opi,FlashSize=16M,FlashMode=qio,PartitionScheme=custom,CPUFreq=240,UploadSpeed=921600,DebugLevel=none,EraseFlash=none"

function Find-BootloaderPort {
  # The ROM bootloader shows up as "ESP32 Family Device" in arduino-cli's board list.
  $line = (& $Cli board list 2>$null) | Where-Object { $_ -match 'ESP32 Family Device' } | Select-Object -First 1
  if ($line -and $line -match '^(COM\d+)') { return $Matches[1] }
  return $null
}

switch ($Action) {
  "compile" {
    & $Cli compile --fqbn $Fqbn --build-path $Build --warnings default $Sketch
    exit $LASTEXITCODE
  }
  "upload" {
    & $Cli compile --fqbn $Fqbn --build-path $Build --warnings default $Sketch
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & $Cli upload --fqbn $Fqbn --input-dir $Build -p $Port $Sketch
    if ($LASTEXITCODE -ne 0) {
      Start-Sleep -Seconds 2
      $boot = Find-BootloaderPort
      if ($boot) {
        Write-Host "Retrying on bootloader port $boot" -ForegroundColor Yellow
        & $Cli upload --fqbn $Fqbn --input-dir $Build -p $boot $Sketch
      }
    }
    exit $LASTEXITCODE
  }
  "monitor" {
    & $Cli monitor -p $Port --config baudrate=115200
    exit $LASTEXITCODE
  }
}
