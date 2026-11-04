#include "setup_portal.hpp"

#include "portal_codec.hpp"
#include "portal_page.hpp"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif_defaults.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"

#include <array>
#include <cstdio>
#include <span>

namespace {
constexpr char kTag[] = "setup_portal";
constexpr std::array<uint8_t, 4> kPortalAddress{192, 168, 4, 1};

esp_err_t portal_stage_error(const char* stage, esp_err_t result) {
    ESP_LOGE(kTag, "stage=%s error=%s (0x%x)", stage,
             esp_err_to_name(result), static_cast<unsigned>(result));
    return result;
}

SetupPortal* owner(httpd_req_t* request) {
    return static_cast<SetupPortal*>(request->user_ctx);
}

void no_cache(httpd_req_t* request) {
    httpd_resp_set_hdr(request, "Cache-Control", "no-store, max-age=0");
    httpd_resp_set_hdr(request, "Pragma", "no-cache");
}

esp_err_t send_ok(httpd_req_t* request) {
    no_cache(request);
    httpd_resp_set_type(request, "text/plain; charset=utf-8");
    return httpd_resp_send(request, "OK", 2);
}
}  // namespace

esp_err_t SetupPortal::start() {
    if (active()) return ESP_OK;
    const esp_err_t cleanup_result = unwind();
    if (cleanup_result != ESP_OK) return cleanup_result;
    if (!lifecycle_.begin()) return ESP_ERR_INVALID_STATE;

    esp_err_t result = esp_netif_init();
    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
        lifecycle_.fail();
        (void)unwind();
        return portal_stage_error("netif_init", result);
    }
    result = esp_event_loop_create_default();
    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
        lifecycle_.fail();
        (void)unwind();
        return portal_stage_error("event_loop", result);
    }
    esp_netif_config_t netif_config = ESP_NETIF_DEFAULT_WIFI_AP();
    ap_netif_ = esp_netif_new(&netif_config);
    if (ap_netif_ == nullptr) {
        lifecycle_.fail();
        (void)unwind();
        return portal_stage_error("netif_new", ESP_ERR_NO_MEM);
    }
    (void)lifecycle_.mark_acquired(PortalResource::Netif);
    result = esp_netif_attach_wifi_ap(ap_netif_);
    netif_attached_ = result == ESP_OK;
    if (result == ESP_OK) result = esp_wifi_set_default_wifi_ap_handlers();
    if (result != ESP_OK) {
        lifecycle_.fail();
        (void)unwind();
        return portal_stage_error("netif_attach", result);
    }

    wifi_init_config_t wifi_init = WIFI_INIT_CONFIG_DEFAULT();
    result = esp_wifi_init(&wifi_init);
    if (result != ESP_OK) {
        lifecycle_.fail();
        (void)unwind();
        return portal_stage_error("wifi_init", result);
    }
    (void)lifecycle_.mark_acquired(PortalResource::Wifi);
    result = configure_ap();
    if (result != ESP_OK) {
        lifecycle_.fail();
        (void)unwind();
        return portal_stage_error("wifi_config", result);
    }
    result = esp_wifi_start();
    if (result != ESP_OK) {
        lifecycle_.fail();
        (void)unwind();
        return portal_stage_error("wifi_start", result);
    }
    (void)lifecycle_.mark_acquired(PortalResource::WifiStarted);

    result = start_http();
    if (result != ESP_OK) {
        lifecycle_.fail();
        (void)unwind();
        return portal_stage_error("http_start", result);
    }
    (void)lifecycle_.mark_acquired(PortalResource::Http);

    result = dns_.start(kPortalAddress, 0);
    if (result != ESP_OK) {
        lifecycle_.fail();
        (void)unwind();
        return portal_stage_error("dns_start", result);
    }
    (void)lifecycle_.mark_acquired(PortalResource::Dns);
    ESP_LOGI(kTag, "Portal active at http://192.168.4.1");
    return ESP_OK;
}

