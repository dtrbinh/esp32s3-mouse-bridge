# BLE Device Captive Portal Design

## Intent

Replace blind first-match BLE pairing in the one-board `apps/ble_only`
firmware with an explicit device-selection flow. The ESP32-S3 hosts a small
Wi-Fi captive portal that lets the user choose a Freewolf X7 or another BLE
mouse from live scan results, reconnect a previously used mouse, or forget a
saved mouse. No external website, cloud service, phone app, or Web Bluetooth
connection is involved.

The portal is a setup surface, not part of the real-time data path. Wi-Fi is
fully stopped after a mouse has been selected and its HID report map has been
validated so normal BLE-to-USB HID latency is unchanged.

## User-visible behavior

### Portal lifecycle

- With no selected mouse, the board starts a soft AP named
  `MouseBridge-Setup-XXXX`, where `XXXX` is derived from the board MAC.
- With a selected mouse, the board first scans for and reconnects only that
  device. If it is not connected within 10 seconds, the setup AP and captive
  portal start while BLE scanning continues.
- Holding BOOT for 5 seconds opens the portal manually. This replaces the old
  destructive `clear_bond` action; it does not erase history or bonds.
- The AP is open and local-only for quick setup. It has no upstream routing.
  Its short lifetime and MAC-derived SSID distinguish nearby boards.
- Wildcard DNS resolves to `192.168.4.1`. Common Apple, Android, Windows, and
  Chromium captive-portal probes redirect or return the expected portal
  response so the page opens immediately after joining Wi-Fi.
- After an explicitly selected device connects and exposes a valid mouse HID
  report map, the page shows success for 3 seconds, then the HTTP server, DNS
  server, AP, and Wi-Fi driver stop completely.
- If connection or HID validation fails, Wi-Fi remains active, the page shows
  a concise error, and BLE scanning resumes.
- The existing yellow scan LED also represents portal/setup mode. Connected
  pass-through and profile colors remain unchanged.

### Single-page UI

The page is responsive, text-first, and contains no external assets or build
toolchain. It uses embedded HTML, CSS, and JavaScript and polls JSON state once
per second rather than keeping a WebSocket open.

The first section, **Previously connected**, lists up to eight saved devices.
The currently connected device is sorted first and marked with a green status
dot. Each row shows the last known name and address. A connected row offers
**Disconnect**; an inactive row offers **Connect** and **Delete**. Forgetting a
device removes its history entry and matching NimBLE bond after confirmation.

The second section, **Nearby devices**, shows live BLE observations sorted by
RSSI, strongest first. Each row shows name (or `Unknown device`), RSSI, address,
and **Connect**. Saved devices may appear in both sections but are visibly
marked as saved. Stale nearby entries disappear after 15 seconds. The scan
cache holds at most 32 devices and evicts the stalest/weakest entry when full.

While a connection attempt is active, only that row is busy and competing
connect actions are disabled. Empty, scanning, connecting, connected, failed,
and Wi-Fi-shutdown states all have explicit English messages.

## BLE selection and validation

The current requirement that an advertising packet contain service UUID
`0x1812` is removed from the connection gate. Scan observations combine data
from advertising packets and scan responses by BLE address and retain:

- address and address type;
- complete local name when available;
- RSSI and last-seen time;
- Appearance value;
- whether HID service UUID `0x1812` was advertised.

All discoverable devices can be shown, but the firmware connects only after an
explicit portal choice or when the address matches the selected saved device.
After GATT/HOGP open, the existing descriptor parser is authoritative: at
least one report map must parse as a mouse before the device is accepted,
saved, and allowed to forward reports. A non-mouse or invalid HID device is
closed and reported as a failed attempt. This makes name- or Appearance-only
advertising work without guessing from the name.

Automatic reconnect never selects an arbitrary nearby HID device. The selected
device identity is the stable peer identity resolved by NimBLE after bonding,
not a transient resolvable private address observed during a scan. Scan-time
addresses are mapped to that identity during connection and bond lookup. This
allows a bonded Freewolf X7 to reconnect after its RPA changes. A successful
explicit selection becomes the single automatic reconnect target. Up to eight
NimBLE bonds are retained so saved devices can reconnect without re-pairing.

