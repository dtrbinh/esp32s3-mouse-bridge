# Bring-up an toàn

## 1. Build và kiểm tra trên máy phát triển

Dự án dùng ESP-IDF 6.0.3. Sau khi export môi trường IDF:

```bash
cmake -S host-tests -B build/host
cmake --build build/host
ctest --test-dir build/host --output-on-failure
idf.py -C apps/input build
idf.py -C apps/output build
idf.py -C apps/ble_only build
```

## Test BLE trên một bo

Firmware `apps/ble_only` không cần bo thứ hai, dây SPI hoặc nguồn VBUS Host.
Cắm USB-C onboard trực tiếp vào PC; cáp này vừa cấp nguồn vừa là USB HID output.

Lần đầu hoặc khi cần vào download mode, giữ BOOT, nhấn rồi thả RESET, sau đó
thả BOOT. Xác định cổng vừa xuất hiện và flash:

```bash
idf.py -C apps/ble_only -p /dev/cu.usbmodem_BLE_ONLY flash
```

Sau reset, cổng download có thể biến mất vì cùng USB-C đã chuyển sang thiết bị
HID `Adaptive HID Mouse Bridge`; đây là hành vi bình thường. Đặt chuột vào BLE
pairing mode, sau đó kiểm tra con trỏ, nút, wheel và pan.

BOOT có ba hành động, mỗi lần nhấn chỉ phát tối đa một hành động:

- Nhấn dưới 700 ms: chuyển profile 1 → 2 → … → 14 → 1.
- Giữ từ 700 ms đến dưới 1,5 giây: không làm gì.
- Giữ ít nhất 1,5 giây nhưng dưới 5 giây rồi thả: bật/tắt toàn bộ mod.
- Giữ đủ 5 giây: xóa bond ngay và quét lại; khi thả không đổi profile/mod.

Profile/mod khởi động ở trạng thái bật. Khi mod tắt, X/Y, nút, wheel và pan đi
thẳng từ chuột BLE sang PC. Vì USB-C onboard đang được TinyUSB HID chiếm dụng,
`idf.py monitor` trên cùng cổng sẽ ngắt sau khi app khởi động. Muốn xem log
`MB_DIAG` liên tục, dùng UART0 ngoài; việc điều khiển chuột không cần UART.

Phân bố mong đợi: `nimble_host`, `ble_mouse`, `ble_only_boot` và
`ble_only_diag` ở core 0; `ble_only_pipeline`, `usb_mouse` và TinyUSB ở core 1.

## 2. Flash từng bo riêng biệt

Chưa đấu SPI và chưa bật nguồn Host VBUS. Cắm đúng một bo mỗi lần:

```bash
idf.py -C apps/input -p /dev/cu.USB_INPUT flash monitor
idf.py -C apps/output -p /dev/cu.USB_OUTPUT flash monitor
```

Thay tên cổng serial bằng cổng thực tế. Thoát monitor bằng `Ctrl+]`.

## 3. Kiểm tra bo output độc lập

Kết nối riêng bo output với PC. PC phải nhận `Adaptive HID Mouse Bridge`; con trỏ không tự di chuyển và không có nút bị giữ. Firmware phát release-all khi khởi động và sau USB resume.

## 4. Đấu liên kết hai bo

1. Rút USB và tắt tất cả nguồn.
2. Nối GND trước, sau đó GPIO4–GPIO8 theo [wiring.md](wiring.md).
3. Kiểm tra thông mạch và chắc chắn không chập 5 V với 3,3 V.
4. Cấp nguồn bo input bằng đường nguồn dành cho hệ thống, vẫn để Host VBUS tắt.
5. Cắm bo output vào PC và xác nhận trạng thái release-all.
6. Bật nguồn 5 V đã bảo vệ cho cổng USB-A Host.
7. Cuối cùng mới cắm chuột dây hoặc receiver 2.4 GHz vào USB-A.

Không cắm USB-C onboard của bo input trong khi Host VBUS đang hoạt động.

## 5. BLE

Để chuột ở BLE pairing mode, không cắm USB/receiver. Bo input quét HID Service `0x1812`, bond một peer và yêu cầu interval 7,5 ms. Muốn thay chuột đã bond, giữ BOOT trên bo input để xóa bond rồi pairing lại.

## 6. Profile và bypass

Profile engine mặc định bật; `CONFIG_MOUSE_BRIDGE_DEFAULT_PROFILE` chọn profile khởi động từ 1 đến 14. Nút chuột 4 chọn profile 1, nút chuột 5 chọn profile 2. Profile chỉ sinh chuyển động khi cả nút trái và phải đang giữ.

Giữ BOOT trên bo output để loại chuyển động sinh bởi profile. Release BOOT để trở lại chế độ profile. Bypass không được làm mất chuyển động vật lý hay trạng thái nút.

## 7. Diagnostics

Mỗi bo in một dòng `MB_DIAG` tối đa mỗi 5 giây, ngoài đường report 1 ms. Giải mã log trực tiếp:

```bash
idf.py -C apps/input -p /dev/cu.USB_INPUT monitor | python3 tools/decode_diagnostics.py
```

Hoặc đọc hai file log và lấy mẫu cuối mỗi bo:

```bash
python3 tools/decode_diagnostics.py --latest input.log output.log
```

`usb_interval_us` trên input là khoảng cách report đã lọc vì USB Host HID API không công khai `bInterval`; trên output, đây là cadence HID đang áp dụng. Endpoint vẫn quảng bá `bInterval=1 ms`, nhưng trả NAK ở các USB frame không dùng để có thể đổi runtime mà không reconnect. `ble_interval_units` dùng đơn vị BLE 1,25 ms.

Output tự bám input trong khoảng 125–1000 Hz. Report chuyển động/nút mới luôn được gửi ở USB frame 1 ms kế tiếp; chỉ report duy trì trạng thái được giảm nhịp. BLE dùng connection interval đã thương lượng, ví dụ 7,5 ms sẽ xen kẽ khoảng cách 8 ms và 7 ms để không trôi pha dài hạn.

Sau report đầu tiên, bo input gửi keepalive trạng thái không có motion mỗi 2 ms khi chuột không phát report mới. Vì vậy nút đang giữ không bị timeout ở chế độ BLE; output chỉ kích hoạt failsafe khi chính liên kết SPI/bo input im lặng.

### Kiểm tra phân bố hai nhân

Ngay sau boot, bo input phải báo `input_pipeline`, `usb_events`, `mouse_input` và `spi_producer` trên core 1. `nimble_host`, `ble_mouse`, `bond_button` và `diagnostics` phải chạy trên core 0. Report thật đánh thức pipeline ngay; timeout 1 ms chỉ còn dùng cho BLE fallback và watchdog.

Bo output phải báo `spi_consumer` và `diagnostics` trên core 0, `usb_mouse` trên core 1. TinyUSB cũng được cấu hình cố định ở core 1. `max_link_latency_us` trên output đo từ lúc nhận xong packet SPI đến lần HID scheduler đầu tiên xử lý packet đó.
