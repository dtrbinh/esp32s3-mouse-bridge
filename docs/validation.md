# Ma trận validation

Kết quả phần mềm dưới đây đã được chạy trên host. Các mục cần bo thật được để `CHƯA CHẠY`; không được đổi thành PASS nếu chưa ghi lại log hoặc ảnh logic analyzer.

## Kiểm tra tự động

| Kiểm tra | Kết quả hiện tại | Bằng chứng |
|---|---|---|
| Host unit tests | PASS | 21/21 CTest |
| Firmware input ESP-IDF | PASS | build ESP-IDF 6.0.3; `0x9aaf0`, còn 40% partition |
| Firmware output ESP-IDF | PASS | build ESP-IDF 6.0.3; `0x3afe0`, còn 77% partition; HID endpoint 8 byte, interval 1 ms |
| Firmware BLE-only N8R8 ESP-IDF | PASS | performance build ESP-IDF 6.0.3; `0x1159c0`, còn 64% partition 3 MB; flash 8 MB + Octal PSRAM |
| Whitespace/static diff | PASS | `git diff --check` |

## Kiểm tra phần cứng BLE-only một bo

| Ca kiểm tra | Cách thực hiện / tiêu chí PASS | Trạng thái |
|---|---|---|
| USB enumerate | Cắm USB-C onboard; PC nhận HID mouse, không có nút bị giữ | CHƯA CHẠY |
| Captive portal macOS | Join `MouseBridge-Setup-XXXX`; captive window tự mở và fallback `192.168.4.1` hoạt động | CHƯA CHẠY |
| Captive portal iOS | Join AP; captive window tự mở và hiển thị đủ hai danh sách/hành động | CHƯA CHẠY |
| Captive portal Android | Join AP; captive detection mở trang hoặc fallback hoạt động | CHƯA CHẠY |
| Captive portal Windows | Join AP; captive detection mở trang hoặc fallback hoạt động | CHƯA CHẠY |
| Freewolf X7 discovery | X7 xuất hiện trong Nearby dù advertisement không có UUID `0x1812` | CHƯA CHẠY |
| Select/validate/store/shutdown | Connect X7, HID map hợp lệ, lưu identity; Wi-Fi dừng sau trạng thái thành công 3 giây | CHƯA CHẠY |
| Power-cycle RPA reconnect | Tắt/bật nguồn; RPA mới resolve về identity đã lưu và chỉ reconnect đúng X7 | CHƯA CHẠY |
| Switch A→B failure | Đang nối A, chọn B không hợp lệ; A bị ngắt và không tự nối lại ẩn, portal vẫn mở | CHƯA CHẠY |
| Disconnect | Disconnect xóa target hiện tại nhưng giữ lịch sử và bond | CHƯA CHẠY |
| Forget | Forget xóa đúng lịch sử và NimBLE bond của peer | CHƯA CHẠY |
| Invalid HID rejection | Chọn thiết bị BLE không phải mouse/HID map lỗi; báo lỗi an toàn và portal vẫn hoạt động | CHƯA CHẠY |
| BLE reconnect | Tắt/bật chuột; tự nối lại identity đã lưu, fallback portal sau 10 giây nếu thất bại | CHƯA CHẠY |
| LED scan/portal | Khi chưa kết nối, reconnect hoặc portal mở: vàng 150 ms sáng, 150 ms tắt | CHƯA CHẠY |
| LED pass-through | Kết nối rồi tắt mod: xanh lá 1 giây sáng, 1 giây tắt | CHƯA CHẠY |
| LED 14 profile | Mod bật; 14 profile có 14 màu liên tục khác nhau, đổi trong tối đa 50 ms | CHƯA CHẠY |
| LED ưu tiên trạng thái | Mất BLE khi profile sáng: chuyển vàng; reconnect: trở lại màu profile | CHƯA CHẠY |
| LED sau BOOT | Nhấn nhanh đổi màu; giữ toggle đổi profile-color ↔ xanh lá; giữ 5 giây mở portal và chuyển vàng | CHƯA CHẠY |
| Nhiệt độ LED | Chạy liên tục ít nhất 30 phút; board/LED không nóng bất thường ở `R+G+B ≤ 76` | CHƯA CHẠY |
| Pass-through | Giữ BOOT 1,5–4,999 giây rồi thả để tắt mod; report vật lý không đổi | CHƯA CHẠY |
| Chuyển 14 profile | Nhấn BOOT nhanh 14 lần; profile tuần tự và wrap 14 → 1 | CHƯA CHẠY |
| Vùng chết BOOT | Giữ 700–1.499 ms; profile và mod không đổi | CHƯA CHẠY |
| Toggle mod | Mỗi lần giữ 1,5–4,999 giây rồi thả chỉ toggle đúng một lần | CHƯA CHẠY |
| BOOT mở portal | Giữ đủ 5 giây; portal mở, bond/lịch sử còn nguyên, lúc thả không toggle mod | CHƯA CHẠY |
| Wi-Fi disappearance | Sau validated selection + 3 giây, SSID biến mất và Wi-Fi driver dừng | CHƯA CHẠY |
| HID sau setup | Sau khi Wi-Fi dừng, pointer/buttons/wheel/pan/profile đúng và không tăng latency bất thường | CHƯA CHẠY |
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
