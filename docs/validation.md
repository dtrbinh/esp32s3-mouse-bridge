# Validation Checklist

Do not mark a hardware check as PASS without observing it on the board.

## Latest automated checks

| Check | Result |
|---|---|
| Host unit tests | PASS, 20/20 |
| ESP-IDF 6.0.3 build | PASS |
| N8R8 firmware | `0x116720` bytes, 64% of the 3 MB partition free |
| `git diff --check` | PASS |

Run these checks after every source change:

```bash
cmake -S host-tests -B host-tests/build -DCMAKE_BUILD_TYPE=Release
cmake --build host-tests/build
ctest --test-dir host-tests/build --output-on-failure
idf.py -C apps/ble_only build
git diff --check
```

## BLE and portal

| Check | PASS criteria | Status |
|---|---|---|
| Named-device scan | Only devices with names are displayed | NOT RERUN |
| Pair a new device | Connect, validate the HID map, and save the identity | NOT RERUN |
| Reconnect | Power-cycle the mouse or board and reconnect the existing bond | NOT RERUN |
| RPA reconnect | Resolve a changed advertising address to the stable identity | NOT RERUN |
| Connected/Ready/Offline | Dot and label match the actual state | NOT RERUN |
| Delete | Two presses remove the correct peer's history and bond | NOT RERUN |
| Wi-Fi shutdown | The access point disappears about 3 seconds after validation | NOT RERUN |
| Portal fallback | The portal opens when the target cannot reconnect | NOT RERUN |

## USB HID

| Check | PASS criteria | Status |
|---|---|---|
| Enumeration | The computer detects composite Mouse + Keyboard | NOT RERUN |
| Mouse motion | Smooth X/Y motion without abnormal drops | NOT RERUN |
| Buttons | Correct press/hold/release without click conversion or duplicates | NOT RERUN |
| Wheel/pan | Correct direction with no missing events | NOT RERUN |
| Q/E side buttons | Complete keyboard press, hold, and release | NOT RERUN |
| Disconnect release | Turning off a held mouse does not leave a stuck PC button | NOT RERUN |
| Suspend/resume | The first report after PC sleep/wake is safe | NOT RERUN |

## Profiles, BOOT, and LED

| Check | PASS criteria | Status |
|---|---|---|
| Seven profiles | Short presses select 1→7→1 | NOT RERUN |
| Realtime profile | Holding left produces continuous motion with no release jump | NOT RERUN |
| Profile toggle | Holding 1.5–4.999 seconds toggles exactly once | NOT RERUN |
| No-action interval | Holding 700–1,499 ms changes nothing | NOT RERUN |
| Open portal | Holding at least 5 seconds preserves the profile state | NOT RERUN |
| Scan LED | Yellow, 150 ms on / 150 ms off | NOT RERUN |
| Connected LED | Green, 1 second on / 1 second off | NOT RERUN |
| Profile LED | Seven distinct colors with `R+G+B ≤ 23` | NOT RERUN |

## Performance

Capture at least 10,000 reports and record median, p95, p99, and maximum
latency. Confirm that realtime tasks and TinyUSB run on CPU1, radio and portal
tasks run on CPU0, no watchdog reset occurs, and Wi-Fi coexistence traffic stops
after the portal closes.