Because the controller supports one BLE connection, switching devices is
deliberately non-transactional: choosing device B disconnects device A before
opening B. If B fails validation or connection, the portal remains open and A
remains in saved history for an explicit reconnect; firmware does not silently
fall back to A.

**Disconnect** disconnects the current peer and clears the automatic
reconnect target while retaining its saved metadata and bond. The board stays
in setup mode until the user explicitly selects a device again. This differs
from **Delete**, which also removes saved metadata and the matching NimBLE bond.

## Firmware architecture

### Portable state and storage model

A small portable model owns bounded scan records, saved-device metadata,
sorting, stale-entry eviction, and portal-visible connection status. It has no
ESP-IDF dependencies and is covered by host tests.

`BleDeviceStore` persists a versioned, checksummed record in an application NVS
namespace. It stores at most eight device identities, names, last-success
ordering, and the selected reconnect target. Corrupt or unknown versions fall
back to an empty store without blocking BLE or USB HID startup.

### BLE client changes

`BleHogpClient` continues to own NimBLE/HOGP calls on core 0. It publishes
bounded atomic/locked snapshots for the portal, accepts explicit connect,
disconnect, and forget commands through a queue, and reports attempt outcomes.
Callbacks never call HTTP functions. Address selection and scan-cache mutation
remain on core 0; the USB report path remains on core 1.

### Portal controller

`SetupPortal` owns Wi-Fi AP mode, `esp_http_server`, and a minimal UDP DNS task
on core 0 at priorities below the BLE worker. HTTP handlers only read snapshots
or enqueue commands. They never open/close BLE devices directly. API surface:

- `GET /` returns the embedded page.
- `GET /api/state` returns portal, attempt, connected, saved, and nearby state.
- `POST /api/connect` selects an exact address and address type.
- `POST /api/disconnect` disconnects the current device without forgetting it.
- `POST /api/forget` removes one saved identity and its bond.
- `POST /api/close` stops manual setup when a valid saved target still exists.

Inputs are bounded and parsed strictly. Responses disable caching. Unknown
paths redirect to `/` while the captive portal is active.

## Resource and performance constraints

- Wi-Fi and the portal are absent from `apps/input` and `apps/output`.
- All portal/DNS/BLE control work runs on CPU0; USB HID and the report/profile
  pipeline remain on CPU1.
- No dynamic work, logging, HTTP handling, or Wi-Fi call enters the CPU1 report
  fast path.
- Lists and request bodies have fixed maximum sizes. HTTP connections and DNS
  buffers are deliberately small.
- Wi-Fi stops after successful selection; normal connected operation has no
  Wi-Fi coexistence traffic.
- Portal startup failure is non-fatal to USB HID. It is logged and BLE retry
  continues, although device selection is unavailable until restart/manual
  retry.
- The firmware must continue to fit the existing 1 MiB application partition.

## Verification

Host tests cover scan/scan-response merging, RSSI sorting, stale eviction,
capacity eviction, saved-device ordering, exact-address selection, attempt
state transitions, bounded JSON-facing snapshots, and BOOT 5-second behavior.

ESP-IDF verification builds `apps/ble_only` and confirms all application tasks
are pinned to the intended core. Existing 14 host tests remain green.

Hardware validation remains explicitly pending until performed on the board:

- captive portal auto-opens on macOS/iOS, Android, and Windows where available;
- Freewolf X7 appears even if it omits UUID `0x1812` from advertising;
- selecting X7 connects, validates HID, stores history, and shuts Wi-Fi down;
- reconnect after power cycle targets X7 only;
- manual 5-second BOOT opens the portal without deleting history;
- connect/disconnect/forget actions and ordering work;
- invalid/non-mouse BLE devices are rejected cleanly;
- pointer/buttons/wheel/profile behavior and latency remain correct after Wi-Fi
  has stopped.

## Explicit non-goals

- No internet access, cloud backend, authentication account, OTA, or external
  JavaScript/CSS.
- No browser Web Bluetooth; the ESP32 performs all BLE scanning and pairing.
- No portal availability during ordinary connected operation unless the user
  explicitly holds BOOT for 5 seconds.
- No support for classic Bluetooth mice in this portal.

