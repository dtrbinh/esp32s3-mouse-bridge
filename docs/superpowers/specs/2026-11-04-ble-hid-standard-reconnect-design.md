# BLE HID Standard Reconnect Design

## Intent

Make the single-board BLE-to-USB bridge reconnect reliably with general BLE
HID mice. Freewolf X7 remains a test device, not a product-specific target.

## Required behavior

- Prefer standard BLE Security Manager pairing and bonding with NoInputNoOutput
  / Just Works behavior.
- Persist NimBLE LTK and IRK material in NVS across resets.
- Resolve rotating RPA advertisements to a bonded identity and select the
  current over-the-air address for the connection.
- Treat device names as display metadata. Name matching is allowed only as a
  compatibility fallback for peers that do not produce a usable bond.
- After every link, complete HID service discovery, report-map parsing, CCCD
  subscription, and low-latency interval negotiation before forwarding input.
- On disconnect or failed connect, return to continuous scan and retain the
  selected saved device for retry.
- Forgetting a device removes both application history and NimBLE security
  material.
- A peer that cannot complete security may continue through unencrypted direct
  GATT when its HID service and mouse report descriptor validate successfully.
- Log security start/result, resolved identity, bond deletion, and fallback so
  hardware behavior is diagnosable from the serial monitor.

## Non-goals

- No GATT service cache in this change.
- No strict authenticated/MITM requirement.
- No X7-only name, address, report layout, or pairing behavior.
- No captive-portal redesign.

## Compatibility policy

Security is attempted first on each newly established link. Successful
encryption proceeds through normal HID discovery. A security error that leaves
the link alive falls back to direct GATT discovery; normal GATT validation is
still mandatory. If the peer disconnects during security, the existing scan and
retry loop handles the next advertising window.

