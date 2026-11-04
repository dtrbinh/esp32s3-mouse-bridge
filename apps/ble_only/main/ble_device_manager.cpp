#include "ble_device_manager.hpp"

void BleDeviceManager::start(uint32_t now_ms) {
    Guard guard(lock_);
    if (started_) return;
    history_ = persistence_.load_history();
    catalog_.clear();
    setup_.start(history_.selected(), now_ms);
    started_ = true;
    history_dirty_ = false;
    persistence_failed_ = false;
    extra_read_ = 0;
    extra_write_ = 0;
    extra_count_ = 0;
    reconnect_attempt_ = false;
    reconnect_retry_after_ms_ = now_ms;
}

bool BleDeviceManager::request_connect(BlePeerAddress peer) {
    Guard guard(lock_);
    peer = normalize_ble_address(peer);
    if (setup_.snapshot().attempt.has_value()) return false;
    history_.disconnect_without_forget();
    history_.select(peer);
    reconnect_attempt_ = false;
    history_dirty_ = true;
    persistence_failed_ = false;
    setup_.request_connect(peer);
    return true;
}

void BleDeviceManager::request_disconnect() {
    Guard guard(lock_);
    history_.disconnect_without_forget();
    history_dirty_ = true;
    setup_.request_disconnect();
}

void BleDeviceManager::request_forget(BlePeerAddress identity) {
    Guard guard(lock_);
    identity = normalize_ble_address(identity);
    if (history_.selected() == identity) {
        setup_.request_disconnect();
    } else {
        setup_.forget(identity);
    }
    if (history_.forget(identity)) history_dirty_ = true;
    push_extra({BleManagerDecisionKind::Forget, identity});
}

void BleDeviceManager::request_portal() {
    Guard guard(lock_);
    setup_.request_portal();
}

void BleDeviceManager::request_close() {
    Guard guard(lock_);
    setup_.request_close();
}

void BleDeviceManager::set_portal_active(bool active) {
    Guard guard(lock_);
    setup_.on_portal_active(active);
}

void BleDeviceManager::suppress_portal() {
    Guard guard(lock_);
    setup_.on_portal_active(false);
}

void BleDeviceManager::portal_start_failed() {
    Guard guard(lock_);
    setup_.on_portal_start_failed();
}

void BleDeviceManager::push_extra(BleManagerDecision decision) {
    if (extra_count_ == extra_.size()) return;
    extra_[extra_write_] = decision;
    extra_write_ = (extra_write_ + 1) % extra_.size();
    ++extra_count_;
}

std::optional<BleManagerDecision> BleDeviceManager::pop_extra() {
    if (extra_count_ == 0) return std::nullopt;
    const auto result = extra_[extra_read_];
    extra_read_ = (extra_read_ + 1) % extra_.size();
    --extra_count_;
    return result;
}

BleManagerDecision BleDeviceManager::translate(BleSetupAction action) {
    BleManagerDecisionKind kind = BleManagerDecisionKind::StartPortal;
    switch (action.kind) {
    case BleSetupActionKind::StartPortal:
        kind = BleManagerDecisionKind::StartPortal;
        break;
    case BleSetupActionKind::StopPortal:
        kind = BleManagerDecisionKind::StopPortal;
        break;
    case BleSetupActionKind::Connect:
        kind = BleManagerDecisionKind::Connect;
        break;
    case BleSetupActionKind::Disconnect:
        kind = BleManagerDecisionKind::Disconnect;
        break;
    }
    return {kind, action.peer};
}

std::optional<BleManagerDecision> BleDeviceManager::service(uint32_t now_ms) {
    std::optional<BleDeviceHistory> to_save;
    {
        Guard guard(lock_);
        setup_.tick(now_ms);
        if (history_dirty_) {
            to_save = history_;
            history_dirty_ = false;
        }
    }
    if (to_save.has_value()) {
        const bool saved = persistence_.save_history(*to_save);
        Guard guard(lock_);
        persistence_failed_ = !saved;
    }

    Guard guard(lock_);
    if (const auto action = setup_.take_action()) return translate(*action);
    return pop_extra();
}

