# MidiPedal control protocol (v2)

Transport-agnostic, newline-delimited JSON. The same bytes flow over:

* **BLE** — service `7a1c0001-5e4d-4c1b-9a2f-0d6e8b3f1a10`
  * RX `7a1c0002-…` (write / write-without-response): app -> device
  * TX `7a1c0003-…` (notify): device -> app
  * Messages longer than MTU-3 are split across writes/notifies; the receiver
    concatenates bytes and splits on `\n`. The device requests MTU 517.
  * When the device has a passkey (`blePin` != 0) both characteristics require an
    encrypted, authenticated, bonded link. The phone's OS shows the pairing dialog on the
    first write; the user types the 6-digit code.
* **USB CDC serial** — 115200 baud (rate is ignored on USB). Assert DTR (and RTS) when
  opening: the pedal drops output while no host holds DTR. Lines starting with `# `
  are human log output and must be ignored by clients. Only lines starting with `{`
  are protocol frames. A firmware update over USB runs at roughly 60 KB/s.

Every request carries `cmd`; every frame from the device carries `ev`.

## Requests and their responses

| Request | Response `ev` | Notes |
|---|---|---|
| `{"cmd":"hello"}` | `hello` | Same payload as `info`. Use it as a ping. |
| `{"cmd":"info"}` | `info` | `fw, proto, name, usb, eth, ip, sessions, ble, bonds, secured, page, pages, uptime, heap` |
| `{"cmd":"get"}` | `config` | `{"ev":"config","config":{…}}` |
| `{"cmd":"set","config":{…}}` | `ok` (with `rebootNeeded`) or `error` | Validates, applies live, saves to flash. Other links get `configChanged`. Name and passkey changes need a reboot. |
| `{"cmd":"state"}` | `state` | `page`, `latched:[{page,i}]`, `banks:[{ch,value}]` — snapshot for a freshly connected app |
| `{"cmd":"page","index":n}` | `ok` | `n` = page index, `-1` previous, `-2` next |
| `{"cmd":"press","i":n}` / `{"cmd":"release","i":n}` | `ok` | Simulate footswitch `n` (0-8). |
| `{"cmd":"midi","type":"cc|noteon|noteoff|pc","ch":1,"n":64,"v":127,"route":7}` | `ok` | Send an arbitrary message; `route` is a port bitmask (default all) |
| `{"cmd":"presets"}` | `presets` | `items:[{slot,name,size}]` for occupied slots (0-7) |
| `{"cmd":"presetSave","slot":n,"name":"…"}` | `ok` | Stores the current config in slot `n` |
| `{"cmd":"presetLoad","slot":n}` | `ok` | Loads slot `n` as the current config (saved, applied); others get `configChanged` |
| `{"cmd":"presetDelete","slot":n}` | `ok` | |
| `{"cmd":"unpair"}` | `ok` | Forget all BLE bonds |
| `{"cmd":"otaBegin","size":N,"md5":"hex"}` | `ok` | Start a firmware update of N bytes (MD5 optional but recommended) |
| `{"cmd":"otaData","d":"base64"}` | `ok` (with `got`) | Up to 4 KB of base64 per line; wait for `ok` before the next chunk |
| `{"cmd":"otaEnd"}` | `ok` | Verifies the image, then reboots into it |
| `{"cmd":"otaAbort"}` | `ok` | |
| `{"cmd":"reset"}` | `ok` | Factory defaults, saved |
| `{"cmd":"reboot"}` | `ok` | Then restarts |

Errors: `{"ev":"error","msg":"…"}`.

Ethernet also exposes ArduinoOTA (`_arduino._tcp`, port 3232) for `arduino-cli upload -p <ip>`.
The OTA password is the BLE passkey when one is set.

## Unsolicited events

