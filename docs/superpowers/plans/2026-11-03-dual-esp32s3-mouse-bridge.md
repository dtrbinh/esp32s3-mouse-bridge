# Dual ESP32-S3 Mouse Bridge Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build two ESP32-S3 N4R2 firmwares that bridge a wired, 2.4 GHz receiver, or BLE HID mouse to a 1 ms USB HID output while applying deterministic movement profiles.

**Architecture:** The input firmware owns USB Host and BLE Central, normalizes reports, runs a non-blocking profile engine, and publishes fixed-size CRC-protected packets over SPI. The output firmware consumes those packets, applies link failsafes and emergency bypass, and exposes a TinyUSB relative mouse to the PC. Portable shared logic is verified with host-native CTest tests before hardware integration.

**Tech Stack:** ESP-IDF v6.0.3, C++20 for portable components, C17 for ESP-IDF/TinyUSB integration, `espressif/usb_host_hid` 1.2.1, NimBLE, TinyUSB, FreeRTOS, SPI master/slave drivers, CMake/CTest.

**Spec:** `docs/superpowers/specs/2026-11-03-dual-esp32s3-mouse-bridge-design.md`

## Global Constraints

- Target hardware is two Waveshare ESP32-S3-Zero N4R2 boards; flash is 4 MB and OPI PSRAM is 2 MB.
- USB Host uses GPIO19 D- and GPIO20 D+ through an external receptacle with protected 5 V VBUS.
- Inter-board link uses GPIO4 SCLK, GPIO5 MOSI, GPIO6 MISO, GPIO7 CS, GPIO8 READY, and common GND.
- USB input has priority over BLE; every source transition sends a zero-motion release-all boundary report.
- USB output descriptor uses a 1 ms interrupt interval; BLE is best-effort and must not be represented as 1 kHz input.
- Report-path callbacks use no blocking sleeps, unbounded loops, logging, or dynamic allocation.
- SPI packet timeout clears relative movement after 5 ms and releases all buttons after 20 ms.
- BOOT on the output board is an emergency bypass that disables generated movement but preserves physical pass-through.
- Do not connect the input board's onboard USB-C to a PC while its external Host receptacle is active.

## Review Focus

- Sequence wraparound from `UINT32_MAX` to zero must remain newer than the preceding packet; pin this in Task 2 protocol tests.
- A report descriptor with report IDs and signed non-byte-aligned X/Y fields must decode without corrupting buttons; pin this in Task 4 parser tests.
- USB detachment while buttons are held must produce exactly one release boundary before BLE resumes; pin this in Task 5 arbiter tests.
- SPI corruption or silence must never leave a button held on the PC; pin CRC and timeout cases in Tasks 2 and 8.
- Profile delay values of zero and elapsed-time jumps across several steps must terminate deterministically without an infinite loop; pin this in Task 3 engine tests.

---

### Task 1: Repository Skeleton and Repeatable Builds

**Files:**
- Create: `CMakeLists.txt`
- Create: `apps/input/CMakeLists.txt`
- Create: `apps/input/main/CMakeLists.txt`
- Create: `apps/input/main/app_main.cpp`
- Create: `apps/input/sdkconfig.defaults`
- Create: `apps/output/CMakeLists.txt`
- Create: `apps/output/main/CMakeLists.txt`
- Create: `apps/output/main/app_main.cpp`
- Create: `apps/output/sdkconfig.defaults`
- Create: `host-tests/CMakeLists.txt`
- Create: `host-tests/smoke_test.cpp`
- Create: `README.md`
- Create: `.gitignore`

**Interfaces:**
- Consumes: ESP-IDF v6.0.3 environment exported by the developer.
- Produces: `cmake -S host-tests -B build/host && ctest --test-dir build/host`; `idf.py -C apps/input build`; `idf.py -C apps/output build`.

- [ ] **Step 1: Add a host smoke test that asserts the test runner executes one passing case**

- [ ] **Step 2: Configure and run the host test**

Run: `cmake -S host-tests -B build/host && cmake --build build/host && ctest --test-dir build/host --output-on-failure`

Expected: one test passes.

- [ ] **Step 3: Add minimal ESP-IDF applications and defaults for ESP32-S3 N4R2**

