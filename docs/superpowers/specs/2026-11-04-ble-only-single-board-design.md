# BLE-Only Single-Board Test Design

## Intent

Add a third, independent ESP-IDF application for testing a BLE mouse with one
Waveshare ESP32-S3-Zero N4R2. The board connects to a BLE HID mouse, applies the
existing movement profiles when enabled, and presents itself to the PC as a USB
HID mouse through the onboard USB-C connector.

The application is a bring-up and validation path for BLE. It must not change
the matched two-board `apps/input` and `apps/output` firmware.

## Data path

The application lives at `apps/ble_only` and reuses the existing BLE HOGP
client, HID report parser, source arbiter, profile engine, output state, report
scheduler, TinyUSB descriptors, and USB HID device implementation.

The runtime path is:

```text
BLE HID mouse -> NimBLE/HOGP -> SourceArbiter -> ProfileEngine
              -> UsbMouseDevice -> TinyUSB HID -> onboard USB-C -> PC
```

No USB Host controller, SPI producer, or SPI consumer is started. The onboard
USB-C connector is USB Device only and may power the board directly from the
PC.

## Core placement and latency

NimBLE, the BLE worker, BOOT controller, and diagnostics run on CPU0. The
report/profile pipeline and TinyUSB/HID scheduler run on CPU1. The source
arbiter wake callback notifies the pipeline immediately; notifications are wake
hints and the arbiter queue remains the source of truth.

The output HID descriptor remains Full Speed with `bInterval=1`. BLE connection
interval metadata is passed through the existing cadence sanitizer so output
cadence follows the negotiated BLE interval while new physical events may wake
the HID scheduler immediately.

## Profile and BOOT behavior

There are 14 existing profiles. Power-on selects the configured default profile
and enables mods by default.

GPIO0/BOOT is sampled every 20 ms and recognizes exactly one action per press:

- Release before 700 ms: advance profile 1 -> 2 -> ... -> 14 -> 1.
- Release from 700 ms up to but not including 1.5 seconds: no action,
  preventing an almost-long press from being interpreted as a profile change.
- Hold through 1.5 seconds, then release before 5 seconds: toggle all mods.
- Hold through 5 seconds: clear the stored BLE bond immediately and restart
  scanning; releasing afterward performs no additional action.

A long press never also triggers the short-press action. Changing profile or
mod state resets profile-generated motion so the old profile cannot leak into
the next state. With mods disabled, reports pass through unchanged. State
changes and BLE connection information are reported in logs. The onboard LED
is not controlled because this board uses an addressable WS2812 and no LED
driver is currently part of the repository; avoiding a new runtime dependency
keeps this test firmware focused and reproducible.

## USB safety behavior

The application reuses the output firmware's 5 ms motion timeout, 20 ms button
release timeout, USB suspend/resume release-all behavior, motion accumulation
while the endpoint is busy, and watchdog servicing. Disconnecting the BLE mouse
queues a source boundary; the pipeline resets the profile engine and forwards a
neutral boundary so the PC cannot retain generated motion or pressed buttons.

## Configuration and feedback

The application has its own `sdkconfig.defaults`, managed-component lock, and
build directory. It enables BLE central/HOGP and esp_tinyusb together, pins
NimBLE to CPU0, pins `app_main`/the fast path to CPU1, and rejects unicore
builds.

Diagnostics log at five-second intervals: connection count, negotiated BLE
interval, selected profile, mods enabled state, active source, HID interval,
timeouts, and USB resumes. Logs are the authoritative feedback for BOOT actions.

## Verification

Host tests cover BOOT duration classification and state transitions without
FreeRTOS or GPIO. Existing parser, profile, arbiter, output-state, scheduler,
cadence, and protocol tests remain green. `apps/ble_only` must build with
ESP-IDF 6.0.3 and must not introduce an unpinned application task.

Hardware validation remains separate: enumerate on a PC, pair/reconnect a BLE
mouse, verify all 14 short-press selections, verify mod toggling, verify 5-second
bond clearing, and confirm button release on BLE disconnect and USB resume.
