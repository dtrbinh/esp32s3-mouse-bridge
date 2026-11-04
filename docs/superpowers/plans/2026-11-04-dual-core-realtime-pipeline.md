# Dual-Core Real-Time Pipeline Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Use both ESP32-S3 cores and event-driven wakeups to reduce internal mouse-report latency and jitter while preserving adaptive 125-1000 Hz HID behavior.

**Architecture:** Pin Bluetooth and maintenance work to CPU0 and the input USB-to-SPI fast path to CPU1; on output, pin SPI receive to CPU0 and TinyUSB/HID scheduling to CPU1. Replace the input report-path polling delay with direct notifications and add packet-assisted output wakeups while retaining a 1 ms maximum maintenance deadline.

**Tech Stack:** C++20, ESP-IDF 6.0.3, FreeRTOS SMP static tasks, USB Host HID, NimBLE, SPI DMA, esp_tinyusb/TinyUSB, CMake/CTest.

**Spec:** `docs/superpowers/specs/2026-11-04-dual-core-realtime-pipeline-design.md`

## Global Constraints

- ESP32-S3 remains a dual-core target; `CONFIG_FREERTOS_UNICORE` must stay disabled.
- NimBLE stays on CPU0; input USB Host, source/profile processing, and SPI transmit run on CPU1.
- Output SPI receive runs on CPU0; esp_tinyusb and HID scheduling run on CPU1.
- USB HID endpoint remains Full Speed, `bInterval=1`, with adaptive 1/2/4/8 ms successful-report pacing.
- Direct task notifications are wake hints only; queues and accumulated state remain the source of truth.
- No busy loop, dynamic task allocation, PSRAM-backed real-time buffer, or protocol-v3 change is introduced.
- Existing 5 ms motion timeout, 20 ms release timeout, 50 ms BLE fallback, watchdog, suspend/resume, and source-priority behavior remain unchanged.

## Review Focus

- Several reports arrive before the notified task runs: every queued/merged movement delta is retained despite notification coalescing.
- BLE report is suppressed while USB owns the source: no false pipeline wake or source change occurs.
- USB disconnect starts the 50 ms BLE fallback without an additional report: periodic maintenance still activates the pending BLE report.
- Output packet arrives while TinyUSB endpoint is busy: motion remains accumulated and is sent when the endpoint becomes ready.
- A core is saturated by diagnostics or BLE scanning: watchdog servicing, timeout releases, and USB suspend/resume still run on their assigned core.

---

### Task 1: Source-arbiter wake contract

**Files:**
- Modify: `apps/input/main/source_arbiter.hpp`
- Modify: `apps/input/main/source_arbiter.cpp`
- Test: `host-tests/source_arbiter_test.cpp`

**Interfaces:**
- Produces: `SourceArbiter::set_wake_callback(void (*callback)(void*), void* context) -> void`.
- Contract: invoke once after each deliverable report or boundary is queued, outside the arbiter lock; do not invoke for a BLE report held pending behind active USB.

- [ ] **Step 1: Write failing wake-contract tests**

  Add assertions named `wake_callback_fires_for_deliverable_reports_and_boundaries`, `suppressed_ble_does_not_wake_pipeline`, and `coalesced_wakes_do_not_change_queue_contents`. Count callback invocations and drain `poll()` to verify the callback never substitutes for stored data.

- [ ] **Step 2: Run the focused test and verify it fails**

  Run: `cmake --build host-tests/build --target source_arbiter_test && ./host-tests/build/source_arbiter_test`

  Expected: FAIL to compile because `set_wake_callback` does not exist.

- [ ] **Step 3: Implement the wake callback**

  Add `WakeCallback`, callback/context fields, and `notify_wake()` to `SourceArbiter`. Record whether `on_report()` or `on_disconnected()` actually queued data, leave the critical section, and then notify. Do not notify for connection bookkeeping or pending BLE data that was not queued.

- [ ] **Step 4: Run host tests**

  Run: `cmake --build host-tests/build && ctest --test-dir host-tests/build --output-on-failure`

  Expected: all tests pass.

- [ ] **Step 5: Commit**

  ```bash
  git add apps/input/main/source_arbiter.hpp apps/input/main/source_arbiter.cpp host-tests/source_arbiter_test.cpp
  git commit -m "feat: wake input pipeline on source events"
  ```

