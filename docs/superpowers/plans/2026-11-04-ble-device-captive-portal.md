# BLE Device Captive Portal Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an on-device Wi-Fi captive portal to `apps/ble_only` for explicitly selecting, reconnecting, disconnecting, and forgetting BLE mice such as the Freewolf X7.

**Architecture:** Portable fixed-capacity models own BLE observations, saved-device history, setup state, JSON, and DNS packet encoding. `BleHogpClient` gains an optional managed-selection mode for `apps/ble_only`, while a CPU0 setup controller owns NVS, Wi-Fi AP, DNS, and HTTP; after a chosen mouse passes HID report-map validation, Wi-Fi is completely stopped and the existing CPU1 USB HID fast path is unchanged.

**Tech Stack:** C++20, ESP-IDF 6.0.3, NimBLE HOGP, NVS, `esp_wifi`, `esp_netif`, `esp_http_server`, lwIP sockets, FreeRTOS SMP static tasks, CMake/CTest, embedded HTML/CSS/JavaScript.

**Spec:** `docs/superpowers/specs/2026-11-04-ble-device-captive-portal-design.md`

## Global Constraints

- Only `apps/ble_only` gets portal/device-selection behavior; `apps/input` retains automatic HID discovery and `apps/output` remains unchanged.
- The AP is open, local-only, named `MouseBridge-Setup-XXXX`, and uses `192.168.4.1`; wildcard DNS and captive-probe routes open the embedded page.
- With a selected peer, portal fallback starts after 10,000 ms without a valid mouse connection; nearby observations expire after 15,000 ms.
- Saved history capacity is 8; nearby capacity is 32; successful peers are stored by NimBLE-resolved identity address, not a transient RPA.
- Only an explicit selection or the selected saved identity can connect in managed mode; report-map validation remains authoritative.
- Selecting B disconnects A before trying B; failure stays in the portal without silently reconnecting A.
- Disconnect clears the selected target but preserves history and bond; forget removes both history and the matching NimBLE bond.
- Successful selection shows success for 3,000 ms, then HTTP, DNS, AP, and the Wi-Fi driver stop completely.
- Portal/BLE/Wi-Fi application work runs on CPU0 below the BLE worker; USB HID, report scheduling, and profile processing remain on CPU1.
- All lists, names, request bodies, DNS packets, JSON responses, queues, stacks, and snapshots are bounded; portal failures never abort USB HID.
- The finished BLE-only image must fit the existing 1 MiB application partition.

## Review Focus

- A bonded mouse rotating its RPA must resolve to its saved identity and reconnect; pin this in Task 5 with resolved-identity selection tests and an ESP-IDF build using `ble_gap_rpa_resolve`.
- Malformed/truncated advertisement fields and hostile names containing quotes, control bytes, or invalid UTF-8 must not corrupt state or JSON; pin catalog parsing in Task 1 and escaping/truncation in Task 4.
- Switching A→B and failing B must leave no hidden auto-reconnect target; pin the exact state sequence in Task 3.
- Malformed, compressed, oversized, and non-A DNS questions must be rejected or answered without out-of-bounds access; pin packet cases in Task 4.
- Partial Wi-Fi/HTTP/DNS startup and shutdown failures must unwind only acquired resources and leave BLE/USB alive; pin lifecycle decisions in Task 6 and verify runtime ownership in Task 7.

---

### Task 1: Fixed-capacity BLE nearby catalog

**Files:**
- Create: `apps/input/main/ble_peer.hpp`
- Create: `apps/ble_only/main/ble_device_catalog.hpp`
- Create: `apps/ble_only/main/ble_device_catalog.cpp`
- Create: `host-tests/ble_device_catalog_test.cpp`
- Modify: `host-tests/CMakeLists.txt`

