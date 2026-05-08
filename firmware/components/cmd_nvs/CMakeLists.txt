idf_component_register(
    SRCS "cmd_nvs_stub.c"
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash console
)
