#include "ble_setup_state.hpp"

#include <cassert>

namespace {
BlePeerAddress address(uint8_t tail) {
    return {{{1, 2, 3, 4, 5, tail}}, 0};
}

BleSetupAction take(BleSetupState& state, BleSetupActionKind expected) {
    const auto action = state.take_action();
    assert(action.has_value());
    assert(action->kind == expected);
    return *action;
}
}  // namespace

int main() {
    BleSetupState empty;
    empty.start(std::nullopt, 100);
    assert(empty.snapshot().phase == BleSetupPhase::Portal);
    assert(empty.snapshot().portal_desired);
    take(empty, BleSetupActionKind::StartPortal);

    BleSetupState reconnect;
    reconnect.start(address(1), 100);
    assert(reconnect.snapshot().phase == BleSetupPhase::ScanningSelected);
    assert(!reconnect.snapshot().portal_desired);
    reconnect.tick(10'099);
    assert(!reconnect.take_action().has_value());
    reconnect.tick(10'100);
    assert(reconnect.snapshot().portal_desired);
    take(reconnect, BleSetupActionKind::StartPortal);

    BleSetupState connecting_fallback;
    connecting_fallback.start(address(1), 100);
    connecting_fallback.request_connect(address(1));
    take(connecting_fallback, BleSetupActionKind::Connect);
    connecting_fallback.tick(10'100);
    assert(connecting_fallback.snapshot().phase == BleSetupPhase::Connecting);
    assert(connecting_fallback.snapshot().portal_desired);
    take(connecting_fallback, BleSetupActionKind::StartPortal);

    BleSetupState manual;
    manual.start(address(1), 0);
    manual.request_portal();
    take(manual, BleSetupActionKind::StartPortal);
    manual.on_portal_active(true);
    manual.request_close();
    take(manual, BleSetupActionKind::StopPortal);
    assert(!manual.snapshot().portal_desired);

    BleSetupState invalid_close;
    invalid_close.start(std::nullopt, 0);
    take(invalid_close, BleSetupActionKind::StartPortal);
    invalid_close.request_close();
    assert(invalid_close.snapshot().portal_desired);
    assert(!invalid_close.take_action().has_value());

    BleSetupState portal_failure;
    portal_failure.start(std::nullopt, 0);
    take(portal_failure, BleSetupActionKind::StartPortal);
    portal_failure.on_portal_start_failed();
    assert(!portal_failure.snapshot().portal_desired);
    assert(!portal_failure.snapshot().portal_active);
    portal_failure.request_portal();
    take(portal_failure, BleSetupActionKind::StartPortal);

    const auto mouse_a = address(1);
    const auto mouse_b = address(2);
    BleSetupState switching;
    switching.start(mouse_a, 0);
    switching.on_connected(mouse_a, 10);
    assert(switching.snapshot().phase == BleSetupPhase::Connected);
    switching.request_connect(mouse_b);
    auto action = take(switching, BleSetupActionKind::Disconnect);
    assert(action.peer == mouse_a);
    assert(switching.snapshot().phase == BleSetupPhase::SwitchDisconnect);
    switching.on_disconnected(20);
    action = take(switching, BleSetupActionKind::Connect);
    assert(action.peer == mouse_b);
    switching.on_failed(mouse_b, BleAttemptError::NotMouse);
    assert(switching.snapshot().phase == BleSetupPhase::Failed);
    assert(!switching.snapshot().selected.has_value());
    assert(switching.snapshot().portal_desired);
    take(switching, BleSetupActionKind::StartPortal);

    BleSetupState disconnect;
    disconnect.start(mouse_a, 0);
    disconnect.on_connected(mouse_a, 1);
    disconnect.request_disconnect();
    assert(!disconnect.snapshot().selected.has_value());
    take(disconnect, BleSetupActionKind::Disconnect);
    take(disconnect, BleSetupActionKind::StartPortal);

    BleSetupState success;
    success.start(std::nullopt, 0);
    take(success, BleSetupActionKind::StartPortal);
    success.on_portal_active(true);
    success.request_connect(mouse_b);
    take(success, BleSetupActionKind::Connect);
    success.on_connected(mouse_b, 1'000);
    assert(success.snapshot().phase == BleSetupPhase::SuccessDelay);
    assert(success.snapshot().wifi_shutdown_due_ms == 4'000);
    success.tick(3'999);
    assert(!success.take_action().has_value());
    success.tick(4'000);
    take(success, BleSetupActionKind::StopPortal);
    assert(success.snapshot().phase == BleSetupPhase::Connected);

    BleSetupState cancelled_success;
    cancelled_success.start(std::nullopt, 0);
    take(cancelled_success, BleSetupActionKind::StartPortal);
    cancelled_success.on_portal_active(true);
    cancelled_success.request_connect(mouse_b);
    take(cancelled_success, BleSetupActionKind::Connect);
    cancelled_success.on_connected(mouse_b, 100);
    cancelled_success.on_disconnected(200);
    assert(cancelled_success.snapshot().portal_desired);
    assert(!cancelled_success.snapshot().wifi_shutdown_due_ms.has_value());
    cancelled_success.tick(3'100);
    assert(!cancelled_success.take_action().has_value());

    BleSetupState forget_selected;
    forget_selected.start(mouse_a, 0);
    forget_selected.forget(mouse_a);
    assert(!forget_selected.snapshot().selected.has_value());
    assert(forget_selected.snapshot().portal_desired);
    take(forget_selected, BleSetupActionKind::StartPortal);
}
