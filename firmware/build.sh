#!/usr/bin/env bash
# Same as build.ps1 for bash users:  ./build.sh [compile|upload|monitor] [PORT]
# See build.ps1 for the bootloader-port retry explanation.
set -u
ACTION="${1:-compile}"; PORT="${2:-COM6}"
HERE="$(cd "$(dirname "$0")" && pwd)"
CLI="$LOCALAPPDATA/Programs/Arduino IDE/resources/app/lib/backend/resources/arduino-cli.exe"
[ -x "$CLI" ] || CLI=arduino-cli
FQBN="esp32:esp32:esp32s3:USBMode=default,CDCOnBoot=default,PSRAM=opi,FlashSize=16M,FlashMode=qio,PartitionScheme=custom,CPUFreq=240,UploadSpeed=921600,DebugLevel=none,EraseFlash=none"

bootloader_port() {
  "$CLI" board list 2>/dev/null | awk '/ESP32 Family Device/ {print $1; exit}'
}

case "$ACTION" in
  compile)
    "$CLI" compile --fqbn "$FQBN" --build-path "$HERE/build" --warnings default "$HERE/MidiPedal" ;;
  upload)
    "$CLI" compile --fqbn "$FQBN" --build-path "$HERE/build" --warnings default "$HERE/MidiPedal" || exit $?
    if ! "$CLI" upload --fqbn "$FQBN" --input-dir "$HERE/build" -p "$PORT" "$HERE/MidiPedal"; then
      sleep 2
      BOOT="$(bootloader_port)"
      if [ -n "$BOOT" ]; then
        echo "Retrying on bootloader port $BOOT"
        "$CLI" upload --fqbn "$FQBN" --input-dir "$HERE/build" -p "$BOOT" "$HERE/MidiPedal"
      else
        exit 1
      fi
    fi ;;
  monitor)
    "$CLI" monitor -p "$PORT" --config baudrate=115200 ;;
esac
