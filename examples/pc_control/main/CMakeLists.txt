idf_component_register(
    SRCS "main.c"
         "pc_cmd.c"
         "pc_config.c"
         "pc_state.c"
         "pc_hw.c"
         "pc_auth.c"
         "pc_controller.c"
         "pc_api.c"
         "pc_hid.c"
    INCLUDE_DIRS "."
    REQUIRES microlink nvs_flash esp_wifi esp_netif esp_event
             esp_http_server espressif__cjson esp_timer espressif__esp_tinyusb
    # esp_driver_gpio backs the real GPIO hardware layer (pc_hw.c).
    # espressif__esp_tinyusb backs pc_hid.c (gated by CONFIG_PCCTL_ENABLE_HID).
    PRIV_REQUIRES esp_driver_gpio
)
