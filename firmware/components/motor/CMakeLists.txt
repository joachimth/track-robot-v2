idf_component_register(
    SRCS "pwm_ledc.c" "motor_bts7960.c"
    INCLUDE_DIRS "include"
    REQUIRES driver
)