Pin the chip target to `esp32s3`, flash size to 4 MB, OPI PSRAM to 2 MB, USB Host in input only, and TinyUSB Device in output only. Add `espressif/usb_host_hid: "1.2.1"` to `apps/input/main/idf_component.yml`.

- [ ] **Step 4: Build both firmware skeletons**

Run: `idf.py -C apps/input set-target esp32s3 && idf.py -C apps/input build && idf.py -C apps/output set-target esp32s3 && idf.py -C apps/output build`

Expected: both projects produce `.bin`, `.elf`, and flash-argument files without missing-component errors.

- [ ] **Step 5: Document toolchain setup, board roles, power warning, and build/flash commands in `README.md`**

- [ ] **Step 6: Commit**

```bash
git add esp32s3-usb-hid
git commit -m "chore: scaffold dual esp32s3 mouse bridge"
```

### Task 2: Normalized Mouse Type and Wire Protocol

**Files:**
- Create: `components/mouse_bridge/CMakeLists.txt`
- Create: `components/mouse_bridge/include/mouse_bridge/mouse_report.hpp`
- Create: `components/mouse_bridge/include/mouse_bridge/wire_protocol.hpp`
- Create: `components/mouse_bridge/src/wire_protocol.cpp`
- Create: `host-tests/wire_protocol_test.cpp`
- Modify: `host-tests/CMakeLists.txt`

**Interfaces:**
- Consumes: fixed-width integer types only.
- Produces: `enum class InputSource : uint8_t`; `struct MouseReport`; `struct WirePacket`; `WirePacket encode_packet(const MouseReport&, uint32_t sequence)`; `DecodeResult decode_packet(std::span<const uint8_t>)`; `bool sequence_is_newer(uint32_t candidate, uint32_t current)`.

- [ ] **Step 1: Write failing tests for packet size, little-endian round trip, CRC rejection, unsupported version, signed axis limits, and sequence wraparound**

- [ ] **Step 2: Run the focused tests and verify failure**

Run: `cmake --build build/host && ctest --test-dir build/host -R wire_protocol --output-on-failure`

Expected: tests fail because protocol symbols are missing.

- [ ] **Step 3: Implement packed version-1 packet encoding, CRC-16/CCITT-FALSE, decoding, and wrap-safe sequence comparison**

Keep serialization explicit; do not transmit a compiler-native struct with padding.

- [ ] **Step 4: Run all host tests**

Run: `cmake --build build/host && ctest --test-dir build/host --output-on-failure`

Expected: all tests pass.

- [ ] **Step 5: Commit**

```bash
git add esp32s3-usb-hid/components/mouse_bridge esp32s3-usb-hid/host-tests
git commit -m "feat: add versioned mouse bridge protocol"
```

### Task 3: Deterministic Profile Engine

**Files:**
- Create: `components/profile_engine/CMakeLists.txt`
- Create: `components/profile_engine/include/profile_engine/profile.hpp`
- Create: `components/profile_engine/include/profile_engine/profile_engine.hpp`
- Create: `components/profile_engine/src/profile_engine.cpp`
- Create: `components/profile_engine/src/default_profiles.cpp`
- Create: `host-tests/profile_engine_test.cpp`
- Modify: `host-tests/CMakeLists.txt`

**Interfaces:**
- Consumes: physical `MouseReport`, monotonic `uint64_t now_us`, enable/bypass state, selected profile index `0..13`.
- Produces: `ProfileEngine::select(size_t)`; `ProfileEngine::set_enabled(bool)`; `MouseReport ProfileEngine::process(const MouseReport&, uint64_t now_us)`; `ProfileEngine::reset()`; `std::span<const Profile> default_profiles()`.

- [ ] **Step 1: Write failing tests for activation buttons, immediate stop on release, crouch gain, real-time phase changes, elapsed jumps, zero delay, signed rounding, saturation, mod6 X gain, and bypass**

- [ ] **Step 2: Run focused tests and verify failure**

Run: `cmake --build build/host && ctest --test-dir build/host -R profile_engine --output-on-failure`

Expected: tests fail because the engine is absent.

- [ ] **Step 3: Encode the 14 supplied Lua profiles as immutable data**

Preserve all 15 X/Y steps and profile-specific stop thresholds; replace fake `I += 40` timing with monotonic elapsed milliseconds and use the profile's per-step delays consistently.

