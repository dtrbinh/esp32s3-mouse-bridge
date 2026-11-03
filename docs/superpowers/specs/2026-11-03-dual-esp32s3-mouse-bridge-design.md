# Dual ESP32-S3 Mouse Bridge Design

## Intent

Build a two-board ESP32-S3-Zero N4R2 mouse bridge that accepts one tri-mode mouse over USB cable, a 2.4 GHz USB receiver, or Bluetooth Low Energy HID, applies an optional time-based movement profile, and presents a standard 1 kHz USB HID mouse to a computer.

The bridge must operate without a permanent PC helper application. A wired or 2.4 GHz input should preserve a 1 ms report cadence when the source device actually advertises and supplies reports at that interval. BLE is best-effort and cannot be advertised as 1 kHz because its connection interval is controlled by the BLE link and mouse.

## Hardware Architecture

### Input board

The first ESP32-S3-Zero N4R2 is the input processor.

- Its native USB OTG peripheral runs in Host mode.
- An external USB-A receptacle connects D- to GPIO19 and D+ to GPIO20.
- The receptacle VBUS is supplied by a protected external 5 V rail through a current-limited load switch; it is not driven directly from a GPIO.
- The onboard USB-C connector must remain disconnected while the external Host receptacle is active because it shares GPIO19/GPIO20.
- The same board runs as a BLE Central for HID over GATT (HOGP) mice.
- USB input has priority when a USB HID mouse or receiver is attached. BLE is used only while no valid USB mouse interface is active.
- The board normalizes all inputs into one internal mouse report, runs the profile engine, and transfers reports to the output board over SPI.

### Output board

The second ESP32-S3-Zero N4R2 is the PC-facing USB device.

- Its onboard USB-C connector attaches to the PC.
- TinyUSB exposes a boot-compatible relative mouse HID interface with a 1 ms interrupt interval.
- It receives normalized reports from the input board through SPI.
- If input reports time out, it emits a release-all report so buttons cannot remain stuck.
- Its BOOT button acts as a local emergency bypass input. Bypass disables generated movement while preserving physical mouse pass-through.

### Inter-board link

The prototype uses 3.3 V SPI with a dedicated READY interrupt line and common ground.

| Signal | Input board | Output board |
|---|---:|---:|
| SCLK | GPIO4 | GPIO4 |
| MOSI / input-to-output data | GPIO5 | GPIO5 |
| MISO / status and commands | GPIO6 | GPIO6 |
| CS | GPIO7 | GPIO7 |
| READY | GPIO8 | GPIO8 |
| GND | GND | GND |

The boards are powered separately during initial development: the input board and USB mouse from a protected external 5 V supply, and the output board from the PC USB port. Their grounds are joined. A later single-supply PCB requires power-path design and is outside this prototype.

## Software Architecture

Use ESP-IDF native C/C++ projects with two applications and shared components:

- `apps/input`: USB Host HID, BLE HOGP client, source arbitration, profile engine, SPI producer, health telemetry.
- `apps/output`: SPI consumer, watchdog/failsafe, BOOT bypass, TinyUSB HID device.
- `components/mouse_bridge`: versioned fixed-size wire protocol, CRC, sequence checking, normalized mouse types.
- `components/profile_engine`: deterministic non-blocking movement profile state machine.
- `components/hid_parser`: report descriptor parsing and conversion for standard relative mice and common composite receivers.

No input callback may sleep, allocate dynamically, or execute a blocking loop. USB, BLE, SPI, and profile logic communicate through bounded queues and preallocated buffers. Report timestamps use a monotonic microsecond clock.

## Normalized Report and SPI Protocol

The logical report contains:

- protocol version and packet type;
- monotonically increasing sequence number;
- source timestamp in microseconds;
- signed 16-bit relative X and Y;
- signed 8-bit vertical and horizontal wheel values;
- 16 button bits;
- source identity (`USB`, `BLE`, or `NONE`);
- flags for connection, overflow, bypass, and generated movement;
- CRC-16 over the fixed-size packet.

The output board rejects packets with a bad magic value, unsupported version, invalid CRC, or stale sequence. Relative motion is accumulated with saturation, while button state always reflects the newest valid report.

## Source Arbitration

