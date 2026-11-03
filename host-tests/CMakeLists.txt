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

