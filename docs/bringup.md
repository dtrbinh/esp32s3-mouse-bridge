# Build, Flash, and Operation

## 1. Requirements

- ESP32-S3 N8R8 with 8 MB flash and 8 MB Octal PSRAM.
- A data-capable USB-C cable.
- ESP-IDF 6.0.3.
- A mouse with BLE HID mode.

The image uses a 3 MB application partition and is not intended for boards with
smaller flash devices.

## 2. Run host tests

```bash
cmake -S host-tests -B host-tests/build -DCMAKE_BUILD_TYPE=Release
cmake --build host-tests/build
ctest --test-dir host-tests/build --output-on-failure
```

## 3. Build the firmware

```bash
export IDF_TOOLS_PATH="$PWD/.toolchains/idf-tools"
source .toolchains/esp-idf/export.sh
idf.py -C apps/ble_only build
```

The output image is `apps/ble_only/build/esp32s3-mouse-bridge.bin`.

## 4. Enter download mode and flash

1. Hold BOOT.
2. Press and release RESET.
3. Release BOOT.
4. Identify the newly visible `/dev/cu.usbmodem*` port.
5. Flash through that exact port:

```bash
idf.py -C apps/ble_only -p /dev/cu.usbmodemXXXX flash
```

After reset, the same USB-C port switches to composite USB HID Mouse + Keyboard.
The native serial port may disappear; this is expected and does not indicate a
flash failure.

## 5. Connect a BLE mouse

When no valid target is available, the board creates the open
`MouseBridge-Setup-XXXX` access point:

1. Put the mouse into BLE pairing mode.
2. Join the access point from a phone or computer.
3. Wait for the captive page to open or visit `http://192.168.4.1/`.
4. Select the mouse under **Nearby devices** and press **Connect**.
5. Wait for **Connected**; the access point shuts down after about 3 seconds.

The portal displays only advertisements that include a name. The
**Previously connected** list uses these states:

- `Connected`: currently connected.
- `Ready`: saved and visible in the current scan range.
- `Offline`: saved but not currently visible.

Delete requires a second confirmation and removes both device history and its
BLE bond. If a saved target cannot reconnect, the portal reopens after the
fallback period. Hold BOOT for at least 5 seconds to open the portal manually.

## 6. Verify HID behavior

After connecting:

- Motion, left/right/middle clicks, wheel, and pan must work.
- Holding a mouse button must not be converted into a short click.
- Press and release events must not be duplicated.
- Side buttons mapped by vendor software to `Q` or `E` must forward complete
  keyboard press, hold, and release events through composite USB HID.
- With profiles enabled, holding the left button must generate continuous
  profile motion without jumping when the button is released.

## 7. BOOT and LED behavior

A short BOOT press selects one of seven profiles. Hold for 1.5–4.999 seconds
and release to toggle profiles; hold for at least 5 seconds to open the portal.
The 700–1,499 ms interval is intentionally a no-action region.

The LED blinks yellow during scanning or portal activity, blinks green with a
2-second period while connected with profiles disabled, and shows a steady
profile color when profiles are enabled. The brightness limiter enforces
`R+G+B ≤ 23`.

## 8. Realtime logs

TinyUSB HID takes ownership of native USB after boot, so `idf.py monitor` cannot
remain attached to the same port. For continuous logs, connect an external
3.3 V UART adapter to UART0. The firmware prints `MB_DIAG` every 5 seconds, but
HID bridging does not depend on UART.
