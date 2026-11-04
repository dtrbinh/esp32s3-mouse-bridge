#include "captive_dns.hpp"

#include "captive_dns_codec.hpp"

#include "esp_log.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"

#include <array>
#include <cerrno>
#include <span>

namespace {
constexpr char kTag[] = "captive_dns";
}

esp_err_t CaptiveDns::start(std::array<uint8_t, 4> ipv4, BaseType_t core) {
    if (active() || socket_ >= 0) return ESP_ERR_INVALID_STATE;
    ipv4_ = ipv4;
    stopping_.store(false, std::memory_order_release);
    stopped_ = xSemaphoreCreateBinaryStatic(&stopped_control_);
    if (stopped_ == nullptr) return ESP_ERR_NO_MEM;

    socket_ = lwip_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_ < 0) return ESP_FAIL;

    int reuse = 1;
    (void)lwip_setsockopt(socket_, SOL_SOCKET, SO_REUSEADDR, &reuse,
                          sizeof(reuse));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(53);
    const uint32_t host_address =
        (static_cast<uint32_t>(ipv4[0]) << 24) |
        (static_cast<uint32_t>(ipv4[1]) << 16) |
        (static_cast<uint32_t>(ipv4[2]) << 8) | ipv4[3];
    address.sin_addr.s_addr = htonl(host_address);
    if (lwip_bind(socket_, reinterpret_cast<sockaddr*>(&address),
                  sizeof(address)) != 0) {
        lwip_close(socket_);
        socket_ = -1;
        return ESP_FAIL;
    }

    running_.store(true, std::memory_order_release);
    task_handle_ = xTaskCreateStaticPinnedToCore(
        task_entry, "captive_dns", task_stack_.size(), this, 2,
        task_stack_.data(), &task_control_, core);
    if (task_handle_ == nullptr) {
        running_.store(false, std::memory_order_release);
        lwip_close(socket_);
        socket_ = -1;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void CaptiveDns::stop() {
    if (!active() && socket_ < 0) return;
    stopping_.store(true, std::memory_order_release);
    const int socket = socket_;
    socket_ = -1;
    if (socket >= 0) {
        (void)lwip_shutdown(socket, SHUT_RDWR);
        lwip_close(socket);
    }
    if (task_handle_ != nullptr && stopped_ != nullptr) {
        if (xSemaphoreTake(stopped_, pdMS_TO_TICKS(1000)) != pdTRUE) {
            ESP_LOGW(kTag, "DNS task stop timed out");
        }
    }
    task_handle_ = nullptr;
    running_.store(false, std::memory_order_release);
}

void CaptiveDns::task_entry(void* argument) {
    static_cast<CaptiveDns*>(argument)->task();
}

void CaptiveDns::task() {
    std::array<uint8_t, kPacketSize> request{};
    std::array<uint8_t, kPacketSize> response{};
    while (!stopping_.load(std::memory_order_acquire)) {
        sockaddr_storage source{};
        socklen_t source_length = sizeof(source);
        const int socket = socket_;
        if (socket < 0) break;
        const int received = lwip_recvfrom(
            socket, request.data(), request.size(), 0,
            reinterpret_cast<sockaddr*>(&source), &source_length);
        if (received < 0) {
            if (stopping_.load(std::memory_order_acquire)) break;
            ESP_LOGW(kTag, "DNS receive failed, errno=%d", errno);
            continue;
        }
        const auto size = build_captive_dns_response(
            std::span<const uint8_t>(request.data(), received), response,
            ipv4_);
        if (!size.has_value()) continue;
        (void)lwip_sendto(socket, response.data(), *size, 0,
                          reinterpret_cast<sockaddr*>(&source),
                          source_length);
    }
    running_.store(false, std::memory_order_release);
    if (stopped_ != nullptr) xSemaphoreGive(stopped_);
    vTaskDelete(nullptr);
}
