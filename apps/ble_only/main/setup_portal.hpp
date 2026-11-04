#pragma once

#include "ble_device_manager.hpp"
#include "captive_dns.hpp"
#include "portal_lifecycle.hpp"

#include "esp_err.h"
#include "esp_http_server.h"
#include "esp_netif.h"

#include <array>

class SetupPortal {
public:
    explicit SetupPortal(BleDeviceManager& manager) : manager_(manager) {}

    esp_err_t start();
    esp_err_t stop();
    [[nodiscard]] bool active() const { return lifecycle_.active(); }

private:
    static esp_err_t root_handler(httpd_req_t* request);
    static esp_err_t state_handler(httpd_req_t* request);
    static esp_err_t connect_handler(httpd_req_t* request);
    static esp_err_t disconnect_handler(httpd_req_t* request);
    static esp_err_t forget_handler(httpd_req_t* request);
    static esp_err_t close_handler(httpd_req_t* request);
    static esp_err_t probe_handler(httpd_req_t* request);
    static esp_err_t redirect_handler(httpd_req_t* request);

    esp_err_t configure_ap();
    esp_err_t start_http();
    esp_err_t read_peer(httpd_req_t* request, BlePeerAddress& peer);
    esp_err_t unwind();

    BleDeviceManager& manager_;
    PortalLifecycle lifecycle_{};
    CaptiveDns dns_{};
    esp_netif_t* ap_netif_{};
    bool netif_attached_{};
    httpd_handle_t http_{};
    std::array<char, 8192> json_buffer_{};
};
