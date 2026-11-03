cmake_minimum_required(VERSION 3.20)
project(mouse_bridge_host_tests LANGUAGES CXX)

enable_testing()
add_executable(smoke_test smoke_test.cpp)
target_compile_features(smoke_test PRIVATE cxx_std_20)
add_test(NAME smoke COMMAND smoke_test)