**Interfaces:**
- Produces: `BlePeerAddress { std::array<uint8_t, 6> bytes; uint8_t type; }` with equality and stable display formatting.
- Produces: `BleAdvertisement { BlePeerAddress address; std::string_view name; int8_t rssi; std::optional<uint16_t> appearance; bool advertises_hid; }`.
- Produces: `BleNearbyDevice` with fixed 32-byte UTF-8-safe name, strongest/latest RSSI, merged Appearance/HID flags, `last_seen_ms`, and address.
- Produces: `BleDeviceCatalog::observe(const BleAdvertisement&, uint32_t now_ms)`, `BleDeviceCatalog::snapshot(uint32_t now_ms) -> BleNearbySnapshot`, and `BleDeviceCatalog::clear()`.

- [ ] **Step 1: Write the failing catalog test**

Assert that advertising plus scan-response observations for one address merge; a later non-empty name wins; RSSI ordering is descending with address as deterministic tie-break; entries older than 15,000 ms disappear; the 33rd observation evicts the stalest then weakest entry; overlong, control-containing, and invalid UTF-8 names become bounded valid display text; and address type participates in identity.

- [ ] **Step 2: Run the focused test and verify RED**

Run: `cmake -S host-tests -B host-tests/build && cmake --build host-tests/build --target ble_device_catalog_test`

Expected: FAIL because `ble_device_catalog.hpp` does not exist.

- [ ] **Step 3: Implement the catalog**

Use `std::array<BleNearbyDevice, 32>` and explicit counts; no heap-owned strings. Normalize names when copying, merge strictly by full `BlePeerAddress`, and prune during snapshot creation without mutating caller-owned data.

- [ ] **Step 4: Run the host suite**

Run: `cmake --build host-tests/build && ctest --test-dir host-tests/build --output-on-failure`

Expected: 15/15 tests pass.

- [ ] **Step 5: Commit**

```bash
git add apps/input/main/ble_peer.hpp apps/ble_only/main/ble_device_catalog.hpp apps/ble_only/main/ble_device_catalog.cpp host-tests/ble_device_catalog_test.cpp host-tests/CMakeLists.txt
git commit -m "feat: model BLE nearby devices"
```

### Task 2: Saved history and versioned persistence codec