- [ ] **Step 4: Implement the bounded timestamp-driven state machine**

Cap catch-up work per call to the number of profile steps that can exist within the elapsed duration and report overflow instead of looping indefinitely.

- [ ] **Step 5: Run all host tests**

Run: `cmake --build build/host && ctest --test-dir build/host --output-on-failure`

Expected: all tests pass.

- [ ] **Step 6: Commit**

```bash
git add esp32s3-usb-hid/components/profile_engine esp32s3-usb-hid/host-tests
git commit -m "feat: port recoil profiles to nonblocking engine"
```

### Task 4: USB and BLE HID Report Descriptor Parser

**Files:**
- Create: `components/hid_parser/CMakeLists.txt`
- Create: `components/hid_parser/include/hid_parser/report_descriptor.hpp`
- Create: `components/hid_parser/include/hid_parser/mouse_decoder.hpp`
- Create: `components/hid_parser/src/report_descriptor.cpp`
- Create: `components/hid_parser/src/mouse_decoder.cpp`
- Create: `host-tests/hid_parser_test.cpp`
- Create: `host-tests/fixtures/boot_mouse_descriptor.hpp`
- Create: `host-tests/fixtures/composite_report_id_mouse_descriptor.hpp`
- Modify: `host-tests/CMakeLists.txt`

**Interfaces:**
- Consumes: HID report descriptor bytes and input report bytes.
- Produces: `ParseResult parse_mouse_descriptor(std::span<const uint8_t>)`; `DecodeStatus decode_mouse_report(const MouseLayout&, std::span<const uint8_t>, MouseReport&)`.

- [ ] **Step 1: Write failing tests for boot mouse, report ID, 16 buttons, horizontal wheel, signed 8/12/16-bit X/Y, truncated reports, constant fields, and unsupported absolute axes**

- [ ] **Step 2: Run focused tests and verify failure**

Run: `cmake --build build/host && ctest --test-dir build/host -R hid_parser --output-on-failure`

Expected: tests fail because parser symbols are missing.

- [ ] **Step 3: Implement HID short-item parsing with global stack, local usages, collections, report IDs, bit offsets, sign extension, and bounds checks**

Reject long items, absolute pointer axes, layouts exceeding fixed field capacity, and descriptors without a relative pointer application collection.

- [ ] **Step 4: Run all host tests**

Run: `cmake --build build/host && ctest --test-dir build/host --output-on-failure`

Expected: all tests pass.

- [ ] **Step 5: Commit**

```bash
git add esp32s3-usb-hid/components/hid_parser esp32s3-usb-hid/host-tests
git commit -m "feat: parse generic relative mouse hid reports"
```

### Task 5: Input Source Arbitration and USB Host Adapter

**Files:**
- Create: `apps/input/main/source_arbiter.hpp`
- Create: `apps/input/main/source_arbiter.cpp`
- Create: `apps/input/main/usb_mouse_host.hpp`
- Create: `apps/input/main/usb_mouse_host.cpp`
- Create: `host-tests/source_arbiter_test.cpp`
- Modify: `apps/input/main/CMakeLists.txt`
- Modify: `apps/input/main/app_main.cpp`
- Modify: `host-tests/CMakeLists.txt`

**Interfaces:**
- Consumes: decoded reports/events tagged `USB` or `BLE`, monotonic timestamp, connect/disconnect status.
- Produces: `SourceArbiter::on_connected(InputSource, uint64_t)`; `on_report(InputSource, const MouseReport&, uint64_t)`; `on_disconnected(InputSource, uint64_t)`; `std::optional<MouseReport> poll(uint64_t)`; USB callback forwarding through a bounded static queue.

- [ ] **Step 1: Write failing arbiter tests for USB priority, BLE fallback debounce, stale BLE suppression, one release boundary on detach, and rapid mode switching**

- [ ] **Step 2: Implement the portable source arbiter and pass focused host tests**

Run: `cmake --build build/host && ctest --test-dir build/host -R source_arbiter --output-on-failure`

Expected: all arbiter tests pass.

- [ ] **Step 3: Implement `UsbMouseHost` with ESP-IDF USB Host and `usb_host_hid`**

Claim mouse interfaces, fetch the report descriptor, compile a `MouseLayout`, submit interrupt IN transfers continuously, timestamp callbacks, and reject unsupported layouts with a rate-limited diagnostic.

