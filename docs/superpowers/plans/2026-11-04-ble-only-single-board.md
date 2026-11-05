# BLE-Only Single-Board Firmware Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an independently flashable ESP32-S3 application that receives a BLE HID mouse, optionally applies one of 14 profiles, and emits USB HID directly to a PC from one board.

**Architecture:** Reuse the existing BLE HOGP client and source arbiter from the input application and the output state/scheduler/TinyUSB device from the output application. A CPU1 pipeline drains BLE reports, applies profile commands and generated motion, then injects reports directly into the USB HID device; CPU0 owns NimBLE, BOOT interpretation, and diagnostics.

**Tech Stack:** C++20, ESP-IDF 6.0.3, FreeRTOS SMP static tasks, NimBLE/ESP HIDH, esp_tinyusb/TinyUSB, CMake/CTest.

**Spec:** `docs/superpowers/specs/2026-11-04-ble-only-single-board-design.md`

## Global Constraints

- The new application is `apps/ble_only`; do not change the runtime behavior of `apps/input` or `apps/output`.
- ESP32-S3 remains dual-core: BLE/NimBLE/BOOT/diagnostics on CPU0 and report/profile/TinyUSB/HID on CPU1.
- GPIO0 actions are `<700 ms` next profile, `700-1499 ms` no action, `1500-4999 ms` toggle mods on release, and `>=5000 ms` clear bond immediately.
- There are exactly 14 profiles, power-on mods are enabled, and state changes reset generated-motion state.
- USB remains Full Speed HID with `bInterval=1`; existing 5 ms motion timeout, 20 ms release timeout, suspend/resume release, endpoint-busy accumulation, and watchdog behavior remain intact.
- No USB Host or SPI subsystem is initialized by the BLE-only application.
- Direct notifications are wake hints only; queues remain the source of truth.
- All application tasks are statically allocated, use internal memory, and are pinned to an explicit core.

## Review Focus

- BOOT release near 700 ms or 1500 ms must produce at most one action and never an accidental profile change.
- Holding BOOT beyond 5 seconds must clear bond once; release must not also toggle mods.
- BLE reports and BOOT commands arriving before the pipeline runs must both be retained despite notification coalescing.
- Switching profile or disabling mods during generated motion must reset the profile engine before the next report.
- BLE disconnect, USB suspend/resume, and a busy HID endpoint must still release/accumulate state according to the existing safety deadlines.

---

### Task 1: Portable BOOT press classifier

**Files:**
- Create: `apps/ble_only/main/boot_press.hpp`
- Create: `apps/ble_only/main/boot_press.cpp`
- Create: `host-tests/boot_press_test.cpp`
- Modify: `host-tests/CMakeLists.txt`

**Interfaces:**
- Produces: `enum class BootAction { None, NextProfile, ToggleMods, ClearBond }`.
- Produces: `BootPressClassifier::update(bool pressed, uint32_t elapsed_ms) -> BootAction`, where `elapsed_ms` is monotonic time and one physical press emits at most one terminal action.

- [ ] **Step 1: Write the failing state-machine test**

Add `boot_press_test` assertions for 680 ms next-profile, the 700-1480 ms dead zone, 1500 ms toggle on release, 5000 ms immediate clear, continued hold/release after clear producing no action, and independent actions across consecutive presses.

- [ ] **Step 2: Run the focused test and verify RED**

Run: `cmake -S host-tests -B host-tests/build && cmake --build host-tests/build --target boot_press_test`

Expected: compilation fails because `boot_press.hpp` does not exist.

- [ ] **Step 3: Implement the classifier**

Use elapsed timestamps rather than sample counts. Latch `ClearBond` once at 5000 ms; classify shorter presses only on the pressed-to-released transition; reset state only after release.

- [ ] **Step 4: Run host tests**

Run: `cmake --build host-tests/build && ctest --test-dir host-tests/build --output-on-failure`

Expected: 11/11 tests pass.