**Files:**
- Create: `apps/ble_only/main/ble_device_history.hpp`
- Create: `apps/ble_only/main/ble_device_history.cpp`
- Create: `apps/ble_only/main/ble_device_store.hpp`
- Create: `apps/ble_only/main/ble_device_store.cpp`
- Create: `host-tests/ble_device_history_test.cpp`
- Modify: `apps/ble_only/main/CMakeLists.txt`
- Modify: `host-tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `BlePeerAddress` and bounded display names from Task 1.
- Produces: `BleSavedDevice { BlePeerAddress identity; std::array<char, 33> name; uint32_t last_success_order; }`.
- Produces: `BleDeviceHistory::record_success(...)`, `forget(...)`, `disconnect_without_forget()`, `selected()`, `select(...)`, and `snapshot(connected_identity)` with connected-first then most-recent ordering and capacity 8.
- Produces: `encode_ble_history(const BleDeviceHistory&, std::span<uint8_t>) -> size_t` and `decode_ble_history(std::span<const uint8_t>) -> std::optional<BleDeviceHistory>` using magic, version 1, payload length, and CRC32.
- Produces: ESP-only `BleDeviceStore::load() -> BleDeviceHistory` and `save(const BleDeviceHistory&) -> esp_err_t` in NVS namespace `mb_ble`, key `history`.

- [ ] **Step 1: Write the failing history/codec test**

Assert success records select and move a peer to most-recent; the ninth distinct peer evicts the least-recent non-connected peer; connected sorts first; disconnect clears only selection; forget clears selection only when it matches; encode/decode round-trips; and bad magic, version, length, CRC, count, or truncated blobs decode to empty failure without reading past input.

- [ ] **Step 2: Run the focused test and verify RED**

Run: `cmake --build host-tests/build --target ble_device_history_test`

Expected: FAIL because `ble_device_history.hpp` does not exist.

- [ ] **Step 3: Implement portable history and codec**

Keep the encoded layout independent of C++ struct padding and endian ambiguity. Increment a monotonic order counter and renormalize existing orders before overflow.

- [ ] **Step 4: Implement the NVS adapter**

`BleDeviceStore` treats `ESP_ERR_NVS_NOT_FOUND`, corrupt data, and unknown versions as empty history; failed writes return an error for logging but do not stop BLE or USB HID.

- [ ] **Step 5: Run the host suite**

Run: `cmake --build host-tests/build && ctest --test-dir host-tests/build --output-on-failure`

Expected: 16/16 tests pass.

- [ ] **Step 6: Build the BLE-only NVS adapter**

Add the Task 1 catalog and Task 2 history/store sources to the BLE-only
component, then run: `export IDF_TOOLS_PATH="$PWD/.toolchains/idf-tools"; . .toolchains/esp-idf/export.sh; idf.py -C apps/ble_only build`

Expected: the firmware builds and remains below `0x100000`.

- [ ] **Step 7: Commit**

```bash
git add apps/ble_only/main/ble_device_history.* apps/ble_only/main/ble_device_store.* apps/ble_only/main/CMakeLists.txt host-tests/ble_device_history_test.cpp host-tests/CMakeLists.txt
git commit -m "feat: persist BLE device history"
```

### Task 3: Setup and device-switch state machine

**Files:**
- Create: `apps/ble_only/main/ble_setup_state.hpp`
- Create: `apps/ble_only/main/ble_setup_state.cpp`
- Create: `host-tests/ble_setup_state_test.cpp`
- Modify: `apps/ble_only/main/boot_press.hpp`
- Modify: `apps/ble_only/main/boot_press.cpp`
- Modify: `host-tests/boot_press_test.cpp`
- Modify: `host-tests/CMakeLists.txt`

**Interfaces:**
- Consumes: selected/connected identities from Tasks 1–2 and monotonic milliseconds.
- Produces: `BleSetupState::start(bool has_selected, uint32_t now_ms)`, `tick(uint32_t)`, `request_portal()`, `request_close()`, `request_connect(BlePeerAddress)`, `request_disconnect()`, `on_connected(BlePeerAddress, uint32_t)`, and `on_failed(BlePeerAddress, BleAttemptError)`.
- Produces: `BleSetupSnapshot` containing portal desired/active, attempt state/error, connected/selected identities, and `wifi_shutdown_due_ms`.
- Changes: `BootAction::ClearBond` to `BootAction::OpenPortal`; 5,000 ms still fires once while held.

- [ ] **Step 1: Write failing setup-state and BOOT tests**

Assert no selected peer requests portal immediately; a selected peer gets exactly 10,000 ms before fallback; manual request opens immediately; close is accepted only with a valid selected or connected peer; selecting B while A is connected emits disconnect-A then connect-B; B failure leaves portal open with no selected fallback; disconnect clears selection but not history intent; success schedules Wi-Fi shutdown at +3,000 ms; a disconnect during that delay cancels shutdown and restores the portal; and BOOT 5 seconds emits `OpenPortal` once without any clear-bond action.

- [ ] **Step 2: Run focused tests and verify RED**

Run: `cmake --build host-tests/build --target ble_setup_state_test boot_press_test`

Expected: setup-state compilation fails and the old BOOT expectation fails.

- [ ] **Step 3: Implement the portable state machine and BOOT rename**

Use explicit enum states `Idle`, `ScanningSelected`, `Portal`, `SwitchDisconnect`, `Connecting`, `Connected`, `SuccessDelay`, and `Failed`; expose decisions as values for the runtime controller rather than calling ESP-IDF from the model.

- [ ] **Step 4: Run the host suite**

Run: `cmake --build host-tests/build && ctest --test-dir host-tests/build --output-on-failure`

Expected: 17/17 tests pass.

- [ ] **Step 5: Commit**

```bash
git add apps/ble_only/main/ble_setup_state.* apps/ble_only/main/boot_press.* host-tests/ble_setup_state_test.cpp host-tests/boot_press_test.cpp host-tests/CMakeLists.txt
git commit -m "feat: model BLE setup lifecycle"
```

### Task 4: Bounded portal JSON, form parsing, DNS responses, and page

**Files:**
- Create: `apps/ble_only/main/portal_codec.hpp`
- Create: `apps/ble_only/main/portal_codec.cpp`
- Create: `apps/ble_only/main/captive_dns_codec.hpp`
- Create: `apps/ble_only/main/captive_dns_codec.cpp`
- Create: `apps/ble_only/main/portal_page.hpp`
- Create: `host-tests/portal_codec_test.cpp`
- Create: `host-tests/captive_dns_codec_test.cpp`
- Modify: `host-tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `BleNearbySnapshot`, saved history snapshot, and `BleSetupSnapshot` from Tasks 1–3.
- Produces: `write_portal_json(const PortalView&, std::span<char>) -> std::optional<size_t>` with valid JSON or failure and no partial response.
- Produces: `parse_peer_form(std::string_view) -> std::optional<BlePeerAddress>` accepting only exact `address=AA%3ABB...&type=N` bounded form data.
- Produces: `build_captive_dns_response(std::span<const uint8_t> request, std::span<uint8_t> response, std::array<uint8_t,4> ipv4) -> std::optional<size_t>`.
- Produces: `kPortalPage` as one embedded, dependency-free UTF-8 page polling `/api/state` every 1,000 ms.

