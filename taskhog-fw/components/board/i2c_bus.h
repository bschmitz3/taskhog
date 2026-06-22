#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"

esp_err_t board_i2c_init(i2c_master_bus_handle_t *out_bus);
esp_err_t board_i2c_deinit(i2c_master_bus_handle_t bus);
void board_i2c_scan(i2c_master_bus_handle_t bus);
