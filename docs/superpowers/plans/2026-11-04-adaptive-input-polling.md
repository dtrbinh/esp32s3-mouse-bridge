# Adaptive Input Polling Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make output HID report cadence automatically follow the active USB or BLE mouse input from 125 Hz through 1 kHz without delaying new events.

**Architecture:** Measure/supply cadence on the input board, carry it in wire protocol v2, then use a host-testable output scheduler to decide which 1 ms USB frames produce reports. Keep `bInterval=1` and use NAK for unused frames so runtime changes need no reconnect.

**Tech Stack:** C++20 host components, ESP-IDF 6.0.3, TinyUSB, CMake/CTest.

**Spec:** `docs/superpowers/specs/2026-11-04-adaptive-input-polling-design.md`

## Global Constraints

- USB descriptor remains Full Speed HID with endpoint `0x81`, 8-byte packets, and `bInterval=1`.
- Output cadence is clamped to 1,000–8,000 microseconds.
- Real input events are sent on the next available 1 ms USB frame.
- SPI keepalive remains 2 ms and must not influence input cadence.
- Both boards use wire protocol version 2 and 34-byte packets.

## Review Focus

- No cadence measurement yet: output defaults to 1 kHz and does not stall.
- Abrupt 125 Hz ↔ 1 kHz change: estimator and output converge quickly.
- BLE 7.5 ms interval: scheduler alternates frame spacing without long-term drift.
- Several input reports before one USB send: motion and wheel deltas are accumulated, not lost.
- SPI loss while HID is paced below 1 kHz: existing 5/20 ms failsafes still fire.

---

### Task 1: Cadence model and wire protocol v2

**Files:**
- Create: `components/mouse_bridge/include/mouse_bridge/cadence.hpp`
- Create: `components/mouse_bridge/src/cadence.cpp`
- Modify: `components/mouse_bridge/include/mouse_bridge/mouse_report.hpp`
- Modify: `components/mouse_bridge/include/mouse_bridge/wire_protocol.hpp`
- Modify: `components/mouse_bridge/src/wire_protocol.cpp`
- Modify: `components/mouse_bridge/CMakeLists.txt`
- Modify: `components/spi_link/src/keepalive.cpp`
- Test: `host-tests/cadence_test.cpp`
- Test: `host-tests/wire_protocol_test.cpp`
- Test: `host-tests/spi_keepalive_test.cpp`

**Interfaces:**
- Produces: `CadenceEstimator::observe(uint64_t timestamp_us) -> uint16_t`; `sanitize_poll_interval_us(uint32_t) -> uint16_t`; `MouseReport::poll_interval_us`; `ReportFlags::Keepalive`.
- Produces: wire protocol version 2, 34-byte packet with cadence at bytes 30–31 and CRC at 32–33.

- [ ] Write failing tests for standard-rate convergence, abrupt step reset, protocol cadence round-trip, and keepalive flag/cadence preservation.
- [ ] Run focused tests and verify failure against the version-1 model.
- [ ] Implement cadence estimator, report fields, protocol v2, and keepalive marking.
- [ ] Run all host tests; expect all pass.
- [ ] Commit as `feat: carry input cadence over spi`.

### Task 2: Annotate USB and BLE input cadence

**Files:**
- Modify: `apps/input/main/usb_mouse_host.hpp`
- Modify: `apps/input/main/usb_mouse_host.cpp`
- Modify: `apps/input/main/ble_hogp_client.cpp`
- Modify: `apps/input/main/source_arbiter.cpp`

**Interfaces:**
- Consumes: `CadenceEstimator`, `MouseReport::poll_interval_us`, and BLE `conn_itvl` units.
- Produces: every real USB/BLE report and boundary report carries the best current cadence.

- [ ] Add source-arbiter host assertions that cadence survives tagging and boundary transitions.
- [ ] Run focused test and verify failure.
- [ ] Feed USB timestamps through `CadenceEstimator`; set BLE cadence from negotiated interval and preserve last source cadence on boundaries.
- [ ] Run all host tests and build input firmware; expect success.
- [ ] Commit as `feat: measure active mouse polling cadence`.

### Task 3: Adaptive output scheduler

**Files:**
- Create: `apps/output/main/report_scheduler.hpp`
- Create: `apps/output/main/report_scheduler.cpp`
- Modify: `apps/output/main/CMakeLists.txt`
- Modify: `apps/output/main/output_state.hpp`
- Modify: `apps/output/main/output_state.cpp`
- Modify: `apps/output/main/usb_mouse_device.hpp`
- Modify: `apps/output/main/usb_mouse_device.cpp`
- Modify: `apps/output/main/app_main.cpp`
- Test: `host-tests/report_scheduler_test.cpp`
- Modify: `host-tests/CMakeLists.txt`
- Modify: `docs/bringup.md`
- Modify: `docs/validation.md`

**Interfaces:**
- Consumes: cadence and `Keepalive` from decoded SPI reports.
- Produces: `ReportScheduler::on_packet(...)`, `ReportScheduler::due(...)`, and adaptive HID report pacing while the USB task continues checking every 1 ms.

- [ ] Write failing tests for 1/2/4/8 ms pacing, 7.5 ms fractional phase, immediate event send, unknown cadence fallback, and accumulated motion across skipped frames.
- [ ] Run focused tests and verify failure.
- [ ] Implement scheduler and call `OutputState::tick()` only when a HID report will actually be sent; keep timeout evaluation available on every 1 ms task iteration.
- [ ] Update diagnostics/docs to report active output interval and hardware validation steps.
- [ ] Run all host tests, build both firmwares, and run `git diff --check`; expect success.
- [ ] Commit as `feat: adapt usb hid reports to input polling`.