- [ ] **Step 1: Write failing codec tests**

For JSON, assert both required sections, connected-first/saved/RSSI fields, escaping of quotes, backslashes, controls and invalid UTF-8 replacement, and clean failure on insufficient output. For forms, assert canonical valid input and reject duplicates, unknown keys, bad hex, bad type, oversized and truncated bodies. For DNS, assert transaction ID/question preservation and one A answer to `192.168.4.1`; reject truncation, compression-pointer loops, non-query packets, oversized labels, non-A/IN, and insufficient response buffers.

- [ ] **Step 2: Run focused tests and verify RED**

Run: `cmake --build host-tests/build --target portal_codec_test captive_dns_codec_test`

Expected: FAIL because the codec headers do not exist.

- [ ] **Step 3: Implement bounded codecs**

Use cursor writers over caller-provided spans. DNS supports one standard question and may safely decline unsupported packets; it must never dereference a compression pointer recursively.

- [ ] **Step 4: Implement the embedded page**

Render two compact sections, English status text, connected green dot, saved markers, per-row busy state, Connect/Disconnect/Forget buttons, a `Close setup` action, confirmation before forget, empty/error states, and a responsive single-column layout without external assets.

- [ ] **Step 5: Run the host suite**

Run: `cmake --build host-tests/build && ctest --test-dir host-tests/build --output-on-failure`

Expected: 19/19 tests pass.

- [ ] **Step 6: Commit**

```bash
git add apps/ble_only/main/portal_codec.* apps/ble_only/main/captive_dns_codec.* apps/ble_only/main/portal_page.hpp host-tests/portal_codec_test.cpp host-tests/captive_dns_codec_test.cpp host-tests/CMakeLists.txt
git commit -m "feat: add captive portal codecs and page"
```

### Task 5: Managed BLE selection, identity resolution, and history integration

**Files:**
- Create: `apps/ble_only/main/ble_device_manager.hpp`
- Create: `apps/ble_only/main/ble_device_manager.cpp`
- Create: `apps/input/main/ble_selection_delegate.hpp`
- Create: `host-tests/ble_device_manager_test.cpp`
- Modify: `apps/input/main/ble_hogp_client.hpp`
- Modify: `apps/input/main/ble_hogp_client.cpp`
- Modify: `apps/ble_only/main/CMakeLists.txt`
- Modify: `apps/ble_only/sdkconfig.defaults`
- Modify: `host-tests/CMakeLists.txt`

**Interfaces:**
- Consumes: catalog, history/store, and setup state from Tasks 1–3.
- Produces: `BleDeviceManager::observe(...)`, `request_connect(BlePeerAddress)`, `request_disconnect()`, `request_forget(BlePeerAddress)`, `request_portal()`, `service(uint32_t now_ms)`, and `snapshot(uint32_t) -> PortalView`.
- Produces: fixed-capacity `BleManagerCommand` queue and `BleManagerDecision` values consumed only by the BLE worker.
- Produces: shared abstract `BleSelectionDelegate` callbacks for advertisements, attempt start/failure, and validated connection identity; `BleDeviceManager` implements it without making `apps/input` link portal code.
- Extends: `BleHogpClient::set_selection_delegate(BleSelectionDelegate*)` and `enqueue_control(BleClientCommand)`; null delegate preserves `apps/input` behavior, while a non-null delegate disables first-HID auto-selection and receives observations/results.

