idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES control motor motion safety nvs_flash esp_http_server)
