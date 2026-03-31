idf_component_register(
    SRCS "led.c"
    INCLUDE_DIRS "."
    REQUIRES state_machine
    PRIV_REQUIRES esp_timer
)
