idf_component_register(
    SRCS
        "app_main.cpp"
        "spi_packet_consumer.cpp"
        "output_state.cpp"
        "report_scheduler.cpp"
        "usb_mouse_device.cpp"
        "tusb_descriptors.c"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_gpio esp_driver_spi esp_timer mouse_bridge spi_link)