- [ ] **Step 4: Build input firmware**

Run: `idf.py -C apps/input build`

Expected: input firmware builds with USB Host enabled and no TinyUSB Device ownership conflict.

- [ ] **Step 5: Commit**

```bash
git add esp32s3-usb-hid/apps/input esp32s3-usb-hid/host-tests
git commit -m "feat: receive and arbitrate usb mouse input"
```

### Task 6: BLE HOGP Central

**Files:**
- Create: `apps/input/main/ble_hogp_client.hpp`
- Create: `apps/input/main/ble_hogp_client.cpp`
- Create: `apps/input/main/bond_control.hpp`
- Create: `apps/input/main/bond_control.cpp`
- Modify: `apps/input/main/CMakeLists.txt`
- Modify: `apps/input/main/app_main.cpp`
- Modify: `apps/input/sdkconfig.defaults`

**Interfaces:**
- Consumes: NimBLE GAP/GATT events, HID service `0x1812`, report map, input notifications, input-board BOOT long press.
- Produces: decoded BLE `MouseReport` events into `SourceArbiter`; one persisted bond; `BleHogpClient::start()`; `suspend_forwarding()`; `resume_or_scan()`; `clear_bond()`.

- [ ] **Step 1: Implement scan filtering, secure pairing, one-peer bonding, service discovery, report-map parsing, and input-report subscriptions**

Request the lowest legal connection interval but accept the interval negotiated by the mouse.

- [ ] **Step 2: Implement USB-priority suspension and reconnect behavior**

Do not delete the bond when USB becomes active; suppress BLE reports until USB disconnects and the arbiter debounce expires.

- [ ] **Step 3: Implement BOOT long-press bond clearing without blocking the BLE host task**

- [ ] **Step 4: Build input firmware and inspect memory use**

Run: `idf.py -C apps/input build && idf.py -C apps/input size-components`

Expected: firmware fits 4 MB flash and internal RAM; callback queues and USB DMA buffers are not allocated in PSRAM.

- [ ] **Step 5: Commit**

```bash
git add esp32s3-usb-hid/apps/input
git commit -m "feat: add ble hid mouse input"
```

### Task 7: SPI Link for Input and Output Boards

**Files:**
- Create: `components/spi_link/CMakeLists.txt`
- Create: `components/spi_link/include/spi_link/link_config.hpp`
- Create: `apps/input/main/spi_packet_producer.hpp`
- Create: `apps/input/main/spi_packet_producer.cpp`
- Create: `apps/output/main/spi_packet_consumer.hpp`
- Create: `apps/output/main/spi_packet_consumer.cpp`
- Modify: `apps/input/main/CMakeLists.txt`
- Modify: `apps/input/main/app_main.cpp`
- Modify: `apps/output/main/CMakeLists.txt`
- Modify: `apps/output/main/app_main.cpp`

**Interfaces:**
- Consumes: encoded fixed-size `WirePacket`; GPIO4–8 assignments; 10 MHz SPI clock.
- Produces: input-board producer with bounded latest-report mailbox and READY signaling; output-board consumer callback `on_valid_packet(const MouseReport&, uint32_t sequence, uint64_t rx_us)`; link counters.

- [ ] **Step 1: Implement an input-board SPI master producer that replaces stale unsent motion by saturating accumulation and retains newest buttons**

- [ ] **Step 2: Implement an output-board SPI slave consumer with prequeued DMA-capable transactions**

READY signals that a complete packet is available; no ISR may decode CRC or log.

- [ ] **Step 3: Add sequence-gap, CRC-failure, queue-overrun, and maximum-link-latency counters**

- [ ] **Step 4: Build both firmwares**

Run: `idf.py -C apps/input build && idf.py -C apps/output build`

Expected: both builds pass; map files show SPI DMA buffers in internal DMA-capable memory.

- [ ] **Step 5: Commit**

```bash
git add esp32s3-usb-hid/components/spi_link esp32s3-usb-hid/apps
git commit -m "feat: bridge mouse reports over spi"
```

### Task 8: Output USB HID, Failsafe, and Bypass

