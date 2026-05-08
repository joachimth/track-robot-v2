idf_component_register(
    SRCS
        "control_manager.c"
        "controller_serial.c"
        "controller_http.c"
        "controller_ps4.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_http_server esp_wifi json nvs_flash motor motion safety
    PRIV_REQUIRES ps4
)
