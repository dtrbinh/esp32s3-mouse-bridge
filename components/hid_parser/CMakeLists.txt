idf_component_register(
    SRCS "src/report_descriptor.cpp" "src/mouse_decoder.cpp"
         "src/keyboard_decoder.cpp"
    INCLUDE_DIRS "include"
    REQUIRES mouse_bridge
)

