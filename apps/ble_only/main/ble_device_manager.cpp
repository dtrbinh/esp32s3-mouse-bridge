#include "ble_device_manager.hpp"

void BleDeviceManager::start(uint32_t now_ms) {
    BleDeviceHistory loaded = persistence_.load_history();
    Guard guard(lock_);
    history_ = loaded;
    catalog_.clear();
    setup_.start(history_.selected(), now_ms);
    history_dirty_ = false;
    persistence_failed_ = false;
    extra_read_ = 0;
    extra_write_ = 0;
    extra_count_ = 0;
}

void BleDeviceManager::request_connect(BlePeerAddress peer) {
    Guard guard(lock_);
    history_.disconnect_without_forget();
    history_.select(peer);
    history_dirty_ = true;
    persistence_failed_ = false;
    setup_.request_connect(peer);
}

void BleDeviceManager::request_disconnect() {
    Guard guard(lock_);
    history_.disconnect_without_forget();
    history_dirty_ = true;
    setup_.request_disconnect();
}

void BleDeviceManager::request_forget(BlePeerAddress identity) {
    Guard guard(lock_);
    const auto connected = setup_.snapshot().connected;
    if (connected == identity) {
        setup_.request_disconnect();
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

void BleDeviceManager::on_advertisement(
    const BleAdvertisement& advertisement,
    std::optional<BlePeerAddress> resolved_identity, uint32_t now_ms) {
    Guard guard(lock_);
    catalog_.observe(advertisement, now_ms);
    const auto selected = history_.selected();
    const auto setup = setup_.snapshot();
    if (!selected.has_value() || setup.connected.has_value() ||
        setup.attempt.has_value()) {
        return;
    }
    const BlePeerAddress comparison =
        resolved_identity.value_or(advertisement.address);
    if (comparison == *selected) setup_.request_connect(advertisement.address);
}

void BleDeviceManager::on_validated_connection(
    BlePeerAddress, BlePeerAddress stable_identity, std::string_view name,
    uint32_t now_ms) {
    Guard guard(lock_);
    history_.record_success(stable_identity, name);
    history_dirty_ = true;
    persistence_failed_ = false;
    setup_.on_connected(stable_identity, now_ms);
}

void BleDeviceManager::on_attempt_failed(BlePeerAddress observed,
                                         BleSelectionFailure failure,
                                         uint32_t) {
    Guard guard(lock_);
    setup_.on_failed(observed,
                     failure == BleSelectionFailure::NotMouse
                         ? BleAttemptError::NotMouse
                         : BleAttemptError::ConnectionFailed);
    history_.disconnect_without_forget();
    history_dirty_ = true;
}

void BleDeviceManager::on_disconnected(uint32_t now_ms) {
    Guard guard(lock_);
    setup_.on_disconnected(now_ms);
}
