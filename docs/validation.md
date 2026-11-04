# Ma trận validation

Kết quả phần mềm dưới đây đã được chạy trên host. Các mục cần bo thật được để `CHƯA CHẠY`; không được đổi thành PASS nếu chưa ghi lại log hoặc ảnh logic analyzer.

## Kiểm tra tự động

| Kiểm tra | Kết quả hiện tại | Bằng chứng |
|---|---|---|
| Host unit tests | PASS | 12/12 CTest |
| Firmware input ESP-IDF | PASS | build ESP-IDF 6.0.3; `0x9a2a0`, còn 40% partition |
| Firmware output ESP-IDF | PASS | build ESP-IDF 6.0.3; `0x3afe0`, còn 77% partition; HID endpoint 8 byte, interval 1 ms |
| Firmware BLE-only ESP-IDF | PASS | build ESP-IDF 6.0.3; `0x87ae0`, còn 47% partition; BLE central + USB HID trên một bo |
| Whitespace/static diff | PASS | `git diff --check` |

## Kiểm tra phần cứng BLE-only một bo

| Ca kiểm tra | Cách thực hiện / tiêu chí PASS | Trạng thái |
|---|---|---|
| USB enumerate | Cắm USB-C onboard; PC nhận HID mouse, không có nút bị giữ | CHƯA CHẠY |
| BLE pairing | Chuột pairing được với HID service `0x1812`, con trỏ/nút/wheel/pan đúng | CHƯA CHẠY |
| BLE reconnect | Tắt/bật chuột; tự nối lại bond đã lưu | CHƯA CHẠY |
| Pass-through | Giữ BOOT 1,5–4,999 giây rồi thả để tắt mod; report vật lý không đổi | CHƯA CHẠY |
| Chuyển 14 profile | Nhấn BOOT nhanh 14 lần; profile tuần tự và wrap 14 → 1 | CHƯA CHẠY |
| Vùng chết BOOT | Giữ 700–1.499 ms; profile và mod không đổi | CHƯA CHẠY |
| Toggle mod | Mỗi lần giữ 1,5–4,999 giây rồi thả chỉ toggle đúng một lần | CHƯA CHẠY |
| Xóa bond | Giữ đủ 5 giây; bond cũ bị xóa, quét lại, lúc thả không toggle mod | CHƯA CHẠY |
| BLE disconnect release | Rút/tắt chuột khi giữ nút; PC release trong tối đa 20 ms | CHƯA CHẠY |
| Suspend/resume | Sleep/wake PC; report đầu sau resume là release-all | CHƯA CHẠY |
| Dual-core affinity | Log đúng core như `bringup.md`, không watchdog reset | CHƯA CHẠY |
| BLE latency | Đo tối thiểu 10.000 report; ghi median/p95/p99/max và interval BLE | CHƯA CHẠY |

## Kiểm tra phần cứng bắt buộc

| Ca kiểm tra | Cách thực hiện / tiêu chí PASS | Trạng thái |
|---|---|---|
| Chuột boot 125 Hz | Cắm chuột boot; nút, X/Y và wheel đúng, không report lỗi | CHƯA CHẠY |
| Chuột dây 1 kHz | Diagnostics input xấp xỉ 1000 µs; không drop/overrun | CHƯA CHẠY |
| Receiver 2.4 GHz 1 kHz | Như chuột dây; chuyển mode không kẹt nút | CHƯA CHẠY |
| BLE pairing | Pair HID `0x1812`, giữ nút đứng yên >20 ms vẫn giữ; ghi interval thực nhận | CHƯA CHẠY |
| BLE reconnect | Tắt/bật chuột; tự nối lại bond cũ | CHƯA CHẠY |
| Xóa BLE bond | Giữ BOOT input; peer cũ bị xóa và quét lại | CHƯA CHẠY |
| Chuyển ba mode | Lần lượt dây → 2.4 GHz → BLE; USB luôn ưu tiên, BLE fallback sau 50 ms | CHƯA CHẠY |
| Adaptive polling | Lần lượt chọn 125/250/500/1000 Hz; diagnostics output lần lượt khoảng 8000/4000/2000/1000 µs, sự kiện mới không chờ hết chu kỳ | CHƯA CHẠY |
| Dual-core affinity | Log input đúng CPU0/CPU1 và output đúng CPU0/CPU1 như `bringup.md`; không có watchdog reset | CHƯA CHẠY |
| Rút khi giữ nút | Giữ nút rồi rút nguồn vào; PC release trong tối đa 20 ms | CHƯA CHẠY |
| Emergency bypass | Profile đang chạy, giữ BOOT output; chỉ còn chuyển động vật lý | CHƯA CHẠY |
| Mất SPI | Ngắt SCLK/MOSI; motion xóa sau 5 ms, nút release sau 20 ms | CHƯA CHẠY |
| CRC lỗi | Chèn packet sai CRC; counter tăng, report không tới PC | CHƯA CHẠY |
| PC reboot | Reboot khi cầu nối đang bật; enumerate lại với release-all | CHƯA CHẠY |
| Suspend/resume | Sleep/wake PC; report đầu sau resume là release-all | CHƯA CHẠY |
| Descriptor không hỗ trợ | Cắm HID map malformed; interface bị reject an toàn | CHƯA CHẠY |
| Soak 1 kHz 10 phút | Phát 600.000 report; gaps, CRC, drops, overruns đều bằng 0 | CHƯA CHẠY |

## Đo latency

Mục tiêu thêm của cầu nối dây/2.4 GHz là 1–2 ms. Hiện tại **chưa xác minh** vì hai clock ESP32 độc lập. Counter input đo từ timestamp report đến lúc SPI master hoàn tất; counter output đo từ lúc nhận packet SPI đến lần HID scheduler đầu tiên xử lý packet. Hai counter nội bộ không thay thế phép đo end-to-end bằng logic analyzer.

Dùng logic analyzer ít nhất ba kênh:

1. USB input SOF hoặc GPIO debug tại callback input.
2. READY GPIO8.
3. USB output IN transaction hoặc GPIO debug ngay trước `tud_hid_report`.

Thu tối thiểu 10.000 mẫu cho chuột dây và receiver, báo median, p95, p99 và maximum. Chỉ đánh dấu mục tiêu PASS khi p95 nằm trong 1–2 ms và không có drop.

## Ghi kết quả

Lưu monitor log của cả hai bo, phiên bản commit, loại chuột/receiver, nguồn 5 V và ảnh logic analyzer cùng lần test. Dùng:

```bash
python3 tools/decode_diagnostics.py --latest input.log output.log
```

để chụp snapshot counter cuối bài test.
