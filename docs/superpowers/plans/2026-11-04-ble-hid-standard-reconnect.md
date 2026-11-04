# BLE HID Standard Reconnect Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add persistent standards-based BLE HID bonding and identity-aware reconnect while retaining validated direct-GATT compatibility.

**Architecture:** Extend the existing direct NimBLE GATT client instead of returning to `esp_hidh`. NimBLE owns SMP keys and RPA resolution; the existing device manager owns user selection/history and consumes resolved identities.

**Tech Stack:** ESP-IDF 5.4, NimBLE central/GATT client, NVS, C++20 host tests.

**Spec:** `docs/superpowers/specs/2026-11-04-ble-hid-standard-reconnect-design.md`

## Global Constraints

- Target ESP32-S3 N8R8 and keep the existing direct-GATT HID parser/bridge.
- Pair with NoInputNoOutput, bonding enabled, MITM disabled.
- Security failure may fall back only when the BLE link remains connected.
- Device names are never the preferred identity when IRK resolution succeeds.

## Review Focus

- Bonded mouse advertises with a rotating RPA and no name: reconnect by resolved identity.
- Unbonded mouse rejects security but permits HID GATT: continue validation unencrypted.
- Stored peer has stale keys: handle repeat pairing without permanent reconnect failure.
- Forget is requested for a bonded connected or disconnected peer: remove the correct bond.
- Disconnect during pairing or discovery: reset state once and resume scanning.

---

### Task 1: Identity-first reconnect selection

**Files:**
- Modify: `host-tests/ble_device_manager_test.cpp`
- Modify: `apps/ble_only/main/ble_device_manager.cpp`

**Interfaces:**
- Consumes: `BleDeviceManager::on_advertisement(..., std::optional<BlePeerAddress> resolved_identity, ...)`.
- Produces: identity-first selection with exact-name compatibility fallback only when no resolved identity exists.

- [ ] **Step 1: Add failing tests** proving a nameless resolved RPA reconnects and a resolved foreign identity with the same name does not reconnect.
- [ ] **Step 2: Run** `cmake --build host-tests/build --target ble_device_manager_test && host-tests/build/ble_device_manager_test`; expect the foreign-identity test to fail before implementation.
- [ ] **Step 3: Change selection** so name fallback is eligible only when `resolved_identity` is absent.
- [ ] **Step 4: Run the test target** and expect PASS.
- [ ] **Step 5: Commit** identity-first reconnect behavior.

### Task 2: Persistent SMP and adaptive direct-GATT fallback

**Files:**
- Modify: `apps/ble_only/main/ble_hogp_client.hpp`
- Modify: `apps/ble_only/main/ble_hogp_client.cpp`
- Modify: `apps/ble_only/sdkconfig.defaults`

**Interfaces:**
- Consumes: NimBLE security store, GAP security events, `ble_gap_rpa_resolve`, and the existing HID discovery pipeline.
- Produces: persistent bonding, RPA resolution, repeat-pair repair, bond deletion, and guarded security-to-GATT transition.

- [ ] **Step 1: Configure SMP** with bonding, Secure Connections, ENC+ID key distribution, security-store callbacks, `ble_store_config_init()`, NVS persistence, and up to eight bonds.
- [ ] **Step 2: Resolve scan RPAs** and pass the resolved identity to the device manager while connecting to the observed address.
- [ ] **Step 3: Add guarded security startup** after connect; begin discovery once after successful encryption or a non-fatal security failure.
- [ ] **Step 4: Handle repeat pairing and forget** by deleting NimBLE peer security material.
- [ ] **Step 5: Build firmware** with `export IDF_TOOLS_PATH="$PWD/.toolchains/idf-tools"; . .toolchains/esp-idf/export.sh; idf.py -C apps/ble_only reconfigure build`; expect success.
- [ ] **Step 6: Run regression suite** with `cmake -S host-tests -B host-tests/build && cmake --build host-tests/build && ctest --test-dir host-tests/build --output-on-failure && git diff --check`; expect all tests and checks to pass.
- [ ] **Step 7: Commit** the persistent reconnect implementation.

