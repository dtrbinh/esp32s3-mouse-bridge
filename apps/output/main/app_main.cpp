#include "esp_log.h"

#include "spi_packet_consumer.hpp"

namespace {
void on_packet(void*, const mouse_bridge::MouseReport&, uint32_t, uint64_t) {
    // Task 8 forwards valid reports into OutputState.
}
}

extern "C" void app_main() {
    static SpiPacketConsumer spi_consumer(on_packet, nullptr);
    ESP_ERROR_CHECK(spi_consumer.start());
    ESP_LOGI("bridge_output", "SPI mouse bridge consumer started");
}

