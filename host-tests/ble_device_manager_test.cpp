#include "ble_device_manager.hpp"

#include <cassert>

namespace {
BlePeerAddress address(uint8_t tail, uint8_t type = 0) {
    return {{{1, 2, 3, 4, 5, tail}}, type};
}

BleAdvertisement advertisement(BlePeerAddress peer, std::string_view name) {
    return {peer, name, -40, std::nullopt, false};
}

class FakePersistence final : public BleHistoryPersistence {
public:
    BleDeviceHistory load_history() override { return loaded; }
    bool save_history(const BleDeviceHistory& history) override {
        ++save_count;
        saved = history;
        return save_ok;
    }

    BleDeviceHistory loaded{};
    BleDeviceHistory saved{};
    int save_count{};
    bool save_ok{true};
};

BleManagerDecision take(BleDeviceManager& manager,
                        BleManagerDecisionKind expected, uint32_t now_ms) {
    const auto decision = manager.service(now_ms);
    assert(decision.has_value());
    assert(decision->kind == expected);
    return *decision;
}
}  // namespace

int main() {
    FakePersistence persistence;
    BleDeviceManager manager(persistence);
    manager.start(0);
    take(manager, BleManagerDecisionKind::StartPortal, 0);

    const auto freewolf_rpa = address(0x70, 1);
    const auto freewolf_id = address(0x17, 1);
    manager.on_advertisement(advertisement(freewolf_rpa, "Freewolf X7"),
                             std::nullopt, 10);
    assert(manager.snapshot(10).nearby.count == 1);
    assert(!manager.service(10).has_value());

    manager.request_connect(freewolf_rpa);
    auto decision = take(manager, BleManagerDecisionKind::Connect, 20);
    assert(decision.peer == freewolf_rpa);
    manager.on_validated_connection(freewolf_rpa, freewolf_id,
                                    "Freewolf X7", 30);
    manager.service(30);  // Flush persistence before the portal decision.
    assert(persistence.save_count > 0);
    auto view = manager.snapshot(30);
    assert(view.setup.connected == freewolf_id);
    assert(view.saved.count == 1);
    assert(view.saved.devices[0].identity == freewolf_id);

    FakePersistence reconnect_persistence;
    reconnect_persistence.loaded.record_success(freewolf_id, "Freewolf X7");
    BleDeviceManager reconnect(reconnect_persistence);
    reconnect.start(100);
    reconnect.on_advertisement(advertisement(address(0x71, 1), "Other"),
                               address(0x99, 1), 110);
    assert(!reconnect.service(110).has_value());
    reconnect.on_advertisement(advertisement(freewolf_rpa, "Freewolf X7"),
                               freewolf_id, 120);
    decision = take(reconnect, BleManagerDecisionKind::Connect, 120);
    assert(decision.peer == freewolf_rpa);

    reconnect.on_attempt_failed(freewolf_rpa,
                                BleSelectionFailure::ConnectionFailed, 130);
    take(reconnect, BleManagerDecisionKind::StartPortal, 130);
    assert(!reconnect.snapshot(130).setup.selected.has_value());

    FakePersistence failing_persistence;
    failing_persistence.save_ok = false;
    BleDeviceManager persistence_failure(failing_persistence);
    persistence_failure.start(0);
    take(persistence_failure, BleManagerDecisionKind::StartPortal, 0);
    persistence_failure.request_connect(freewolf_rpa);
    take(persistence_failure, BleManagerDecisionKind::Connect, 1);
    persistence_failure.on_validated_connection(
        freewolf_rpa, freewolf_id, "Freewolf X7", 2);
    persistence_failure.service(2);
    view = persistence_failure.snapshot(2);
    assert(view.setup.connected == freewolf_id);
    assert(view.setup.error == BleAttemptError::PersistenceFailed);

    persistence_failure.request_disconnect();
    take(persistence_failure, BleManagerDecisionKind::Disconnect, 3);
    assert(!persistence_failure.snapshot(3).setup.selected.has_value());

    manager.request_forget(freewolf_id);
    take(manager, BleManagerDecisionKind::Disconnect, 40);
    decision = take(manager, BleManagerDecisionKind::Forget, 40);
    assert(decision.peer == freewolf_id);
    assert(manager.snapshot(40).saved.count == 0);
}