esp_err_t SetupPortal::stop() {
    lifecycle_.stop();
    return unwind();
}

esp_err_t SetupPortal::unwind() {
    while (const auto resource = lifecycle_.next_release()) {
        esp_err_t result = ESP_OK;
        switch (*resource) {
        case PortalResource::Dns:
            result = dns_.stop();
            break;
        case PortalResource::Http:
            if (http_ != nullptr) {
                std::array<int, 4> clients{};
                size_t client_count = clients.size();
                if (httpd_get_client_list(http_, &client_count,
                                          clients.data()) == ESP_OK) {
                    for (size_t i = 0; i < client_count; ++i) {
                        (void)httpd_sess_trigger_close(http_, clients[i]);
                    }
                }
                result = httpd_stop(http_);
                if (result == ESP_OK) http_ = nullptr;
            }
            break;
        case PortalResource::WifiStarted:
            result = esp_wifi_stop();
            if (result == ESP_ERR_WIFI_NOT_STARTED) result = ESP_OK;
            break;
        case PortalResource::Wifi:
            result = esp_wifi_deinit();
            if (result == ESP_ERR_WIFI_NOT_INIT) result = ESP_OK;
            break;
        case PortalResource::Netif:
            if (ap_netif_ != nullptr) {
                if (netif_attached_) {
                    result = esp_wifi_clear_default_wifi_driver_and_handlers(
                        ap_netif_);
                    if (result == ESP_OK) netif_attached_ = false;
                }
                if (result == ESP_OK) {
                    esp_netif_destroy(ap_netif_);
                    ap_netif_ = nullptr;
                }
            }
            break;
        }
        if (result != ESP_OK) return result;
        if (!lifecycle_.mark_released(*resource)) return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

esp_err_t SetupPortal::configure_ap() {
    uint8_t mac[6]{};
    esp_err_t result = esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    if (result != ESP_OK) return result;

    wifi_config_t config{};
    const int written = std::snprintf(
        reinterpret_cast<char*>(config.ap.ssid), sizeof(config.ap.ssid),
        "MouseBridge-Setup-%02X%02X", mac[4], mac[5]);
    if (written <= 0 || static_cast<size_t>(written) >= sizeof(config.ap.ssid)) {
        return ESP_ERR_INVALID_SIZE;
    }
    config.ap.ssid_len = written;
    config.ap.channel = 1;
    config.ap.authmode = WIFI_AUTH_OPEN;
    config.ap.max_connection = 4;
    config.ap.beacon_interval = 100;
    config.ap.csa_count = 3;
    config.ap.dtim_period = 1;
    result = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (result != ESP_OK) return result;
    result = esp_wifi_set_mode(WIFI_MODE_AP);
    if (result != ESP_OK) return result;
    result = esp_wifi_set_config(WIFI_IF_AP, &config);
    if (result != ESP_OK) return result;
    ESP_LOGI(kTag, "Setup AP SSID=%s", config.ap.ssid);
    return ESP_OK;
}

esp_err_t SetupPortal::start_http() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.core_id = 0;
    config.stack_size = 6144;
    config.task_priority = 2;
    config.recv_wait_timeout = 1;
    config.send_wait_timeout = 1;
    config.max_open_sockets = 4;
    config.max_uri_handlers = 16;
    config.lru_purge_enable = true;
    config.uri_match_fn = httpd_uri_match_wildcard;
    esp_err_t result = httpd_start(&http_, &config);
    if (result != ESP_OK) return result;

    const std::array<httpd_uri_t, 12> handlers{{
        {"/", HTTP_GET, root_handler, this},
        {"/api/state", HTTP_GET, state_handler, this},
        {"/api/connect", HTTP_POST, connect_handler, this},
        {"/api/disconnect", HTTP_POST, disconnect_handler, this},
        {"/api/forget", HTTP_POST, forget_handler, this},
        {"/api/close", HTTP_POST, close_handler, this},
        {"/hotspot-detect.html", HTTP_GET, probe_handler, this},
        {"/generate_204", HTTP_GET, probe_handler, this},
        {"/connecttest.txt", HTTP_GET, probe_handler, this},
        {"/ncsi.txt", HTTP_GET, probe_handler, this},
        {"/canonical.html", HTTP_GET, probe_handler, this},
        {"/*", HTTP_GET, redirect_handler, this},
    }};
    for (const auto& handler : handlers) {
        result = httpd_register_uri_handler(http_, &handler);
        if (result != ESP_OK) {
            (void)httpd_stop(http_);
            http_ = nullptr;
            return result;
        }
    }
    return ESP_OK;
}

esp_err_t SetupPortal::root_handler(httpd_req_t* request) {
    no_cache(request);
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    return httpd_resp_send(request, kPortalPage.data(), kPortalPage.size());
}

esp_err_t SetupPortal::state_handler(httpd_req_t* request) {
    auto* self = owner(request);
    const auto view = self->manager_.snapshot(
        static_cast<uint32_t>(esp_timer_get_time() / 1'000));
    const auto size = write_portal_json(view, self->json_buffer_);
    if (!size.has_value()) {
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "state too large");
    }
    no_cache(request);
    httpd_resp_set_type(request, "application/json; charset=utf-8");
    return httpd_resp_send(request, self->json_buffer_.data(), *size);
}

esp_err_t SetupPortal::read_peer(httpd_req_t* request, BlePeerAddress& peer) {
    if (request->content_len <= 0 || request->content_len > 96) {
        return ESP_ERR_INVALID_ARG;
    }
    std::array<char, 97> body{};
    size_t received = 0;
    const int64_t deadline_us = esp_timer_get_time() + 1'500'000;
    while (received < static_cast<size_t>(request->content_len)) {
        if (esp_timer_get_time() >= deadline_us) return ESP_ERR_TIMEOUT;
        const int amount = httpd_req_recv(
            request, body.data() + received, request->content_len - received);
        if (amount == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (amount <= 0) return ESP_FAIL;
        received += amount;
    }
    const auto parsed = parse_peer_form({body.data(), received});
    if (!parsed.has_value()) return ESP_ERR_INVALID_ARG;
    peer = *parsed;
    return ESP_OK;
}

esp_err_t SetupPortal::connect_handler(httpd_req_t* request) {
    BlePeerAddress peer{};
    if (owner(request)->read_peer(request, peer) != ESP_OK) {
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST,
                                   "invalid device");
    }
    if (!owner(request)->manager_.request_connect(peer)) {
        no_cache(request);
        httpd_resp_set_status(request, "409 Conflict");
        httpd_resp_set_type(request, "text/plain; charset=utf-8");
        return httpd_resp_send(request, "connection already in progress",
                               HTTPD_RESP_USE_STRLEN);
    }
    return send_ok(request);
}

esp_err_t SetupPortal::disconnect_handler(httpd_req_t* request) {
    owner(request)->manager_.request_disconnect();
    return send_ok(request);
}

esp_err_t SetupPortal::forget_handler(httpd_req_t* request) {
    BlePeerAddress peer{};
    if (owner(request)->read_peer(request, peer) != ESP_OK) {
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST,
                                   "invalid device");
    }
    owner(request)->manager_.request_forget(peer);
    return send_ok(request);
}

esp_err_t SetupPortal::close_handler(httpd_req_t* request) {
    owner(request)->manager_.request_close();
    return send_ok(request);
}

esp_err_t SetupPortal::probe_handler(httpd_req_t* request) {
    return root_handler(request);
}

esp_err_t SetupPortal::redirect_handler(httpd_req_t* request) {
    no_cache(request);
    httpd_resp_set_status(request, "302 Found");
    httpd_resp_set_hdr(request, "Location", "/");
    return httpd_resp_send(request, "Open Mouse Bridge setup", 23);
}
