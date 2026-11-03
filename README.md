# Dual ESP32-S3 Mouse Bridge

Hai firmware ESP-IDF cho hai bo Waveshare ESP32-S3-Zero N4R2:

- `apps/input`: nhận chuột dây hoặc receiver 2.4 GHz bằng USB Host, hoặc chuột BLE HID; chuẩn hóa report và gửi qua SPI.
- `apps/output`: nhận SPI và xuất USB HID mouse sang máy tính với endpoint interval 1 ms.

## Cảnh báo nguồn và USB

Bo input dùng GPIO19 (D-) và GPIO20 (D+) cho cổng USB Host ngoài. Không cắm cổng USB-C onboard của bo input vào máy tính khi cổng Host ngoài đang hoạt động. VBUS của chuột phải lấy từ nguồn 5 V có giới hạn dòng/load switch; không cấp từ GPIO. Hai bo phải chung GND.

## SPI giữa hai bo

| Tín hiệu | GPIO |
|---|---:|
| SCLK | 4 |
| MOSI | 5 |
| MISO | 6 |
| CS | 7 |
| READY | 8 |

## Toolchain

Yêu cầu ESP-IDF v6.0.3 và CMake 3.20+. Sau khi export môi trường ESP-IDF:

```bash
idf.py -C apps/input set-target esp32s3
idf.py -C apps/input build
idf.py -C apps/output set-target esp32s3
idf.py -C apps/output build
```

Unit test portable không cần ESP-IDF:

```bash
cmake -S host-tests -B build/host
cmake --build build/host
ctest --test-dir build/host --output-on-failure
```

Xem [wiring](docs/wiring.md), [bring-up](docs/bringup.md) và [validation](docs/validation.md) khi các phần cứng adapter được hoàn thiện.
