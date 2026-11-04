# Đấu dây hai bo ESP32-S3-Zero N4R2

## Sơ đồ chức năng

```text
Chuột dây / receiver 2.4 GHz ─ USB-A Host ┐
                                          ├─ ESP32-S3 input ─ SPI ─ ESP32-S3 output ─ USB-C ─ PC
Chuột BLE ────────────────────────────────┘
```

Bo input làm USB Host hoặc BLE Central. Bo output chỉ làm USB HID Device với PC.

## SPI giữa hai bo

Tắt nguồn cả hai bo trước khi đấu. Dây nên ngắn, đặc biệt SCLK và GND.

| Tín hiệu | Bo input | Bo output | Hướng |
|---|---:|---:|---|
| SCLK | GPIO4 | GPIO4 | input → output |
| MOSI | GPIO5 | GPIO5 | input → output |
| MISO | GPIO6 | GPIO6 | dự phòng output → input |
| CS | GPIO7 | GPIO7 | input → output |
| READY | GPIO8 | GPIO8 | input → output |
| GND | GND | GND | bắt buộc chung mass |

SPI chạy mode 0, 10 MHz, packet protocol v2 cố định 34 byte. READY phát xung cạnh lên sau khi một transaction hoàn tất. Hai bo phải được flash cùng phiên bản firmware.

## USB Host trên bo input

Gắn một đầu USB-A cái ngoài:

| USB-A | ESP32-S3 input |
|---|---|
| D- | GPIO19 |
| D+ | GPIO20 |
| GND | GND chung |
| VBUS | nguồn 5 V ngoài qua load switch giới hạn dòng |

Không cấp VBUS chuột từ GPIO. Nên dùng load switch có giới hạn dòng, chống dòng ngược và tụ decoupling theo datasheet của switch.

> Cảnh báo: USB-C onboard của bo input dùng chung GPIO19/GPIO20. Không cắm USB-C onboard vào máy tính khi VBUS của cổng USB-A Host đang bật. Việc nối hai nguồn 5 V có thể gây back-feed và hỏng phần cứng.

## USB Device trên bo output

Cổng USB-C onboard của bo output là cổng native USB nối với PC. Firmware xuất HID mouse Full Speed, endpoint IN `0x81`, packet 8 byte, interval 1 ms.

## Nút BOOT

- BOOT/GPIO0 bo input: giữ lâu để xóa bond BLE duy nhất và quét ghép đôi lại.
- BOOT/GPIO0 bo output: giữ để bypass chuyển động sinh bởi profile; chuyển động vật lý và nút chuột vẫn đi qua.

Hai chức năng độc lập vì nằm trên hai bo khác nhau.