- [ ] **Step 5: Commit**

```bash
git add apps/ble_only/main/boot_press.hpp apps/ble_only/main/boot_press.cpp host-tests/boot_press_test.cpp host-tests/CMakeLists.txt
git commit -m "feat: classify ble-only boot actions"
```

### Task 2: USB device mode without GPIO0 bypass

**Files:**
- Modify: `apps/output/main/usb_mouse_device.hpp`
- Modify: `apps/output/main/usb_mouse_device.cpp`

**Interfaces:**
- Produces: `UsbMouseDevice::start(bool enable_bypass_button = true) -> esp_err_t`.
- Contract: `apps/output` retains its current default GPIO0 emergency bypass; `apps/ble_only` passes `false` so GPIO0 is owned exclusively by its BOOT controller.

- [ ] **Step 1: Add the configuration interface**

Store the option before task creation. Skip GPIO0 configuration and debounce sampling when disabled; do not change output firmware's default behavior.

- [ ] **Step 2: Build output regression firmware and host tests**

Run: `cmake --build host-tests/build && ctest --test-dir host-tests/build --output-on-failure`

Run: `. .toolchains/esp-idf/export.sh && idf.py -C apps/output build`

Expected: host tests pass and `mouse_bridge_output.bin` builds with the existing call to `start()` unchanged.

- [ ] **Step 3: Commit**

```bash
git add apps/output/main/usb_mouse_device.hpp apps/output/main/usb_mouse_device.cpp
git commit -m "refactor: make output bypass button optional"
```

### Task 3: BLE-only ESP-IDF application

