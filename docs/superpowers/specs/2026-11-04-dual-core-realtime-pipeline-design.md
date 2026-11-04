# Dual-Core Real-Time Pipeline Design

## Intent

Use both ESP32-S3 cores to reduce internal mouse-report latency and jitter without changing the adaptive 125-1000 Hz behavior. The optimization targets scheduling delay inside the two boards; it cannot remove the USB Full Speed 1 ms frame boundary.

## Chosen architecture

The input board reserves CPU0 for the Bluetooth stack and maintenance work, and CPU1 for the wired-mouse fast path. NimBLE remains pinned to CPU0 through `CONFIG_BT_NIMBLE_PINNED_TO_CORE_0`. `app_main` runs on CPU1 long enough to install USB Host on that core, then a dedicated source/profile pipeline task and SPI transmission remain on CPU1. Diagnostics and the bond-button task run on CPU0.

The output board keeps SPI receive/decode and diagnostics on CPU0. The esp_tinyusb device task already defaults to CPU1 on a multicore ESP32-S3; the HID state/scheduler task is explicitly pinned to CPU1. SPI delivers packets across cores through the existing short critical section, then notifies the HID task immediately.

## Event-driven input path

`SourceArbiter` gains an optional wake callback. It invokes the callback after a report or boundary is committed and after releasing its lock. On ESP32 the callback uses a direct task notification to wake the CPU1 pipeline task. The pipeline drains all queued reports, applies the profile engine, and wakes the higher-priority SPI producer.

The pipeline retains a 1 ms maximum notification timeout only to evaluate the existing 50 ms USB-to-BLE fallback and reset the watchdog. Real reports no longer wait for the periodic 1 ms loop.

## Event-assisted output path

`UsbMouseDevice::on_packet()` continues to update `OutputState` and `ReportScheduler` under the existing short cross-core critical section, then notifies the CPU1 HID task. The HID task also wakes at least once per millisecond for cadence deadlines, timeout releases, and bypass-button debounce. Packet notifications may wake it earlier, allowing a real event to be staged as soon as TinyUSB reports the HID endpoint ready.

The USB descriptor remains `bInterval=1`. Adaptive 1/2/4/8 ms scheduling, 7.5 ms BLE phase behavior, NAK on unused frames, motion accumulation, and immediate safety releases remain unchanged.

## Priorities and affinity

Input CPU1 priorities increase along the downstream path so a newly decoded report can run to SPI without waiting for a lower-priority polling loop: USB daemon 4, HID driver 5, USB service 6, pipeline 7, SPI producer 8. Input CPU0 uses the ESP-IDF NimBLE defaults, BLE worker 3, bond button 2, and diagnostics 1.

Output uses SPI consumer priority 8 on CPU0 and HID scheduler priority 8 on CPU1. TinyUSB keeps its managed-component default of priority 5 on CPU1. Diagnostics stays at priority 1 on CPU0. All application tasks use static stacks in internal SRAM; SPI buffers remain DMA-capable internal memory.

## Safety and failure behavior

- Task affinity is explicit; startup diagnostics report the running core so an unexpected SDK configuration is visible.
- Wake callbacks never run while `SourceArbiter` holds its lock.
- Notifications are hints, not data storage. Queues/mailboxes remain the source of truth, so coalesced notifications cannot lose movement.
- SPI and HID tasks continue to block when idle; no busy loop is introduced.
- USB suspend/resume, SPI 5/20 ms failsafes, BLE fallback, source boundaries, and watchdog servicing retain their current behavior.
- If a notification arrives while the HID endpoint is busy, state remains accumulated until the next endpoint-ready check.

## Performance observability

Diagnostics retain the input report-to-SPI maximum latency and add output packet-to-HID-stage maximum latency, alongside task startup core logs. The goal is internal report processing below 250 us per board under normal operation, but this is a hardware-validation target rather than a host-test guarantee.

## Verification

- Host tests prove wake callbacks fire for deliverable reports and boundaries, do not fire for suppressed BLE reports, and preserve queue semantics when notifications coalesce.
- Existing scheduler, timeout, cadence, mailbox, protocol, and source-arbitration tests remain green.
- Both ESP-IDF applications build with two cores enabled and explicit affinities.
- Hardware validation checks logged core placement, 125/250/500/1000 Hz behavior, BLE operation, SPI overruns, and latency/jitter with diagnostics enabled and disabled.