| `ev` | Payload | When |
|---|---|---|
| `switch` | `i`, `down` | Physical or simulated switch edge |
| `latch` | `page`, `i`, `on` | Latch or radio switch state changed (by a press or by incoming MIDI) |
| `page` | `index` | Active page changed |
| `bank` | `ch`, `value` | A bank switch stepped, or an incoming program change moved the counter |
| `long` | `i` | Long press (800 ms) |
| `tap2` | `i` | Double tap recognised |
| `link` | `eth`, `ip` | Ethernet came up / went down |
| `configChanged` | `from` (`usb`, `ble`, `preset`), `slot` | The config was rewritten by another link or a preset action |

## Port bitmask

`1` = USB, `2` = DIN/TRS, `4` = Network (RTP-MIDI). `7` = everything.

## Config schema (v2)

v1 documents still load: new fields take their defaults.

```json
{
  "v": 2,
  "name": "MidiPedal",          // USB product, BLE and mDNS name (max 24 chars)
  "ch": 1,                      // global MIDI channel 1-16
  "startPage": 0,
  "brightness": 40,             // status LED 0-255
  "blePin": 0,                  // 6-digit BLE passkey, 0 = no pairing required
  "thru": { "usb": 2, "din": 1, "net": 0 },   // per input port: bitmask of outputs it is forwarded to
  "pages": [                    // 1-8 pages
    {
      "name": "Page 1",         // max 16 chars
      "sw": [                   // exactly 9 entries, row-major from top-left
        {
          "name": "SW1",        // max 16 chars
          "mode": "momentary",  // momentary | latch | radio | bank | page
          "type": "cc",         // primary message: cc | note | pc
          "ch": 0,              // 0 = global channel, else 1-16
          "num": 20,            // CC number / note number / program
          "on": 127,            // value sent on press (CC) / velocity (note)
          "off": 0,             // value sent on release, un-latch or radio deselect (CC)
          "page": -2,           // page mode target: index, -1 previous, -2 next
          "color": "#FF4040",
          "route": 7,           // which ports this switch's messages go to
          "group": 0,           // radio group 1-8 (radio mode)
          "bank": { "step": 1, "min": 0, "max": 127, "wrap": true },   // bank mode
          "extra": [ { "t": "cc", "ch": 0, "n": 80, "v": 127 } ],      // macro, sent after the primary action on press
          "hold":  [ { "t": "pc", "ch": 0, "n": 5,  "v": 0 } ],        // long press
          "tap2":  [ { "t": "page", "ch": 0, "n": -2, "v": 0 } ]       // double tap
        }
      ]
    }
  ]
}
```

Action entries (`extra`, `hold`, `tap2`, up to 6 each): `t` is `cc`, `noteon`, `noteoff`,
`pc`, `page` (`n` = page target) or `preset` (`n` = slot 0-7). `ch` 0 = global.

### Behaviour

| mode | press | release | notes |
|---|---|---|---|
| momentary | primary "on" form + `extra` | primary "off" form | |
| latch | toggles; sends the on or off form + `extra` | nothing | state follows incoming CC/PC that matches ch+num+value |
| radio | sends on form, sends off form for the previously active switch of the same `group` on this page + `extra` | nothing | one active switch per group per page; incoming MIDI mirrors the selection |
| bank | steps the per-channel PC counter by `bank.step`, clamps or wraps, sends PC + `extra` | nothing | counter starts at `min`, follows incoming PC on that channel |
| page | changes page + `extra` | nothing | |

* **Long press** (800 ms): `hold` fires once while held, in addition to the press action.
* **Double tap** (second press within 320 ms): `tap2` fires instead of the press action.
  When a switch has `tap2` entries, single presses are deferred until the window expires
  (or the switch is released) so the two can be told apart; switches without `tap2` keep
  zero-latency presses.
* **Thru/merge**: incoming channel messages on a port are forwarded to the ports in that
  port's `thru` mask (never back to the source). Real-time and SysEx are not forwarded yet.
* **State sync** happens for every incoming CC/PC regardless of thru settings.
