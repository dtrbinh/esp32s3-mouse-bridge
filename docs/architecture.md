# Firmware Architecture

## Data flow

```text
BLE advertisement / saved identity
            ↓
NimBLE HOGP client → HID report descriptor parser
            ↓
Mouse decoder ─────────────── Keyboard decoder
            ↓                         ↓
Source arbiter → profile engine       │
            ↓                         │
USB mouse scheduler            USB keyboard queue
            └────────────┬────────────┘
                         ↓
             TinyUSB composite HID → PC
```

The firmware does not assume a Freewolf X7-specific name or report map. After
connecting, it parses the device's report descriptor to locate relative mouse
and keyboard reports. Unnamed devices are omitted from the portal, but names do
not affect report decoding.

## BLE and reconnection

The portal stores the resolved stable identity instead of only the temporary
advertising address. At startup, the firmware scans for the selected target,
resolves its RPA through the NimBLE bond, and reconnects. If the target cannot
be found within the fallback period, the portal opens so another device can be
selected.

Actions have the following semantics:

- `Connect`: disconnect the current peer if necessary, connect the selected
  peer, validate its report map, and then save it.
- `Disconnect`: clear the current target while retaining history and its bond.
- `Delete`: remove the selected device's history and bond.
- `Close setup`: close the portal when a valid reconnect target exists.

After a successful connection, Wi-Fi, captive DNS, and the HTTP server stop to
reduce radio coexistence traffic and HID-path jitter.

## Dual-core pipeline

CPU0 handles NimBLE, Wi-Fi, captive DNS/HTTP, BOOT, the status LED, device
management, and diagnostics. CPU1 runs `ble_only_pipeline`, TinyUSB, and
`usb_mouse`. BLE reports wake the pipeline through task notifications; the
1 ms timeout remains only for profile ticks and safety timing.

Mouse state is processed as transitions, so press, hold, and release are not
converted into clicks or duplicated. Keyboard reports use a separate queue and
are deduplicated before USB transmission.

## Profiles and cadence

Seven profiles are available. A profile activates only while the left mouse
button is held and emits X/Y deltas on realtime ticks, producing continuous
motion instead of accumulating a jump on release. When profiles are disabled,
physical reports pass directly to USB.

The USB descriptor advertises a 1 ms endpoint interval. The scheduler may lower
the rate of state-maintenance reports to match input cadence, while new events
are still sent on the next available USB frame.

## Current limitations

This version accepts BLE input only. The separate USB Host input application,
two-board SPI bridge, and output application have been removed. Wired mice and
2.4 GHz receivers cannot connect directly to this single-board firmware.