- [ ] **Step 1: Write the failing manager test**

Using a fake persistence sink and BLE decision consumer, assert that all discoverable advertisements reach the catalog; no unselected peer auto-connects; an explicit observed address connects; selected identity reconnect accepts a matching public/random identity and an RPA resolved to it; a different RPA is rejected; successful HID validation records the `peer_id_addr`; switching/disconnect/forget decisions follow the spec; and persistence failure is reported but does not undo a valid connection.

- [ ] **Step 2: Run the focused test and verify RED**

Run: `cmake --build host-tests/build --target ble_device_manager_test`

Expected: FAIL because `ble_device_manager.hpp` does not exist.

- [ ] **Step 3: Implement the portable manager decisions**

HTTP/BOOT producers enqueue bounded manager commands. BLE callbacks publish bounded observations/results through the delegate. Manager mutations and snapshots use one explicit short lock; neither HTTP handlers nor CPU1 code receives mutable containers.

- [ ] **Step 4: Integrate managed mode into `BleHogpClient`**

Parse every `BLE_GAP_EVENT_DISC` and scan response, call `ble_gap_rpa_resolve()` for random RPAs, and let the manager decide whether to open. After a valid HOGP open, obtain `ble_gap_conn_desc.peer_id_addr`; only then record success. Use `ble_store_util_delete_peer()` for a forgotten stable identity. Keep the existing automatic UUID-`0x1812` path byte-for-byte equivalent when no manager is attached.

- [ ] **Step 5: Configure eight bonds and build both BLE consumers**

Set `CONFIG_BT_NIMBLE_MAX_BONDS=8` only in `apps/ble_only/sdkconfig.defaults`.

Run: `export IDF_TOOLS_PATH="$PWD/.toolchains/idf-tools"; . .toolchains/esp-idf/export.sh; idf.py -C apps/input build; idf.py -C apps/ble_only build`

Expected: both builds pass; `apps/input` size/behavior has no portal dependencies; BLE-only still fits 1 MiB.

- [ ] **Step 6: Run the host suite**

Run: `cmake --build host-tests/build && ctest --test-dir host-tests/build --output-on-failure`

Expected: 20/20 tests pass.

- [ ] **Step 7: Commit**

```bash
git add apps/ble_only/main/ble_device_manager.* apps/input/main/ble_selection_delegate.hpp apps/input/main/ble_hogp_client.* apps/ble_only/main/CMakeLists.txt apps/ble_only/sdkconfig.defaults host-tests/ble_device_manager_test.cpp host-tests/CMakeLists.txt
git commit -m "feat: select BLE mouse explicitly"
```

### Task 6: CPU0 DNS and Wi-Fi captive portal runtime

**Files:**
- Create: `apps/ble_only/main/captive_dns.hpp`
- Create: `apps/ble_only/main/captive_dns.cpp`
- Create: `apps/ble_only/main/setup_portal.hpp`
- Create: `apps/ble_only/main/setup_portal.cpp`
- Create: `apps/ble_only/main/portal_lifecycle.hpp`
- Create: `apps/ble_only/main/portal_lifecycle.cpp`
- Create: `host-tests/portal_lifecycle_test.cpp`
- Modify: `apps/ble_only/main/CMakeLists.txt`
- Modify: `apps/ble_only/sdkconfig.defaults`
- Modify: `host-tests/CMakeLists.txt`

**Interfaces:**
- Consumes: portal page/codecs and `BleDeviceManager` from Tasks 4–5.
- Produces: `PortalLifecycle` acquire/unwind decisions for netif, Wi-Fi init/start, HTTP, and DNS resources.
- Produces: `CaptiveDns::start(std::array<uint8_t,4>, BaseType_t core) -> esp_err_t` and `CaptiveDns::stop() -> void` with a static CPU0 task and bounded 512-byte request/response buffers.
- Produces: `SetupPortal::start() -> esp_err_t`, `SetupPortal::stop() -> void`, and `SetupPortal::active() const -> bool`; HTTP handlers call only manager snapshot/enqueue APIs.