### Task 2: Input-board affinity and event-driven pipeline

**Files:**
- Modify: `apps/input/sdkconfig.defaults`
- Modify: `apps/input/main/app_main.cpp`
- Modify: `apps/input/main/usb_mouse_host.cpp`
- Modify: `apps/input/main/spi_packet_producer.cpp`
- Modify: `apps/input/main/ble_hogp_client.cpp`
- Modify: `apps/input/main/bond_control.cpp`
- Modify: `docs/bringup.md`

**Interfaces:**
- Consumes: `SourceArbiter::set_wake_callback(...)` from Task 1.
- Produces: CPU1 event path with priorities USB daemon 4, HID driver 5, USB service 6, pipeline 7, SPI producer 8; CPU0 BLE worker 3, bond button 2, diagnostics 1.

- [ ] **Step 1: Add a build-visible dual-core guard and affinity defaults**

  Set `CONFIG_ESP_MAIN_TASK_AFFINITY_CPU1=y` and `CONFIG_BT_NIMBLE_PINNED_TO_CORE_0=y` in input `sdkconfig.defaults`. Add a compile-time guard in `app_main.cpp` rejecting `CONFIG_FREERTOS_UNICORE`; `app_main` must run on CPU1 so `usb_host_install()` binds its interrupt there.

- [ ] **Step 2: Pin every application-created input task**

  Replace task creation with `xTaskCreateStaticPinnedToCore`: USB daemon/service and SPI producer on CPU1; BLE worker, bond button, and diagnostics on CPU0. Set the exact priorities from the Interfaces block and keep all existing static stack buffers.

- [ ] **Step 3: Make the input pipeline event-driven**

  Move the current processing loop into a statically allocated `input_pipeline` task at priority 7 on CPU1. Register a wake callback that calls `xTaskNotifyGive()` on that task, replace `vTaskDelayUntil(..., 1 ms)` with `ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1))`, drain all available arbiter reports after every wake, and retain the 1 ms timeout for BLE fallback and watchdog reset. Create the pipeline and publish its handle before starting USB/BLE sources; `app_main` returns after startup.

- [ ] **Step 4: Add affinity diagnostics**

  Log `xPortGetCoreID()` once at startup from the pipeline, USB daemon/service, SPI producer, BLE worker, bond-button, and diagnostics tasks. Document expected input-board placement in `docs/bringup.md`.

- [ ] **Step 5: Build and verify input firmware**

  Run: `. .toolchains/esp-idf/export.sh && idf.py -C apps/input set-target esp32s3 && idf.py -C apps/input build`

  Expected: `set-target` preserves the ignored prior configuration as `sdkconfig.old`, regenerates `sdkconfig` from the updated defaults, and the build succeeds with two FreeRTOS cores and produces `apps/input/build/mouse_bridge_input.bin`.

- [ ] **Step 6: Run host regression tests**

  Run: `cmake --build host-tests/build && ctest --test-dir host-tests/build --output-on-failure`

  Expected: all tests pass.

- [ ] **Step 7: Commit**

  ```bash
  git add apps/input/sdkconfig.defaults apps/input/main/app_main.cpp apps/input/main/usb_mouse_host.cpp apps/input/main/spi_packet_producer.cpp apps/input/main/ble_hogp_client.cpp apps/input/main/bond_control.cpp docs/bringup.md
  git commit -m "perf: isolate input fast path on cpu1"
  ```

### Task 3: Output-board affinity and packet-assisted HID wake

**Files:**
- Modify: `apps/output/main/usb_mouse_device.hpp`
- Modify: `apps/output/main/usb_mouse_device.cpp`
- Modify: `apps/output/main/spi_packet_consumer.cpp`
- Modify: `apps/output/main/app_main.cpp`
- Modify: `docs/bringup.md`
- Modify: `docs/validation.md`
- Test: `host-tests/output_state_test.cpp`
- Test: `host-tests/report_scheduler_test.cpp`

**Interfaces:**
- Produces: `UsbMouseDevice::notify_packet_available() -> void` as an internal wake operation called after state/scheduler update.
- Produces: SPI consumer priority 8 on CPU0, HID scheduler priority 8 on CPU1, and diagnostics priority 1 on CPU0; esp_tinyusb remains at its multicore default priority 5 on CPU1.

