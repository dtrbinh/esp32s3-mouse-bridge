# BLE-Only Status LED Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Drive the ESP32-S3-Zero onboard WS2812 at a thermally limited 10% total channel budget to show BLE scan/connection, mod state, and the selected profile.

**Architecture:** A portable state/color model converts atomic BLE and profile snapshots plus monotonic time into one limited RGB value. A low-priority static CPU0 task owns the one-pixel RMT LED strip and updates it only when that RGB value changes; CPU1 report/profile/USB tasks never touch the LED driver.

**Tech Stack:** C++20, ESP-IDF 6.0.3, FreeRTOS SMP static tasks, `espressif/led_strip` RMT backend, CMake/CTest.

**Spec:** `docs/superpowers/specs/2026-11-04-ble-only-status-led-design.md`

## Global Constraints

- The feature applies only to `apps/ble_only`; `apps/input` and `apps/output` gain no LED task or GPIO21 ownership.
- GPIO21 is the configurable default for one onboard WS2812 pixel.
- Scan is yellow with a 300 ms cycle; connected with mods off is green with a 2,000 ms cycle; connected with mods on is a solid profile color.
- Every emitted RGB sum is at most 76; off is exactly `0,0,0`.
- LED sampling is every 50 ms on CPU0, below the BLE worker priority, with static internal-memory task storage.
- LED initialization failure is non-fatal to BLE, profiles, and USB HID.
- BLE/profile state crosses cores only through atomic snapshots; LED writes never occur in callbacks or the CPU1 fast path.

## Review Focus

- Mixed/white colors near the power limit must remain at or below an RGB sum of 76 after integer rounding.
- Entering either blink state at an arbitrary uptime must begin with its on phase, not inherit the old state's phase.
- Clear-bond and physical disconnect must expose scan yellow without waiting for a stale device pointer.
- Missing/failed LED hardware must not abort startup or disturb BLE-to-USB mouse operation.
- Profile changes and mod toggles racing with LED sampling must yield a complete old or new atomic snapshot, never mutate the profile engine from CPU0.

---

### Task 1: Portable LED status and brightness model

**Files:**
- Create: `apps/ble_only/main/status_led_model.hpp`
- Create: `apps/ble_only/main/status_led_model.cpp`
- Create: `host-tests/status_led_model_test.cpp`
- Modify: `host-tests/CMakeLists.txt`

**Interfaces:**
- Produces: `struct LedRgb { uint8_t red, green, blue; bool operator==(...) const; }`.
- Produces: `limit_led_rgb(LedRgb logical) -> LedRgb`, preserving ratios while enforcing a channel sum no greater than 76.
- Produces: `StatusLedModel::update(bool connected, bool mods_enabled, size_t profile_number, uint32_t now_ms) -> LedRgb`.

- [ ] **Step 1: Write the failing model test**

Assert scan yellow at transition and 149 ms, off at 150 ms, and yellow again at 300 ms. Assert connected/mods-off green at transition and 999 ms, off at 1,000 ms, and green again at 2,000 ms. Assert a transition at arbitrary uptime restarts the on phase, all 14 connected/mods-on colors are distinct and solid, profile 0/15 clamp to 1/14, off is zero, and every palette/system color has output sum `<=76` with mixed-channel ratios retained within integer rounding.

- [ ] **Step 2: Run focused test and verify RED**

Run: `cmake -S host-tests -B host-tests/build && cmake --build host-tests/build --target status_led_model_test`

Expected: compilation fails because `status_led_model.hpp` does not exist.

- [ ] **Step 3: Implement the portable model**

Use semantic states `Scanning`, `ConnectedPassThrough`, and `Profile`. Store the last semantic state/profile and its transition timestamp so blink phase starts on. Use the spec's exact logical palette and a deterministic integer limiter with a hard post-rounding sum cap.

- [ ] **Step 4: Run host suite**

Run: `cmake --build host-tests/build && ctest --test-dir host-tests/build --output-on-failure`

Expected: 14/14 tests pass.

- [ ] **Step 5: Commit**

```bash
git add apps/ble_only/main/status_led_model.hpp apps/ble_only/main/status_led_model.cpp host-tests/status_led_model_test.cpp host-tests/CMakeLists.txt
git commit -m "feat: model ble-only status led"
```

### Task 2: Atomic BLE connected snapshot

**Files:**
- Modify: `apps/input/main/ble_hogp_client.hpp`
- Modify: `apps/input/main/ble_hogp_client.cpp`