**Files:**
- Create: `apps/ble_only/CMakeLists.txt`
- Create: `apps/ble_only/sdkconfig.defaults`
- Create: `apps/ble_only/main/CMakeLists.txt`
- Create: `apps/ble_only/main/idf_component.yml`
- Create: `apps/ble_only/main/Kconfig.projbuild`
- Create: `apps/ble_only/main/boot_control.hpp`
- Create: `apps/ble_only/main/boot_control.cpp`
- Create: `apps/ble_only/main/ble_only_mode.hpp`
- Create: `apps/ble_only/main/ble_only_mode.cpp`
- Create: `apps/ble_only/main/app_main.cpp`
- Create after dependency resolution: `apps/ble_only/dependencies.lock`
- Create: `host-tests/ble_only_mode_test.cpp`
- Modify: `host-tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `BootPressClassifier::update(...)`, `SourceArbiter::set_wake_callback(...)`, `ProfileEngine`, `BleHogpClient`, and `UsbMouseDevice::start(false)`.
- Produces: `BleOnlyMode::enqueue(BootAction) -> bool`, `apply_next() -> std::optional<BootAction>`, and `process(MouseReport, uint64_t) -> MouseReport`; its bounded single-producer/single-consumer queue retains every accepted `NextProfile` and `ToggleMods` command until the pipeline applies it.
- Produces: a static CPU1 pipeline plus static CPU0 BOOT/diagnostics tasks.
- Produces: `apps/ble_only/build/mouse_bridge_ble_only.bin`.

- [ ] **Step 1: Write the failing mode/command test**

Add `ble_only_mode_test` assertions that several commands queued before a drain are applied in order, profile 14 wraps to profile 1, toggling mods resets active generated motion, and disabled mods return raw X/Y/buttons/wheel/pan.

- [ ] **Step 2: Run the focused test and verify RED**

Run: `cmake -S host-tests -B host-tests/build && cmake --build host-tests/build --target ble_only_mode_test`

Expected: compilation fails because `ble_only_mode.hpp` does not exist.

- [ ] **Step 3: Implement the portable mode controller**

Use an eight-entry SPSC ring with atomic producer/consumer indices. Only the CPU0 BOOT task enqueues; only the CPU1 pipeline calls `apply_next()` and `process()`. Reject a full queue without overwriting an accepted action.

- [ ] **Step 4: Create the application and combined configuration**

Enable ESP32-S3 N4R2 settings, NimBLE central/HOGP, one persistent bond, CPU0 NimBLE affinity, CPU1 main-task affinity, one TinyUSB HID interface, watchdog, and configurable default profile 1-14. Reference the reusable sibling sources from the new main component instead of copying them.

- [ ] **Step 5: Implement BOOT task and lossless command handoff**

Sample GPIO0 every 20 ms on CPU0. Call `ble.clear_bond()` immediately for `ClearBond`; enqueue `NextProfile` and `ToggleMods` through `BleOnlyMode` and wake the CPU1 pipeline. Log every accepted action and queue failure.

- [ ] **Step 6: Implement the CPU1 report/profile pipeline**

Create USB HID with GPIO0 bypass disabled before BLE starts. Publish the pipeline handle and register the source wake callback before starting BLE. On every wake or 1 ms maintenance timeout, drain all mode commands first, then drain all arbiter reports through `BleOnlyMode::process()` and call `UsbMouseDevice::on_packet()` with a monotonic local sequence. Reset mode state on boundaries and service the task watchdog.

- [ ] **Step 7: Add diagnostics and affinity evidence**

Every five seconds log BLE connections/interval, selected profile, mods state, active source, output interval, timeout count, and resume count. Log startup cores for pipeline, BLE/NimBLE (from reused source), BOOT, diagnostics, and TinyUSB/HID (from reused source).

- [ ] **Step 8: Build BLE-only firmware**

Run: `. .toolchains/esp-idf/export.sh && idf.py -C apps/ble_only set-target esp32s3 && idf.py -C apps/ble_only build`

Expected: `mouse_bridge_ble_only.bin` builds, dependencies lock to ESP-IDF 6.0.3-compatible versions, and the image fits the 1 MiB app partition.

- [ ] **Step 9: Run static and host verification**

Run: `rg -n "xTaskCreateStatic\\(" apps/ble_only apps/input/main apps/output/main`

Expected: no unpinned application task creation.

Run: `cmake --build host-tests/build && ctest --test-dir host-tests/build --output-on-failure && git diff --check`

Expected: 12/12 tests pass and no whitespace errors.

- [ ] **Step 10: Commit**

```bash
git add apps/ble_only
git commit -m "feat: add single-board ble usb hid firmware"
```

### Task 4: Flashing and one-board validation handoff

**Files:**
- Modify: `README.md`
- Modify: `docs/bringup.md`
- Modify: `docs/validation.md`

**Interfaces:**
- Consumes: the BLE-only binary and BOOT behavior from Tasks 1-3.
- Produces: exact build/flash/monitor commands and a hardware checklist that remains `NOT RUN` until measured on the user's board.

- [ ] **Step 1: Document one-board operation**

Document that the onboard USB-C goes directly to the PC, no VBUS Host circuit or second board is used, how to place the board in download mode, and how short/1.5 s/5 s BOOT actions behave. Warn that the 700-1480 ms dead zone intentionally performs no action.

- [ ] **Step 2: Add validation cases**

Add BLE-only enumeration, pair, raw pass-through with mods off, all 14 profile selections, 1.5-second toggle, 5-second bond clear, reconnect, disconnect release, suspend/resume, and latency checks as `NOT RUN`.

- [ ] **Step 3: Run final clean verification**

Run: `cmake -S host-tests -B host-tests/build && cmake --build host-tests/build && ctest --test-dir host-tests/build --output-on-failure`

Run: `. .toolchains/esp-idf/export.sh && idf.py -C apps/ble_only fullclean build`

Run: `git diff --check`

Expected: 12/12 host tests pass, BLE-only firmware builds from clean state, and the diff is clean.

- [ ] **Step 4: Commit**

```bash
git add README.md docs/bringup.md docs/validation.md
git commit -m "docs: add ble-only bring-up guide"
```
