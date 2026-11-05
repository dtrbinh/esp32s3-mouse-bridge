# ESP32-S3 Mouse Bridge

Single-board ESP32-S3 N8R8 firmware that receives input from a Bluetooth Low
Energy mouse and exposes it to a computer as USB HID. The board's native USB-C
port supplies power and carries USB output, so no second board, SPI wiring, or
external USB Host circuit is required.

## Features

- BLE Central/HOGP discovery, pairing, bonding, and automatic reconnection for
  generic BLE mice.
- A captive configuration portal at `192.168.4.1`; unnamed devices are hidden.
- Saved-device states shown as `Connected`, `Ready`, or `Offline`, with Connect,
  Disconnect, and Delete actions.
- Wi-Fi and the web server shut down after the BLE connection is validated.
- Composite USB HID Mouse + Keyboard output. Mouse side buttons mapped by the
  vendor software to keys such as `Q` and `E` retain press, hold, and release.
- Correct forwarding of X/Y motion, wheel, pan, and button state without
  converting held buttons into clicks or duplicating releases.
- Seven non-blocking movement profiles that emit continuous motion while the
  left mouse button is held.
- The realtime pipeline runs on CPU1; BLE, portal, and management tasks run on
  CPU0.

## Hardware

- ESP32-S3 N8R8: 8 MB flash and 8 MB Octal PSRAM.
- Onboard USB-C connected directly to the computer.
- BOOT/GPIO0 for profile and portal controls.
- Onboard WS2812 on GPIO21 by default.

GPIO19 and GPIO20 carry native USB D- and D+ internally to the USB-C connector.
No additional wiring to these pins is needed or recommended for BLE input.

## Build and test

The project uses ESP-IDF 6.0.3:

```bash
export IDF_TOOLS_PATH="$PWD/.toolchains/idf-tools"
source .toolchains/esp-idf/export.sh
idf.py -C apps/ble_only build
```

Run portable host tests without a board:

```bash
cmake -S host-tests -B host-tests/build -DCMAKE_BUILD_TYPE=Release
cmake --build host-tests/build
ctest --test-dir host-tests/build --output-on-failure
```

Flash through the serial port that appears while the board is in download mode:

```bash
idf.py -C apps/ble_only -p /dev/cu.usbmodemXXXX flash
```

After reset, the native serial port may disappear when USB re-enumerates as
`Adaptive HID Mouse Bridge`.

## BOOT controls

| Action | Result |
|---|---|
| Press for less than 700 ms | Select profile 1 through 7, then wrap to 1 |
| Hold for 700–1,499 ms | No action |
| Hold for 1.5–4.999 seconds, then release | Enable or disable movement profiles |
| Hold for at least 5 seconds | Open the captive portal without changing the profile state |

## Status LED

| State | LED |
|---|---|
| Scanning, reconnecting, or portal open | Fast yellow blink |
| Connected with profiles disabled | Slow green blink with a 2-second period |
| Connected with profiles enabled | Steady profile color |

The brightness limiter uses 30% of the previous color level, with a total
`R+G+B ≤ 23`.

## Documentation

- [Architecture](docs/architecture.md)
- [Build, flash, and operation](docs/bringup.md)
- [Hardware connections](docs/wiring.md)
- [Validation checklist](docs/validation.md)
