# Adaptive Input Polling Design

## Intent

Make the USB HID output follow the active mouse input cadence automatically while preserving the lowest possible event latency. A 125 Hz mouse should produce 125 Hz output reports, a 500 Hz mouse should produce 500 Hz reports, and a 1 kHz mouse should keep the existing 1 kHz behavior. BLE follows its negotiated connection interval.

## Chosen behavior

- Keep the USB endpoint descriptor at `bInterval = 1 ms`. This is the upper capability advertised to the PC and cannot be changed without USB re-enumeration.
- Pace successful HID IN reports in firmware. The endpoint returns NAK on unused 1 ms frames.
- Send a new physical input event at the next available USB frame immediately, even if the nominal cadence deadline has not arrived. Reset the cadence phase from that send.
- When there is no new event, send the current zero-motion/button state at the active input cadence. This makes measured report cadence match the input while preserving button state.
- Input cadence travels explicitly through the SPI protocol. SPI keepalive packets preserve cadence but are marked as keepalive so their fixed 2 ms interval is never mistaken for mouse polling.

## Cadence source

- USB input uses a filtered estimate of consecutive valid HID report timestamps. Clamp the supported output range to 1,000–8,000 microseconds (1,000–125 Hz). Snap stable values near 1/2/4/8 ms to those standard periods to reject scheduler jitter.
- A large step change resets the estimator quickly so switching a mouse polling setting does not take seconds.
- BLE uses the negotiated connection interval (`conn_itvl × 1.25 ms`), clamped to 1–8 ms. Notification gaps are not used because BLE HID devices may notify only when state changes.
- Unknown cadence defaults to 1 ms for minimum latency until a reliable value is available.

## Protocol

- `MouseReport` gains `uint16_t poll_interval_us`, with `0` meaning unknown.
- `ReportFlags` gains `Keepalive`.
- Wire protocol becomes version 2 and 34 bytes: bytes 30–31 carry cadence, bytes 32–33 carry CRC16.
- Both boards must run the same firmware version.

## Failure behavior

- Invalid, zero, or out-of-range cadence is sanitized to 1 ms on output.
- Existing 5 ms motion timeout and 20 ms button-release failsafe remain based on SPI packet arrival, not HID pacing.
- Motion is accumulated until an HID report is actually sent; skipped USB frames must never discard movement.
- Source transitions and button changes are treated as real events and sent immediately.

## Verification

- Host tests cover estimator convergence and step changes, protocol round-trip, keepalive marking, fractional BLE scheduling, immediate event send, and no motion loss while frames are skipped.
- Both ESP-IDF applications must build after the protocol change.
- Hardware polling-rate measurement remains a bring-up test because host tests cannot observe PC USB transactions.