**Files:**
- Create: `apps/output/main/output_state.hpp`
- Create: `apps/output/main/output_state.cpp`
- Create: `apps/output/main/usb_mouse_device.hpp`
- Create: `apps/output/main/usb_mouse_device.cpp`
- Create: `apps/output/main/tusb_descriptors.c`
- Create: `host-tests/output_state_test.cpp`
- Modify: `apps/output/main/CMakeLists.txt`
- Modify: `apps/output/main/app_main.cpp`
- Modify: `apps/output/sdkconfig.defaults`
- Modify: `host-tests/CMakeLists.txt`

**Interfaces:**
- Consumes: valid SPI reports and receive timestamps; BOOT GPIO0 state; USB suspend/resume events.
- Produces: `OutputState::on_packet(...)`; `OutputFrame OutputState::tick(uint64_t now_us, bool bypass)`; TinyUSB mouse reports every available 1 ms frame; optional CDC profile-selection command.

- [ ] **Step 1: Write failing tests for normal forwarding, 5 ms motion clear, 20 ms button release, bad-packet silence, bypass removal of generated motion, USB resume release, and accumulator saturation**

- [ ] **Step 2: Implement `OutputState` and pass focused host tests**

Run: `cmake --build build/host && ctest --test-dir build/host -R output_state --output-on-failure`

Expected: all output-state tests pass.

- [ ] **Step 3: Add a boot-compatible relative mouse report descriptor with 16 buttons, X/Y, vertical wheel, horizontal pan, and endpoint interval 1 ms**

- [ ] **Step 4: Implement TinyUSB scheduling, suspend/resume release behavior, BOOT debounce, and optional CDC profile command behind a disabled-by-default Kconfig flag**

- [ ] **Step 5: Build output firmware and inspect the generated descriptor**

Run: `idf.py -C apps/output build`

Expected: output firmware builds; descriptor constants show `bInterval = 1` and report length matches the transmitted report.

- [ ] **Step 6: Commit**

```bash
git add esp32s3-usb-hid/apps/output esp32s3-usb-hid/host-tests
git commit -m "feat: expose failsafe 1khz usb hid mouse"
```

### Task 9: End-to-End Assembly, Diagnostics, and Hardware Validation

**Files:**
- Create: `docs/wiring.md`
- Create: `docs/bringup.md`
- Create: `docs/validation.md`
- Create: `tools/decode_diagnostics.py`
- Modify: `apps/input/main/app_main.cpp`
- Modify: `apps/output/main/app_main.cpp`
- Modify: `README.md`

**Interfaces:**
- Consumes: all firmware components and physical hardware specified by the design.
- Produces: reproducible bring-up sequence, wiring checklist, diagnostics decoder, recorded pass/fail validation table.

- [ ] **Step 1: Integrate input pipeline: USB/BLE → arbiter → profile engine → SPI producer**

- [ ] **Step 2: Integrate output pipeline: SPI consumer → output state → TinyUSB mouse**

- [ ] **Step 3: Add rate-limited diagnostics snapshots and a host-side decoder**

Include source, negotiated USB interval, BLE connection interval, packets received, sequence gaps, CRC failures, overruns, timeouts, and maximum measured link latency.

- [ ] **Step 4: Run host tests and both firmware builds**

Run: `cmake --build build/host && ctest --test-dir build/host --output-on-failure && idf.py -C apps/input build && idf.py -C apps/output build`

Expected: all tests and builds pass.

- [ ] **Step 5: Execute hardware bring-up in safe order**

First flash each board separately, verify release-all output, join grounds, attach SPI, power the input Host VBUS from the protected external rail, then connect the output board to the PC. Never connect input-board USB-C while Host VBUS is active.

- [ ] **Step 6: Validate all modes and failures from the spec**

Record results for 125 Hz wired, 1 kHz wired, 1 kHz receiver, BLE pairing/reconnect/bond clear, mode switching, held-button unplug, bypass, SPI disconnect/corruption, PC reboot, suspend/resume, and the 10-minute no-drop synthetic test. Mark the 1–2 ms latency target unverified until captured with a logic analyzer.

- [ ] **Step 7: Run final static checks**

Run: `git diff --check`

Expected: no whitespace errors; documentation matches actual pins and commands.

- [ ] **Step 8: Commit**

```bash
git add esp32s3-usb-hid
git commit -m "docs: add dual-board bringup and validation"
```
