#include "ble_setup_state.hpp"

void BleSetupState::push(BleSetupActionKind kind,
                         std::optional<BlePeerAddress> peer) {
    if (action_count_ == actions_.size()) return;
    actions_[action_write_] = {kind, peer};
    action_write_ = (action_write_ + 1) % actions_.size();
    ++action_count_;
}

void BleSetupState::ensure_portal() {
    if (!snapshot_.portal_desired) {
        snapshot_.portal_desired = true;
        push(BleSetupActionKind::StartPortal);
    }
}

void BleSetupState::start(std::optional<BlePeerAddress> selected,
                          uint32_t now_ms) {
    snapshot_ = {};
    actions_ = {};
    action_read_ = 0;
    action_write_ = 0;
    action_count_ = 0;
    last_now_ms_ = now_ms;
    reconnect_started_ms_ = now_ms;
    snapshot_.selected = selected;
    if (selected.has_value()) {
        snapshot_.phase = BleSetupPhase::ScanningSelected;
    } else {
        snapshot_.phase = BleSetupPhase::Portal;
        ensure_portal();
    }
}

void BleSetupState::tick(uint32_t now_ms) {
    last_now_ms_ = now_ms;
    if (!snapshot_.portal_desired && snapshot_.selected.has_value() &&
        !snapshot_.connected.has_value() &&
        now_ms - reconnect_started_ms_ >= kPortalFallbackMs) {
        if (snapshot_.phase == BleSetupPhase::ScanningSelected) {
            snapshot_.phase = BleSetupPhase::Portal;
        }
        ensure_portal();
    }
    if (snapshot_.phase == BleSetupPhase::SuccessDelay &&
        snapshot_.wifi_shutdown_due_ms.has_value() &&
        static_cast<int32_t>(now_ms - *snapshot_.wifi_shutdown_due_ms) >= 0) {
        snapshot_.wifi_shutdown_due_ms.reset();
        snapshot_.portal_desired = false;
        if (snapshot_.portal_active) push(BleSetupActionKind::StopPortal);
        snapshot_.phase = BleSetupPhase::Connected;
    }
}

void BleSetupState::request_portal() {
    ensure_portal();
    if (snapshot_.phase != BleSetupPhase::SwitchDisconnect &&
        snapshot_.phase != BleSetupPhase::Connecting) {
        snapshot_.phase = BleSetupPhase::Portal;
    }
}

void BleSetupState::request_close() {
    if (!snapshot_.selected.has_value() && !snapshot_.connected.has_value()) {
        return;
    }
    if (snapshot_.portal_desired || snapshot_.portal_active) {
        snapshot_.portal_desired = false;
        push(BleSetupActionKind::StopPortal);
    }
    snapshot_.wifi_shutdown_due_ms.reset();
    if (snapshot_.connected.has_value()) {
        snapshot_.phase = BleSetupPhase::Connected;
    } else {
        snapshot_.phase = BleSetupPhase::ScanningSelected;
        reconnect_started_ms_ = last_now_ms_;
    }
}

void BleSetupState::request_connect(BlePeerAddress peer) {
    snapshot_.error = BleAttemptError::None;
    snapshot_.selected = peer;
    snapshot_.attempt = peer;
    snapshot_.wifi_shutdown_due_ms.reset();
    if (snapshot_.connected == peer) {
        snapshot_.attempt.reset();
        return;
    }
    if (snapshot_.connected.has_value()) {
        snapshot_.phase = BleSetupPhase::SwitchDisconnect;
        push(BleSetupActionKind::Disconnect, snapshot_.connected);
    } else {
        snapshot_.phase = BleSetupPhase::Connecting;
        push(BleSetupActionKind::Connect, peer);
    }
}