- [ ] **Step 1: Extend regression tests for busy-endpoint accumulation**

  Add test cases in which two physical reports reach `OutputState` before `tick()` and assert summed X/Y/wheel motion and latest buttons. Add a scheduler assertion that repeated event notifications before `on_sent()` do not advance or lose the cadence phase.

- [ ] **Step 2: Run focused tests and verify the new assertions**

  Run: `cmake --build host-tests/build --target output_state_test report_scheduler_test && ./host-tests/build/output_state_test && ./host-tests/build/report_scheduler_test`

  Expected: existing accumulation behavior passes; the tests pin behavior before concurrency changes.

- [ ] **Step 3: Pin output tasks**

  Create SPI consumer and diagnostics with `xTaskCreateStaticPinnedToCore(..., 0)`. Create the HID scheduler with `xTaskCreateStaticPinnedToCore(..., 1)`. Explicitly set `tinyusb_config_t::task.xCoreID = 1` and priority 5 so managed-component defaults cannot drift.

- [ ] **Step 4: Add packet-assisted HID wakeup**

  Store the HID task handle during `start()`. After `on_packet()` releases `state_lock_`, call `xTaskNotifyGive(task_handle_)`. Replace the fixed `vTaskDelayUntil` with `ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1))`; keep timeout evaluation and bypass sampling time-gated to 1 ms so report bursts cannot accelerate debounce.

- [ ] **Step 5: Add output affinity and latency diagnostics**

  Log each output task's startup core. Track packet-to-HID-stage maximum latency from `rx_us` to the first scheduler pass without changing the wire protocol, expose it through `UsbMouseDevice`, and include it in `MB_DIAG`. Document expected placement and hardware checks.

- [ ] **Step 6: Run all tests and build both firmwares**

  Run: `cmake --build host-tests/build && ctest --test-dir host-tests/build --output-on-failure`

  Run: `. .toolchains/esp-idf/export.sh && idf.py -C apps/input build && idf.py -C apps/output reconfigure build`

  Expected: all host tests pass and both firmware images build.

- [ ] **Step 7: Run static verification**

  Run: `rg -n "xTaskCreateStatic\\(" apps/input/main apps/output/main`

  Expected: no application task remains unpinned.

  Run: `git diff --check`

  Expected: no output.

- [ ] **Step 8: Commit**

  ```bash
  git add apps/output/main/usb_mouse_device.hpp apps/output/main/usb_mouse_device.cpp apps/output/main/spi_packet_consumer.cpp apps/output/main/app_main.cpp host-tests/output_state_test.cpp host-tests/report_scheduler_test.cpp docs/bringup.md docs/validation.md
  git commit -m "perf: isolate output hid path on cpu1"
  ```

### Task 4: Whole-system verification and handoff

**Files:**
- Modify: `README.md`
- Modify: `docs/validation.md`

**Interfaces:**
- Consumes: dual-core input and output firmware from Tasks 1-3.
- Produces: reproducible verification evidence and flashing guidance for the matched protocol-v2 pair.

- [ ] **Step 1: Run clean host verification**

  Run: `cmake -S host-tests -B host-tests/build && cmake --build host-tests/build && ctest --test-dir host-tests/build --output-on-failure`

  Expected: all tests pass.

- [ ] **Step 2: Build both production images**

  Run: `. .toolchains/esp-idf/export.sh && idf.py -C apps/input fullclean build && idf.py -C apps/output fullclean build`

  Expected: both builds succeed without affinity or watchdog warnings.

- [ ] **Step 3: Document validation status**

  Record host-test count, image sizes, expected task/core log lines, and that hardware latency/polling validation remains `CHƯA CHẠY` until both boards are flashed and measured. Keep the warning that both protocol-v2 boards must be flashed together.

- [ ] **Step 4: Review the complete diff**

  Check report loss, lock scope, notification-before-handle races, task priorities, watchdog reset paths, suspend/resume, fallback timing, and the five Review Focus cases. Fix any issue and rerun the relevant checks.

- [ ] **Step 5: Commit**

  ```bash
  git add README.md docs/validation.md
  git commit -m "docs: validate dual-core mouse pipeline"
  ```
