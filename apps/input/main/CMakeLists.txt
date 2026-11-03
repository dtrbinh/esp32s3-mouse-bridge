idf_component_register(
    SRCS
        "app_main.cpp"
        "source_arbiter.cpp"
        "usb_mouse_host.cpp"
        "ble_hogp_client.cpp"
        "bond_control.cpp"
        "spi_packet_producer.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        esp_timer
        esp_driver_gpio
        esp_driver_spi
        esp_hid
        hid_parser
        mouse_bridge
        nvs_flash
        spi_link
        usb
        usb_host_hid)