- [ ] **Step 1: Write the failing lifecycle test**

Assert forward acquisition order, reverse cleanup after failure at every acquisition step, idempotent stop, restart after a partial failure, and no release of resources never acquired.

- [ ] **Step 2: Run the focused test and verify RED**

Run: `cmake --build host-tests/build --target portal_lifecycle_test`

Expected: FAIL because `portal_lifecycle.hpp` does not exist.

- [ ] **Step 3: Implement lifecycle model and DNS owner**

Bind UDP port 53 to the AP interface/address, answer with Task 4's codec, and wake/close the socket during stop. Create only `xTaskCreateStaticPinnedToCore(..., 0)` application tasks.

- [ ] **Step 4: Implement AP and HTTP owner**

Initialize `esp_netif`/default event loop idempotently, create the default AP netif, configure open AP `MouseBridge-Setup-XXXX`, `max_connection=4`, and no upstream interface, then start HTTP with `httpd_config_t.core_id=0` and `uri_match_fn=httpd_uri_match_wildcard`. Register `/`, all five API routes, Apple `/hotspot-detect.html`, Android `/generate_204`, Windows `/connecttest.txt` and `/ncsi.txt`, Chromium `/canonical.html`, plus `/*` redirect. Limit POST bodies to 96 bytes and JSON output to a fixed 8 KiB buffer.

- [ ] **Step 5: Pin Wi-Fi and build BLE-only**

Add component requirements `esp_wifi`, `esp_netif`, `esp_http_server`, and `lwip`; set `CONFIG_ESP_WIFI_TASK_PINNED_TO_CORE_0=y`.

Run: `export IDF_TOOLS_PATH="$PWD/.toolchains/idf-tools"; . .toolchains/esp-idf/export.sh; idf.py -C apps/ble_only fullclean build`

Expected: build passes and the binary remains below `0x100000`.

- [ ] **Step 6: Run the host suite and task audit**

Run: `cmake --build host-tests/build && ctest --test-dir host-tests/build --output-on-failure && ! rg -n "xTaskCreateStatic\\(" apps/ble_only apps/input/main apps/output/main && git diff --check`

Expected: 21/21 tests pass, no unpinned application task creation, clean diff.

- [ ] **Step 7: Commit**

```bash
git add apps/ble_only/main/captive_dns.* apps/ble_only/main/setup_portal.* apps/ble_only/main/portal_lifecycle.* apps/ble_only/main/CMakeLists.txt apps/ble_only/sdkconfig.defaults host-tests/portal_lifecycle_test.cpp host-tests/CMakeLists.txt
git commit -m "feat: serve BLE captive portal"
```

### Task 7: BOOT, startup, shutdown, LED, and diagnostics integration

**Files:**
- Modify: `apps/ble_only/main/boot_control.hpp`
- Modify: `apps/ble_only/main/boot_control.cpp`
- Modify: `apps/ble_only/main/app_main.cpp`
- Modify: `apps/ble_only/main/status_led_model.hpp`
- Modify: `apps/ble_only/main/status_led_model.cpp`
- Modify: `host-tests/status_led_model_test.cpp`

**Interfaces:**
- Consumes: manager/setup state and `SetupPortal` from Tasks 3, 5, and 6.
- Changes: `BootControl` receives `BleDeviceManager&` and enqueues `request_portal()` on `OpenPortal`.
- Changes: app startup creates store/manager/portal in static lifetime order, starts USB and LED first, then BLE/manager/portal controller; portal failure is logged without aborting USB/BLE.
- Changes: `StatusLedModel::update(bool connected, bool portal_active, bool mods_enabled, size_t profile, uint32_t now_ms)` and `StatusLed` receives the manager snapshot; disconnected, connecting, or portal-active is yellow even when a mouse is still connected, while existing green/profile states are otherwise unchanged.

- [ ] **Step 1: Extend the LED model regression test**

