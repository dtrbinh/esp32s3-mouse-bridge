#include "esp_log.h"

#include "spi_packet_consumer.hpp"
#include "usb_mouse_device.hpp"

namespace {
void on_packet(void* context, const mouse_bridge::MouseReport& report,
               uint32_t sequence, uint64_t rx_us) {
    static_cast<UsbMouseDevice*>(context)->on_packet(report, sequence, rx_us);
}
}

extern "C" void app_main() {
    static UsbMouseDevice usb_mouse;
    static SpiPacketConsumer spi_consumer(on_packet, &usb_mouse);
    ESP_ERROR_CHECK(usb_mouse.start());
    ESP_ERROR_CHECK(spi_consumer.start());
    ESP_LOGI("bridge_output", "1 kHz USB HID mouse bridge started");
}

