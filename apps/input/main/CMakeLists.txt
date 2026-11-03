idf_component_register(
    SRCS
        "app_main.cpp"
        "source_arbiter.cpp"
        "usb_mouse_host.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        esp_timer
        hid_parser
        mouse_bridge
        usb
        usb_host_hid)

