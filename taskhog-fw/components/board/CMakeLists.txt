idf_component_register(
    SRCS "i2c_bus.c" "board_power.c" "board_buttons.c"
    INCLUDE_DIRS "."
    REQUIRES driver
)
