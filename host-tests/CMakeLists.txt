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