1. A valid USB mouse interface becomes active immediately after enumeration and the first valid report.
2. Activating USB disconnects or suspends BLE report forwarding without deleting the BLE bond.
3. When USB disappears, release all buttons, wait for a short debounce period, then reconnect or resume the bonded BLE mouse.
4. A source change always emits a zero-motion release-all boundary report.
5. Only one source may feed movement at a time.

USB HID parsing must use the report descriptor rather than assuming a fixed boot-mouse byte layout. The first milestone may support boot mouse plus one report-ID composite layout, but unsupported descriptors must fail closed and log the reason instead of forwarding malformed input.

## BLE Behavior

The input board scans for the HID Service UUID `0x1812`, pairs as a BLE Central, stores one bonded mouse, reads its HID Information and Report Map characteristics, subscribes to input reports, and requests the lowest connection interval accepted by the mouse. A long press on the input board BOOT button clears the stored bond and restarts pairing.

BLE output cadence follows received notifications. The bridge may still send USB frames every millisecond, but it must not invent intermediate physical motion reports or claim that BLE input is 1 kHz.

## Profile Engine

Port the intent of the supplied Logitech Lua profiles, not its implementation defects.

- Profiles are immutable tables of 15 signed X/Y steps, per-step delays, five phase thresholds, normal X/Y gain, and crouch X/Y gain.
- The engine is a timestamp-driven state machine and never blocks a USB or BLE task.
- Movement generated during each elapsed tick is added to physical relative movement with signed saturation.
- Correct the evident `mod6_X`/`mod1_X` typo and the inconsistent addition-vs-multiplication sleep calculations.
- Do not reproduce `I += 40` as fake elapsed time; phase changes use real monotonic elapsed milliseconds.
- Preserve the 14 supplied profiles as data, including profiles that are initially unbound.

For the first release, mouse button 4 selects `mod1`, mouse button 5 selects `mod2`, matching the direct bindings in the Lua source. Profiles 3–14 remain selectable through a compile-time configuration value and a small USB CDC configuration command on the output board. Keyboard-modifier selection is not included because the keyboard is not routed through this bridge.

The engine activates only while enabled, the primary button is down, and the configured aim button (default: secondary/right) is down. It stops immediately when either activation button is released or emergency bypass is active.

## Reliability and Diagnostics

- A packet timeout of 5 ms clears relative accumulators; a timeout of 20 ms releases all buttons and marks the link disconnected.
- Both boards use task watchdogs.
- Sequence gaps, CRC failures, queue overruns, USB resets, BLE reconnects, and maximum observed input-to-output latency are counted.
- Diagnostics are rate-limited and must not run in the 1 ms report path.
- The output board exposes optional USB CDC diagnostics, disabled by default for final latency measurements.
- On startup and every reconnect, the system begins with all buttons released and zero relative movement.

## Performance Targets

- USB input and USB output descriptors support 1 ms interrupt intervals.
- SPI runs at 10 MHz or faster and transfers one fixed packet well within 1 ms.
- No dropped reports in a 10-minute synthetic 1 kHz wired-input test.
- Typical additional wired/2.4 GHz latency target: 1–2 ms; this is a target, not a guarantee until measured with a logic analyzer.
- BLE behavior is reported separately and is not included in the 1 kHz acceptance claim.

## Validation

Host-native unit tests cover protocol encoding/decoding, CRC rejection, sequence wraparound, source transitions, profile timing, signed rounding, saturation, and timeout release behavior.

Hardware validation covers:

- boot mouse at 125 Hz;
- wired gaming mouse at 1 kHz;
- 2.4 GHz receiver at 1 kHz;
- BLE HID pairing, reconnect, and bond clearing;
- hot unplug during held buttons;
- switching among the mouse's three physical modes;
- malformed or unsupported HID report descriptors;
- emergency bypass;
- SPI disconnect and corrupted packets;
- USB suspend/resume and PC reboot.

## Non-goals

- Bluetooth Classic HID is unsupported; ESP32-S3 supports BLE, not Bluetooth Classic.
- USB High Speed rates above Full Speed are unsupported.
- Video capture, object detection, keyboard pass-through, and arbitrary vendor configuration interfaces are outside this project.
- The prototype does not include a production enclosure, custom PCB, certified USB power switch, or anti-cheat compatibility claim.
- Use of automated input must comply with the target application's rules.
