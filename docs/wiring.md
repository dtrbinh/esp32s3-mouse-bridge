# Hardware Connections

## Overview

```text
BLE mouse ⇄ ESP32-S3 N8R8 ── native USB-C ── PC
```

Only one board and one data-capable USB-C cable are required. USB-C supplies
5 V to the board and carries USB HID output to the computer.

## Native USB

The ESP32-S3 uses GPIO19 for D- and GPIO20 for D+. On ESP32-S3-Zero and Super
Mini boards, these signals are routed internally to USB-C and may not be exposed
on the headers. No external D+/D- wiring is required.

Do not inject another 5 V supply into USB-C VBUS while the port is connected to
a computer. BLE-only firmware does not power an external mouse or receiver.

## BOOT and RESET

- BOOT/GPIO0: short press to select a profile; hold to toggle profiles or open
  the portal.
- RESET/EN: use with BOOT to enter ROM download mode for flashing.

## Status LED

The onboard WS2812 uses GPIO21 by default. Change `CONFIG_BLE_ONLY_LED_GPIO` for
board variants that route it to another pin.

## Optional UART debugging

To monitor logs after native USB switches to HID, connect an external USB-UART
adapter:

| USB-UART | ESP32-S3 |
|---|---|
| RX | Board UART0 TX |
| TX | Board UART0 RX |
| GND | GND |

Use a 3.3 V logic-level adapter and always connect the grounds.
