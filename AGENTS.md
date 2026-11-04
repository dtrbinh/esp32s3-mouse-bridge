# Project working rules

These rules capture lessons from the ESP32-S3 N8R8 BLE-only firmware work and
apply to future work in this repository.

## Scope and communication

- Confirm what the user wants reviewed: workflow, code, firmware performance,
  or hardware. Do not silently substitute one review target for another.
- A review request is read-only unless the user also asks for changes.
- Resume from Git state, existing plans, and prior evidence; do not redo
  completed work.
- Keep progress updates short and report outcomes rather than raw tool details.

## Efficient implementation

- Maintain one short checklist of remaining work.
- Split fixes into small coherent groups and run focused tests after each
  group. Run the complete host suite once at the final gate.
- Finalize and commit source before the final ESP-IDF build so the embedded
  Git version does not trigger another rebuild before flashing.
- Activate ESP-IDF once per working session and reuse that shell/session for
  build, flash, and monitor operations.
- Redirect long build output to a log and show only the first actionable
  error, image size, effective N8R8 settings, flash verification, and final
  result.
- Do not run repeated full-clean builds unless configuration regeneration is
  actually required.

## Board and flashing workflow

- This project targets ESP32-S3 N8R8: 8 MB flash and 8 MB Octal PSRAM.
- Detect the current USB mode before asking the user to manipulate the board.
  Prefer filtered `system_profiler SPUSBDataType` output on macOS; avoid broad
  `ioreg` output.
- If the board is enumerated as HID, ask for BOOT + RESET, then poll for the
  serial port and stop polling immediately when it appears.
- Before the post-flash reset, ensure BOOT is released. Verify both flash
  hashes and application-mode enumeration; a successful write alone is not a
  successful hardware validation.
- Use the detected serial port, never a guessed port.

## Completion evidence

- Report host test totals, ESP-IDF build result, image/partition size, detected
  chip variant, flash hash verification, and post-reset USB mode when relevant.
- Clearly separate verified facts from physical tests that have not yet been
  observed.
