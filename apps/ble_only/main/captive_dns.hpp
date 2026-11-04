#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <array>
#include <atomic>
#include <cstdint>

class CaptiveDns {
public:
    esp_err_t start(std::array<uint8_t, 4> ipv4, BaseType_t core);
    void stop();
    [[nodiscard]] bool active() const {
        return running_.load(std::memory_order_acquire);
    }

private:
    static constexpr size_t kPacketSize = 512;
    static constexpr size_t kStackWords = 4096 / sizeof(StackType_t);

    static void task_entry(void* argument);
    void task();

    std::array<uint8_t, 4> ipv4_{};
    int socket_{-1};
    std::atomic<bool> running_{};
    std::atomic<bool> stopping_{};
    TaskHandle_t task_handle_{};
    StaticTask_t task_control_{};
    std::array<StackType_t, kStackWords> task_stack_{};
    StaticSemaphore_t stopped_control_{};
    SemaphoreHandle_t stopped_{};
};
