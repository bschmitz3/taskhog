#include "i2c_bus.h"

#include "board_pins.h"
#include "esp_log.h"

static const char *TAG = "i2c_bus";

esp_err_t board_i2c_init(i2c_master_bus_handle_t *out_bus)
{
    i2c_master_bus_config_t cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .sda_io_num = BOARD_I2C_SDA_GPIO,
        .scl_io_num = BOARD_I2C_SCL_GPIO,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    return i2c_new_master_bus(&cfg, out_bus);
}

esp_err_t board_i2c_deinit(i2c_master_bus_handle_t bus)
{
    if (bus == NULL) {
        return ESP_OK;
    }
    return i2c_del_master_bus(bus);
}

static const char *device_name(uint8_t addr)
{
    switch (addr) {
    case BOARD_I2C_ADDR_ES8311:
        return "ES8311 (codec)";
    case BOARD_I2C_ADDR_PCF85063:
        return "PCF85063 (RTC)";
    case BOARD_I2C_ADDR_SHTC3:
        return "SHTC3 (temp/hum)";
    default:
        return "unknown";
    }
}

void board_i2c_scan(i2c_master_bus_handle_t bus)
{
    ESP_LOGI(TAG, "I2C scan SDA=GPIO%d SCL=GPIO%d", BOARD_I2C_SDA_GPIO, BOARD_I2C_SCL_GPIO);

    int found = 0;
    for (uint8_t addr = 0x08; addr < 0x78; addr++) {
        esp_err_t err = i2c_master_probe(bus, addr, 50);
        if (err == ESP_OK) {
            found++;
            ESP_LOGI(TAG, "  0x%02X — %s", addr, device_name(addr));
        }
    }

    if (found == 0) {
        ESP_LOGW(TAG, "Nenhum dispositivo I2C encontrado — verificar pinos");
    } else {
        ESP_LOGI(TAG, "Total: %d dispositivo(s)", found);
    }

    const uint8_t expected[] = {
        BOARD_I2C_ADDR_ES8311,
        BOARD_I2C_ADDR_PCF85063,
        BOARD_I2C_ADDR_SHTC3,
    };
    for (size_t i = 0; i < sizeof(expected); i++) {
        uint8_t addr = expected[i];
        esp_err_t err = i2c_master_probe(bus, addr, 50);
        ESP_LOGI(TAG, "Gate M0-T3: 0x%02X %s", addr, err == ESP_OK ? "OK" : "FALHOU");
    }
}