void BleSetupState::request_disconnect() {
    const bool attempt_in_progress = snapshot_.attempt.has_value();
    const auto attempted_peer = snapshot_.attempt;
    snapshot_.selected.reset();
    snapshot_.attempt.reset();
    snapshot_.wifi_shutdown_due_ms.reset();
    snapshot_.error = BleAttemptError::None;
    if (snapshot_.connected.has_value()) {
        push(BleSetupActionKind::Disconnect, snapshot_.connected);
    } else if (attempt_in_progress) {
        push(BleSetupActionKind::Disconnect, attempted_peer);
    }
    snapshot_.phase = BleSetupPhase::Portal;
    ensure_portal();
}

void BleSetupState::forget(BlePeerAddress peer) {
    if (snapshot_.selected != peer && snapshot_.attempt != peer &&
        snapshot_.connected != peer) {
        return;
    }
    request_disconnect();
}

void BleSetupState::on_connected(BlePeerAddress peer, uint32_t now_ms) {
    last_now_ms_ = now_ms;
    snapshot_.connected = peer;
    snapshot_.selected = peer;
    snapshot_.attempt.reset();
    snapshot_.error = BleAttemptError::None;
    if (snapshot_.portal_desired || snapshot_.portal_active) {
        // Queue shutdown from service()/tick(), not the BLE callback. A link
        // loss before the setup task runs can then cancel the shutdown safely.
        snapshot_.phase = BleSetupPhase::SuccessDelay;
        snapshot_.wifi_shutdown_due_ms = now_ms;
    } else {
        snapshot_.phase = BleSetupPhase::Connected;
        snapshot_.wifi_shutdown_due_ms.reset();
    }
}

void BleSetupState::on_disconnected(uint32_t now_ms) {
    last_now_ms_ = now_ms;
    snapshot_.connected.reset();
    snapshot_.wifi_shutdown_due_ms.reset();
    if (snapshot_.phase == BleSetupPhase::SwitchDisconnect &&
        snapshot_.attempt.has_value()) {
        snapshot_.phase = BleSetupPhase::Connecting;
        push(BleSetupActionKind::Connect, snapshot_.attempt);
        return;
    }
    if (snapshot_.portal_active || snapshot_.portal_desired) {
        snapshot_.phase = BleSetupPhase::Portal;
        ensure_portal();
    } else if (snapshot_.selected.has_value()) {
        snapshot_.phase = BleSetupPhase::ScanningSelected;
        reconnect_started_ms_ = now_ms;
    } else {
        snapshot_.phase = BleSetupPhase::Portal;
        ensure_portal();
    }
}

void BleSetupState::on_failed(BlePeerAddress peer, BleAttemptError error) {
    if (snapshot_.attempt != peer) return;
    snapshot_.attempt.reset();
    if (snapshot_.selected == peer) snapshot_.selected.reset();
    snapshot_.error = error;
    snapshot_.phase = BleSetupPhase::Failed;
    snapshot_.wifi_shutdown_due_ms.reset();
    ensure_portal();
}

void BleSetupState::on_reconnect_failed(BlePeerAddress attempted_peer,
                                        BlePeerAddress saved_peer,
                                        BleAttemptError error,
                                        uint32_t now_ms) {
    if (snapshot_.attempt != attempted_peer) return;
    last_now_ms_ = now_ms;
    snapshot_.attempt.reset();
    snapshot_.selected = saved_peer;
    snapshot_.error = error;
    snapshot_.phase = BleSetupPhase::ScanningSelected;
    snapshot_.wifi_shutdown_due_ms.reset();
}

void BleSetupState::on_portal_active(bool active) {
    snapshot_.portal_active = active;
}

void BleSetupState::on_portal_start_failed() {
    snapshot_.portal_active = false;
    snapshot_.portal_desired = false;
}

std::optional<BleSetupAction> BleSetupState::take_action() {
    if (action_count_ == 0) return std::nullopt;
    const BleSetupAction result = actions_[action_read_];
    action_read_ = (action_read_ + 1) % actions_.size();
    --action_count_;
    return result;
}
