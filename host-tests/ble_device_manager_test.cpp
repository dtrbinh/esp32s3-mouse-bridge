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
    BleDeviceHistory load_history() override {
        ++load_count;
        return loaded;
    }
    bool save_history(const BleDeviceHistory& history) override {
        ++save_count;
        saved = history;
        return save_ok;
    }

    BleDeviceHistory loaded{};
    BleDeviceHistory saved{};
    int load_count{};
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
    FakePersistence idempotent_persistence;
    BleDeviceManager idempotent(idempotent_persistence);
    idempotent.start(0);
    take(idempotent, BleManagerDecisionKind::StartPortal, 0);
    idempotent.set_portal_active(true);
    idempotent.start(1);
    assert(idempotent_persistence.load_count == 1);
    assert(idempotent.snapshot(1).setup.portal_active);

    FakePersistence auto_target_persistence;
    BleDeviceManager auto_target(auto_target_persistence, "X7");
    auto_target.start(0);
    take(auto_target, BleManagerDecisionKind::StartPortal, 0);
    auto_target.on_advertisement(advertisement(address(0x40), "Other mouse"),
                                 std::nullopt, 1);
    assert(!auto_target.service(1).has_value());
    const auto auto_target_peer = address(0x41, 1);
    auto_target.on_advertisement(
        advertisement(auto_target_peer, "Freewolf X7 BLE"), std::nullopt, 2);
    const auto auto_target_decision =
        take(auto_target, BleManagerDecisionKind::Connect, 2);
    assert(auto_target_decision.peer == auto_target_peer);

    FakePersistence stale_target_persistence;
    const auto stale_target_peer = address(0x42, 1);
    stale_target_persistence.loaded.record_success(stale_target_peer,
                                                   "Old mouse");
    BleDeviceManager stale_target(stale_target_persistence, "X7");
    stale_target.start(0);
    stale_target.on_advertisement(
        advertisement(stale_target_peer, "Old mouse"), stale_target_peer, 1);
    assert(!stale_target.service(1).has_value());
    stale_target.on_advertisement(
        advertisement(auto_target_peer, "Freewolf X7 BLE"), std::nullopt, 2);
    const auto stale_target_decision =
        take(stale_target, BleManagerDecisionKind::Connect, 2);
    assert(stale_target_decision.peer == auto_target_peer);

    FakePersistence disabled_portal_persistence;
    disabled_portal_persistence.loaded.record_success(auto_target_peer,
                                                      "Freewolf X7 BLE");
    BleDeviceManager disabled_portal(disabled_portal_persistence, "X7");
    disabled_portal.start(0);
    take(disabled_portal, BleManagerDecisionKind::StartPortal, 10'000);
    disabled_portal.suppress_portal();
    assert(!disabled_portal.service(10'020).has_value());
    assert(!disabled_portal.service(20'000).has_value());

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

    assert(manager.request_connect(freewolf_rpa));
    assert(!manager.request_connect(address(0x71, 1)));
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
    const auto summary = manager.status(30);
    assert(summary.setup.connected == freewolf_id);
    assert(summary.saved_count == 1);
    assert(summary.nearby_count == 1);

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

    FakePersistence rotating_address_persistence;
    rotating_address_persistence.loaded.record_success(freewolf_id,
                                                        "Freewolf X7");
    BleDeviceManager rotating_address(rotating_address_persistence);
    rotating_address.start(100);
    const auto next_freewolf_rpa = address(0x72, 1);
    rotating_address.on_advertisement(
        advertisement(next_freewolf_rpa, "Freewolf X7"), std::nullopt, 121);
    decision = take(rotating_address, BleManagerDecisionKind::Connect, 121);
    assert(decision.peer == next_freewolf_rpa);
    rotating_address.on_validated_connection(
        next_freewolf_rpa, next_freewolf_rpa, "Freewolf X7", 122);
    rotating_address.service(122);
    const auto rotated_view = rotating_address.snapshot(122);
    assert(rotated_view.saved.count == 1);
    assert(rotated_view.saved.devices[0].identity == next_freewolf_rpa);

    reconnect.on_attempt_failed(freewolf_rpa,
                                BleSelectionFailure::ConnectionFailed, 130);
    take(reconnect, BleManagerDecisionKind::StartPortal, 130);
    assert(!reconnect.snapshot(130).setup.selected.has_value());

    FakePersistence controller_identity_persistence;
    controller_identity_persistence.loaded.record_success(address(0x17, 0),
                                                           "Public mouse");
    BleDeviceManager controller_identity(controller_identity_persistence);
    controller_identity.start(0);
    controller_identity.on_advertisement(
        advertisement(address(0x17, 2), "Public mouse"), address(0x17, 2), 1);
    decision = take(controller_identity, BleManagerDecisionKind::Connect, 1);
    assert(decision.peer == address(0x17, 0));

    FakePersistence race_persistence;
    BleDeviceManager race(race_persistence);
    race.start(0);
    take(race, BleManagerDecisionKind::StartPortal, 0);
    const auto race_a = address(0x31, 1);
    const auto race_b = address(0x32, 1);
    assert(race.request_connect(race_a));
    assert(!race.request_connect(race_b));
    take(race, BleManagerDecisionKind::Connect, 1);
    race.on_validated_connection(race_b, race_b, "stale", 2);
    assert(!race.snapshot(2).setup.connected.has_value());
    race.on_validated_connection(race_a, race_a, "A", 3);
    assert(race.snapshot(3).setup.connected == race_a);

    FakePersistence failing_persistence;
    failing_persistence.save_ok = false;
    BleDeviceManager persistence_failure(failing_persistence);
    persistence_failure.start(0);
    take(persistence_failure, BleManagerDecisionKind::StartPortal, 0);
    assert(persistence_failure.request_connect(freewolf_rpa));
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

    FakePersistence forget_persistence;
    forget_persistence.loaded.record_success(freewolf_id, "Freewolf X7");
    BleDeviceManager forget_selected_manager(forget_persistence);
    forget_selected_manager.start(0);
    forget_selected_manager.request_forget(freewolf_id);
    assert(!forget_selected_manager.snapshot(0).setup.selected.has_value());
    take(forget_selected_manager, BleManagerDecisionKind::StartPortal, 0);
    take(forget_selected_manager, BleManagerDecisionKind::Forget, 0);

    FakePersistence forget_attempt_persistence;
    forget_attempt_persistence.loaded.record_success(freewolf_id,
                                                      "Freewolf X7");
    BleDeviceManager forget_attempt(forget_attempt_persistence);
    forget_attempt.start(0);
    forget_attempt.on_advertisement(
        advertisement(freewolf_rpa, "Freewolf X7"), freewolf_id, 1);
    take(forget_attempt, BleManagerDecisionKind::Connect, 1);
    forget_attempt.request_forget(freewolf_id);
    take(forget_attempt, BleManagerDecisionKind::Disconnect, 2);
    take(forget_attempt, BleManagerDecisionKind::StartPortal, 2);
    take(forget_attempt, BleManagerDecisionKind::Forget, 2);
}
