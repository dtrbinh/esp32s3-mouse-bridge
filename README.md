# Dual ESP32-S3 Mouse Bridge

Hai firmware ESP-IDF cho hai bo ESP32-S3. Bản BLE-only hiện được cấu hình cho
biến thể **N8R8: flash 8 MB + Octal PSRAM 8 MB**:

- `apps/input`: nhận chuột dây hoặc receiver 2.4 GHz bằng USB Host, hoặc chuột BLE HID; chuẩn hóa report và gửi qua SPI.
- `apps/output`: nhận SPI và xuất USB HID mouse sang máy tính với endpoint interval 1 ms.

Ngoài cặp firmware chính, `apps/ble_only` là bản test độc lập cho **một bo**:
nhận chuột BLE, chạy đủ 14 profile/mod và xuất USB HID trực tiếp qua USB-C
onboard. Bản này không khởi tạo USB Host hoặc SPI.

Pipeline hoàn chỉnh:

```text
USB Host / BLE HOGP → source arbiter → profile engine → SPI 10 MHz
→ failsafe → TinyUSB HID mouse thích nghi 125–1000 Hz
```

Firmware dùng cả hai nhân. Trên bo input, BLE và tác vụ bảo trì chạy ở CPU0; USB Host → profile → SPI chạy theo sự kiện ở CPU1. Trên bo output, SPI nhận/giải mã chạy ở CPU0; TinyUSB và HID scheduler chạy ở CPU1. USB descriptor vẫn quảng bá `bInterval=1 ms`, còn report thành công thực tế bám cadence đầu vào.

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
idf.py -C apps/ble_only set-target esp32s3
idf.py -C apps/ble_only build
```

Unit test portable không cần ESP-IDF:

```bash
cmake -S host-tests -B build/host
cmake --build build/host
ctest --test-dir build/host --output-on-failure
```

Xem [wiring](docs/wiring.md), [bring-up](docs/bringup.md) và [validation](docs/validation.md) trước khi cấp nguồn hoặc nối phần cứng.

## Profile và diagnostics

Profile engine mặc định bật. Nút chuột 4/5 chọn profile 1/2; các profile 1–14 có thể chọn làm mặc định qua `CONFIG_MOUSE_BRIDGE_DEFAULT_PROFILE`. Giữ BOOT trên bo output để bypass chuyển động sinh nhưng vẫn giữ pass-through vật lý.

Trong firmware một bo `ble_only`, BOOT nhấn dưới 700 ms chuyển profile, giữ
1,5–4,999 giây rồi thả để bật/tắt mod, và giữ đủ 5 giây để mở portal chọn
chuột mà không xóa lịch sử/bond.
Khoảng 700–1.499 ms cố ý không thực hiện hành động.

Khi chưa có chuột đã chọn, board tạo Wi-Fi mở `MouseBridge-Setup-XXXX` và mở
captive portal tại `192.168.4.1`. Trang có hai phần: thiết bị đã lưu (thiết bị
đang kết nối ở đầu) và thiết bị gần đây; hỗ trợ Connect, Disconnect, Forget và
đóng cấu hình. AP chỉ dùng cục bộ, không có Internet, và tắt hoàn toàn sau 3
giây khi kết nối đã được xác thực.

WS2812 onboard GPIO21 báo trạng thái ở giới hạn tổng RGB 10%: vàng nhấp nháy
300 ms khi đang scan/portal, xanh lá nhấp nháy 2 giây khi đã kết nối nhưng mod tắt,
và sáng liên tục bằng màu riêng của profile 1–14 khi mod bật.

Hai firmware in snapshot `MB_DIAG` mỗi 5 giây. Decoder:

```bash
python3 tools/decode_diagnostics.py --latest input.log output.log
```

Chi tiết trình tự an toàn, nguồn Host VBUS và các ca kiểm tra phần cứng nằm trong ba tài liệu phía trên.

Giao thức SPI hiện là version 2, packet 34 byte. Luôn flash cả hai firmware cùng commit; không ghép một bo chạy firmware cũ với một bo chạy firmware mới.