Assert portal/disconnected/connecting inputs remain the same limited yellow blink and that connected pass-through/profile outputs are unchanged.

- [ ] **Step 2: Implement runtime orchestration**

Have one static CPU0 setup-controller task call `BleDeviceManager::service()`, submit returned decisions through `BleHogpClient::enqueue_control()`, and start/stop `SetupPortal`. `BleHogpClient::start()` initializes NVS and asks the attached manager to load history before NimBLE sync can start scanning. Startup with no selected history opens immediately; selected reconnect gets 10 seconds; success stops Wi-Fi after 3 seconds; manual BOOT opens without clearing history.

- [ ] **Step 3: Extend diagnostics**

Add portal active, saved count, nearby count, and attempt state to the 5-second `MB_DIAG` snapshot without logging names, addresses, or request contents.

- [ ] **Step 4: Run complete verification**

Run: `cmake -S host-tests -B host-tests/build && cmake --build host-tests/build && ctest --test-dir host-tests/build --output-on-failure`

Run: `export IDF_TOOLS_PATH="$PWD/.toolchains/idf-tools"; . .toolchains/esp-idf/export.sh; idf.py -C apps/input build; idf.py -C apps/ble_only fullclean build`

Expected: 21/21 host tests and both ESP-IDF builds pass; BLE-only fits 1 MiB.

- [ ] **Step 5: Commit**

```bash
git add apps/ble_only/main/boot_control.* apps/ble_only/main/app_main.cpp apps/ble_only/main/status_led_model.* host-tests/status_led_model_test.cpp
git commit -m "feat: integrate BLE setup portal"
```

### Task 8: Bring-up documentation, hardware validation, and board flash

**Files:**
- Modify: `README.md`
- Modify: `docs/bringup.md`
- Modify: `docs/validation.md`

**Interfaces:**
- Consumes: final SSID, captive routes, UI actions, timings, LED behavior, image size, and tests from Tasks 1–7.
- Produces: operator steps and an explicit hardware validation matrix; no hardware row becomes PASS without observation.

- [ ] **Step 1: Document portal operation**

Document first boot, joining `MouseBridge-Setup-XXXX`, automatic captive opening plus `192.168.4.1` fallback, both UI sections/actions, 5-second BOOT behavior, 10-second fallback, 3-second shutdown, open/local-only AP scope, and Wi-Fi-off latency guarantee.

- [ ] **Step 2: Add hardware validation rows**

Add `NOT RUN` rows for macOS/iOS/Android/Windows captive detection, Freewolf X7 visibility without advertised `0x1812`, select/validate/store/shutdown, power-cycle RPA reconnect, A→B failure, disconnect/forget, invalid HID rejection, LED transitions, Wi-Fi disappearance, and post-setup HID latency/function.

- [ ] **Step 3: Run final software gates**

Run: `cmake -S host-tests -B host-tests/build && cmake --build host-tests/build && ctest --test-dir host-tests/build --output-on-failure`

Run: `export IDF_TOOLS_PATH="$PWD/.toolchains/idf-tools"; . .toolchains/esp-idf/export.sh; idf.py -C apps/ble_only fullclean build`

Run: `git diff --check && git status --short && ! rg -n "xTaskCreateStatic\\(" apps/ble_only apps/input/main apps/output/main`

Expected: 21/21 tests pass, build passes under 1 MiB, no unpinned application task, and only intended documentation changes remain.

- [ ] **Step 4: Commit documentation**

```bash
git add README.md docs/bringup.md docs/validation.md
git commit -m "docs: add BLE portal bring-up"
```

- [ ] **Step 5: Flash the connected board and observe first boot**

Resolve the Espressif `/dev/cu.usbmodem*` port, then run:

```bash
export IDF_TOOLS_PATH="$PWD/.toolchains/idf-tools"
. .toolchains/esp-idf/export.sh
idf.py -C apps/ble_only -p /dev/cu.usbmodemXXXX flash
```

Expected: esptool verifies all hashes, the board re-enumerates as `Adaptive HID Mouse Bridge`, yellow scan/setup LED appears, and `MouseBridge-Setup-XXXX` becomes visible. Mark later hardware checks only from actual observations.

