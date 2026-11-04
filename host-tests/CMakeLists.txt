cmake_minimum_required(VERSION 3.20)
project(mouse_bridge_host_tests LANGUAGES CXX)

enable_testing()
add_executable(smoke_test smoke_test.cpp)
target_compile_features(smoke_test PRIVATE cxx_std_20)
add_test(NAME smoke COMMAND smoke_test)

add_executable(wire_protocol_test wire_protocol_test.cpp)
target_compile_features(wire_protocol_test PRIVATE cxx_std_20)
target_include_directories(wire_protocol_test PRIVATE ../components/mouse_bridge/include)
target_sources(wire_protocol_test PRIVATE ../components/mouse_bridge/src/wire_protocol.cpp)
add_test(NAME wire_protocol COMMAND wire_protocol_test)

add_executable(cadence_test cadence_test.cpp
    ../components/mouse_bridge/src/cadence.cpp)
target_compile_features(cadence_test PRIVATE cxx_std_20)
target_include_directories(cadence_test PRIVATE
    ../components/mouse_bridge/include)
add_test(NAME cadence COMMAND cadence_test)

add_executable(profile_engine_test profile_engine_test.cpp)
target_compile_features(profile_engine_test PRIVATE cxx_std_20)
target_include_directories(profile_engine_test PRIVATE
    ../components/mouse_bridge/include
    ../components/profile_engine/include)
target_sources(profile_engine_test PRIVATE
    ../components/profile_engine/src/profile_engine.cpp
    ../components/profile_engine/src/default_profiles.cpp)
add_test(NAME profile_engine COMMAND profile_engine_test)

add_executable(hid_parser_test hid_parser_test.cpp)
target_compile_features(hid_parser_test PRIVATE cxx_std_20)
target_include_directories(hid_parser_test PRIVATE
    .
    ../components/mouse_bridge/include
    ../components/hid_parser/include)
target_sources(hid_parser_test PRIVATE
    ../components/hid_parser/src/report_descriptor.cpp
    ../components/hid_parser/src/mouse_decoder.cpp)
add_test(NAME hid_parser COMMAND hid_parser_test)

add_executable(source_arbiter_test source_arbiter_test.cpp
    ../apps/input/main/source_arbiter.cpp)
target_compile_features(source_arbiter_test PRIVATE cxx_std_20)
target_include_directories(source_arbiter_test PRIVATE
    ../apps/input/main
    ../components/mouse_bridge/include)
add_test(NAME source_arbiter COMMAND source_arbiter_test)

add_executable(spi_mailbox_test spi_mailbox_test.cpp
    ../components/spi_link/src/latest_report_mailbox.cpp)
target_compile_features(spi_mailbox_test PRIVATE cxx_std_20)
target_include_directories(spi_mailbox_test PRIVATE
    ../components/spi_link/include
    ../components/mouse_bridge/include)
add_test(NAME spi_mailbox COMMAND spi_mailbox_test)

add_executable(spi_keepalive_test spi_keepalive_test.cpp
    ../components/spi_link/src/keepalive.cpp)
target_compile_features(spi_keepalive_test PRIVATE cxx_std_20)
target_include_directories(spi_keepalive_test PRIVATE
    ../components/spi_link/include
    ../components/mouse_bridge/include)
add_test(NAME spi_keepalive COMMAND spi_keepalive_test)

add_executable(output_state_test output_state_test.cpp
    ../apps/output/main/output_state.cpp)
target_compile_features(output_state_test PRIVATE cxx_std_20)
target_include_directories(output_state_test PRIVATE
    ../apps/output/main
    ../components/mouse_bridge/include)
add_test(NAME output_state COMMAND output_state_test)

add_executable(report_scheduler_test report_scheduler_test.cpp
    ../apps/output/main/report_scheduler.cpp)
target_compile_features(report_scheduler_test PRIVATE cxx_std_20)
target_include_directories(report_scheduler_test PRIVATE
    ../apps/output/main
    ../components/mouse_bridge/include)
target_sources(report_scheduler_test PRIVATE
    ../components/mouse_bridge/src/cadence.cpp)
add_test(NAME report_scheduler COMMAND report_scheduler_test)

add_executable(boot_press_test boot_press_test.cpp
    ../apps/ble_only/main/boot_press.cpp)
target_compile_features(boot_press_test PRIVATE cxx_std_20)
target_include_directories(boot_press_test PRIVATE ../apps/ble_only/main)
add_test(NAME boot_press COMMAND boot_press_test)

add_executable(ble_only_mode_test ble_only_mode_test.cpp
    ../apps/ble_only/main/ble_only_mode.cpp
    ../components/profile_engine/src/profile_engine.cpp
    ../components/profile_engine/src/default_profiles.cpp)
target_compile_features(ble_only_mode_test PRIVATE cxx_std_20)
target_include_directories(ble_only_mode_test PRIVATE
    ../apps/ble_only/main
    ../components/mouse_bridge/include
    ../components/profile_engine/include)
add_test(NAME ble_only_mode COMMAND ble_only_mode_test)

add_executable(ble_bond_reset_test ble_bond_reset_test.cpp)
target_compile_features(ble_bond_reset_test PRIVATE cxx_std_20)
target_include_directories(ble_bond_reset_test PRIVATE ../apps/input/main)
add_test(NAME ble_bond_reset COMMAND ble_bond_reset_test)

add_executable(status_led_model_test status_led_model_test.cpp
    ../apps/ble_only/main/status_led_model.cpp)
target_compile_features(status_led_model_test PRIVATE cxx_std_20)
target_include_directories(status_led_model_test PRIVATE ../apps/ble_only/main)
add_test(NAME status_led_model COMMAND status_led_model_test)

add_executable(ble_device_catalog_test ble_device_catalog_test.cpp
    ../apps/ble_only/main/ble_device_catalog.cpp)
target_compile_features(ble_device_catalog_test PRIVATE cxx_std_20)
target_include_directories(ble_device_catalog_test PRIVATE
    ../apps/ble_only/main
    ../apps/input/main)
add_test(NAME ble_device_catalog COMMAND ble_device_catalog_test)

add_executable(ble_device_history_test ble_device_history_test.cpp
    ../apps/ble_only/main/ble_device_history.cpp)
target_compile_features(ble_device_history_test PRIVATE cxx_std_20)
target_include_directories(ble_device_history_test PRIVATE
    ../apps/ble_only/main
    ../apps/input/main)
add_test(NAME ble_device_history COMMAND ble_device_history_test)

add_executable(ble_setup_state_test ble_setup_state_test.cpp
    ../apps/ble_only/main/ble_setup_state.cpp)
target_compile_features(ble_setup_state_test PRIVATE cxx_std_20)
target_include_directories(ble_setup_state_test PRIVATE
    ../apps/ble_only/main
    ../apps/input/main)
add_test(NAME ble_setup_state COMMAND ble_setup_state_test)