**Interfaces:**
- Produces: `BleHogpClient::connected() const -> bool` backed by `std::atomic<bool>`.
- Contract: false before a valid open, true after report-map compilation succeeds, false at the start of `clear_bond()`, and false on the matching close event.

- [ ] **Step 1: Add the atomic connection lifecycle**

Update the four lifecycle edges without changing scan, forwarding, or bond behavior. The snapshot is read-only to the LED controller.

- [ ] **Step 2: Build shared BLE client and run host tests**

Run: `. .toolchains/esp-idf/export.sh && idf.py -C apps/input build`

Run: `cmake --build host-tests/build && ctest --test-dir host-tests/build --output-on-failure`

Expected: input firmware builds and 14/14 host tests pass.

- [ ] **Step 3: Commit**

```bash
git add apps/input/main/ble_hogp_client.hpp apps/input/main/ble_hogp_client.cpp
git commit -m "feat: expose ble connection state"
```

### Task 3: CPU0 WS2812 controller and BLE-only integration

**Files:**
- Create: `apps/ble_only/main/status_led.hpp`
- Create: `apps/ble_only/main/status_led.cpp`
- Modify: `apps/ble_only/main/CMakeLists.txt`
- Modify: `apps/ble_only/main/Kconfig.projbuild`
- Modify: `apps/ble_only/main/idf_component.yml`
- Modify: `apps/ble_only/main/app_main.cpp`
- Regenerate: `apps/ble_only/dependencies.lock`

**Interfaces:**
- Consumes: `BleHogpClient::connected()`, `BleOnlyMode::mods_enabled()`, and `BleOnlyMode::selected_profile()` atomic snapshots.
- Produces: `StatusLed::start() -> esp_err_t`; caller logs failure and continues startup.
- Produces: one static CPU0 task that owns the LED strip handle and samples every 50 ms.

- [ ] **Step 1: Implement the RMT controller**

Initialize one pixel on `CONFIG_BLE_ONLY_LED_GPIO` with the `led_strip` RMT backend at 10 MHz and DMA disabled. Clear once after initialization. Create the static task pinned to CPU0 at priority 1; call `set_pixel` and `refresh` only when model output changes.

- [ ] **Step 2: Integrate non-fatal startup and configuration**

Default `BLE_ONLY_LED_GPIO` to 21, add `espressif/led_strip` version `^3.0.0`, include the model/controller sources, and start LED before BLE. Log but do not `ESP_ERROR_CHECK` an LED failure.

- [ ] **Step 3: Build BLE-only firmware and resolve the lock**

Run: `. .toolchains/esp-idf/export.sh && idf.py -C apps/ble_only fullclean build`

Expected: dependency lock includes a compatible `espressif/led_strip`; firmware fits the 1 MiB partition.

- [ ] **Step 4: Verify task/core and regression gates**

Run: `rg -n "xTaskCreateStatic\\(" apps/ble_only apps/input/main apps/output/main`

Expected: no unpinned application task creation.

Run: `cmake --build host-tests/build && ctest --test-dir host-tests/build --output-on-failure && git diff --check`

Expected: 14/14 tests pass and no whitespace errors.

- [ ] **Step 5: Commit**

```bash
git add apps/ble_only
git commit -m "feat: add ble-only ws2812 status"
```

### Task 4: LED bring-up and hardware validation handoff

**Files:**
- Modify: `README.md`
- Modify: `docs/bringup.md`
- Modify: `docs/validation.md`

**Interfaces:**
- Consumes: status priority, timings, profile palette, and built image from Tasks 1-3.
- Produces: visible-state reference and hardware checks that remain `CHƯA CHẠY` until measured.

- [ ] **Step 1: Document visible states and brightness**

Document yellow 300 ms scan, green 2 s connected pass-through, solid profile colors, GPIO21, total RGB cap 76, and LED failure being non-fatal.

- [ ] **Step 2: Add hardware validation rows**

Add all three state priorities, 14 profile colors, BOOT transition response, reconnect/bond-clear yellow response, and a 30-minute temperature check as `CHƯA CHẠY`.

- [ ] **Step 3: Run final verification**

Run: `cmake -S host-tests -B host-tests/build && cmake --build host-tests/build && ctest --test-dir host-tests/build --output-on-failure`

Run: `. .toolchains/esp-idf/export.sh && idf.py -C apps/ble_only fullclean build`

Run: `git diff --check`

Expected: 14/14 tests pass, firmware builds from clean state, and the diff is clean.

- [ ] **Step 4: Commit**

```bash
git add README.md docs/bringup.md docs/validation.md
git commit -m "docs: add status led bring-up"
```
