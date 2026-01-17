idf_component_register(
    SRCS "control_manager.c"
         "controller_ps3.c"
         "controller_serial.c"
         "controller_http.c"
    INCLUDE_DIRS "include"
    REQUIRES ps3 driver esp_http_server esp_wifi json motor motion safety
)