PortalView BleDeviceManager::snapshot(uint32_t now_ms) const {
    Guard guard(lock_);
    PortalView view{};
    view.setup = setup_.snapshot();
    if (persistence_failed_) {
        view.setup.error = BleAttemptError::PersistenceFailed;
    }
    view.saved = history_.snapshot(view.setup.connected);
    view.nearby = catalog_.snapshot(now_ms);
    return view;
}

BleManagerStatus BleDeviceManager::status(uint32_t now_ms) const {
    Guard guard(lock_);
    BleManagerStatus result{};
    result.setup = setup_.snapshot();
    if (persistence_failed_) {
        result.setup.error = BleAttemptError::PersistenceFailed;
    }
    result.saved_count = history_.count();
    result.nearby_count = catalog_.active_count(now_ms);
    return result;
}

void BleDeviceManager::on_advertisement(
    const BleAdvertisement& advertisement,
    std::optional<BlePeerAddress> resolved_identity, uint32_t now_ms) {
    Guard guard(lock_);
    BleAdvertisement normalized = advertisement;
    normalized.address = normalize_ble_address(normalized.address);
    catalog_.observe(normalized, now_ms);
    const auto selected = history_.selected();
    const auto setup = setup_.snapshot();
    if (setup.connected.has_value() || setup.attempt.has_value()) {
        return;
    }
    if (!auto_connect_name_fragment_.empty()) {
        if (normalized.name.find(auto_connect_name_fragment_) !=
            std::string_view::npos) {
            setup_.request_connect(normalized.address);
        }
        return;
    }
    if (!selected.has_value()) return;
    const BlePeerAddress comparison = normalize_ble_address(
        resolved_identity.value_or(normalized.address));
    const std::string_view selected_name = history_.selected_name();
    const bool address_matches =
        comparison == normalize_ble_address(*selected);
    const bool name_matches = !resolved_identity.has_value() &&
                              !selected_name.empty() &&
                              normalized.name == selected_name;
    const bool retry_due =
        static_cast<int32_t>(now_ms - reconnect_retry_after_ms_) >= 0;
    if ((address_matches || name_matches) && retry_due) {
        reconnect_attempt_ = true;
        setup_.request_connect(normalized.address);
    }
}

void BleDeviceManager::on_validated_connection(
    BlePeerAddress observed, BlePeerAddress stable_identity, std::string_view name,
    uint32_t now_ms) {
    Guard guard(lock_);
    observed = normalize_ble_address(observed);
    stable_identity = normalize_ble_address(stable_identity);
    if (setup_.snapshot().attempt != observed) return;
    const auto previous_identity = history_.selected();
    const bool rotated_identity =
        previous_identity.has_value() &&
        normalize_ble_address(*previous_identity) != stable_identity &&
        !name.empty() && history_.selected_name() == name;
    if (rotated_identity) {
        (void)history_.forget(*previous_identity);
    }
    reconnect_attempt_ = false;
    history_.record_success(stable_identity, name);
    history_dirty_ = true;
    persistence_failed_ = false;
    setup_.on_connected(stable_identity, now_ms);
}

void BleDeviceManager::on_attempt_failed(BlePeerAddress observed,
                                         BleSelectionFailure failure,
                                         uint32_t now_ms) {
    Guard guard(lock_);
    observed = normalize_ble_address(observed);
    const auto saved_peer = history_.selected();
    if (reconnect_attempt_ && saved_peer.has_value() &&
        failure == BleSelectionFailure::ConnectionFailed &&
        setup_.snapshot().attempt == observed) {
        reconnect_attempt_ = false;
        reconnect_retry_after_ms_ = now_ms + kReconnectRetryDelayMs;
        setup_.on_reconnect_failed(observed, *saved_peer,
                                   BleAttemptError::ConnectionFailed,
                                   now_ms);
        return;
    }
    setup_.on_failed(observed,
                     failure == BleSelectionFailure::NotMouse
                         ? BleAttemptError::NotMouse
                         : BleAttemptError::ConnectionFailed);
    history_.disconnect_without_forget();
    history_dirty_ = true;
}

void BleDeviceManager::on_disconnected(uint32_t now_ms) {
    Guard guard(lock_);
    reconnect_attempt_ = false;
    reconnect_retry_after_ms_ = now_ms;
    setup_.on_disconnected(now_ms);
}
