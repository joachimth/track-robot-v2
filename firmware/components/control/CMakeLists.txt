set(SRCS
    "control_manager.c"
    "controller_serial.c"
    "controller_http.c"
)

set(REQS driver esp_http_server esp_wifi json motor motion safety)

if(CONFIG_ROBOT_ENABLE_PS3)
    list(APPEND SRCS "controller_ps3.c")
    list(APPEND REQS ps3)
endif()

if(CONFIG_ROBOT_ENABLE_PS4)
    list(APPEND SRCS "controller_ps4.c")
    list(APPEND REQS ps4)
endif()

idf_component_register(
    SRCS ${SRCS}
    INCLUDE_DIRS "include"
    REQUIRES ${REQS}
)
