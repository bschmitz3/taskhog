/**
 * Driver SSD1681 1.54" 200x200 (Waveshare V2).
 *
 * Filosofia: o caminho elétrico que ATUALIZA o painel é o comprovadamente
 * funcional (LUT/waveform custom + power-on do booster + update 0xC7). A ÚNICA
 * mudança em relação ao firmware antigo é o endereçamento de RAM: aqui é
 * normal/canônico (data entry 0x03, janelas e contadores começando em 0), sem
 * hacks de orientação nos registradores.
 *
 * Toda correção de orientação (espelho/rotação) vive em epaper_cfg.h e é
 * aplicada num único ponto: o flush (build_tx_from_logical).
 *
 * Buffer lógico: origem topo-esquerda, x→direita, y→baixo (igual aos mockups).
 * Convenção de bit: 1 = branco, 0 = preto (RAM B/W do SSD1681).
 */
#include "epaper_drv.h"

#include <string.h>

#include "board_power.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "epaper_cfg.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "epaper_drv";

#define EPD_ROW_BYTES (EPAPER_WIDTH / 8)

/* Waveform full-refresh custom (OTP deste painel é não-confiável). */
static const uint8_t WF_FULL_1IN54[159] = {
    0x80, 0x48, 0x40, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x40, 0x48, 0x80, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x80, 0x48, 0x40, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x40, 0x48, 0x80, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0xA, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x8, 0x1, 0x0, 0x8, 0x1, 0x0, 0x2,
    0xA, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x0, 0x0, 0x0,
    0x22, 0x17, 0x41, 0x0, 0x32, 0x20,
};

static spi_device_handle_t s_spi;
static uint8_t *s_buf;     /* framebuffer lógico (topo-esq) */
static uint8_t *s_tx_buf;  /* buffer físico do painel (pós-transform) */
static bool s_ready;

static void cs_high(void) { gpio_set_level(BOARD_EPD_CS_GPIO, 1); }
static void cs_low(void) { gpio_set_level(BOARD_EPD_CS_GPIO, 0); }
static void dc_high(void) { gpio_set_level(BOARD_EPD_DC_GPIO, 1); }
static void dc_low(void) { gpio_set_level(BOARD_EPD_DC_GPIO, 0); }
static void rst_high(void) { gpio_set_level(BOARD_EPD_RST_GPIO, 1); }
static void rst_low(void) { gpio_set_level(BOARD_EPD_RST_GPIO, 0); }

static void wait_busy(void)
{
    while (gpio_get_level(BOARD_EPD_BUSY_GPIO) == 1) {
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

static void spi_send_byte(uint8_t data)
{
    spi_transaction_t t = {0};
    t.length = 8;
    t.tx_buffer = &data;
    ESP_ERROR_CHECK(spi_device_polling_transmit(s_spi, &t));
}

static void send_command(uint8_t cmd)
{
    dc_low();
    cs_low();
    spi_send_byte(cmd);
    cs_high();
}

static void send_data(uint8_t data)
{
    dc_high();
    cs_low();
    spi_send_byte(data);
    cs_high();
}

static void send_data_buf(const uint8_t *data, size_t len)
{
    dc_high();
    cs_low();
    spi_transaction_t t = {0};
    t.length = len * 8;
    t.tx_buffer = data;
    ESP_ERROR_CHECK(spi_device_polling_transmit(s_spi, &t));
    cs_high();
}

/* Janela e contadores RAM em endereçamento NORMAL (origem 0,0; sem hacks). */
static void panel_set_ram_window(void)
{
    send_command(0x11); /* Data entry: X inc, Y inc, atualiza X */
    send_data(0x03);

    send_command(0x44); /* RAM X start/end (bytes) */
    send_data(0x00);
    send_data(EPD_ROW_BYTES - 1);

    send_command(0x45); /* RAM Y start/end */
    send_data(0x00);
    send_data(0x00);
    send_data((EPAPER_HEIGHT - 1) & 0xFF);
    send_data(((EPAPER_HEIGHT - 1) >> 8) & 0xFF);

    send_command(0x4E); /* RAM X counter */
    send_data(0x00);

    send_command(0x4F); /* RAM Y counter */
    send_data(0x00);
    send_data(0x00);
}

static void set_lut(const uint8_t *lut)
{
    send_command(0x32);
    send_data_buf(lut, 153);
    wait_busy();
    send_command(0x3F);
    send_data(lut[153]);
    send_command(0x03);
    send_data(lut[154]);
    send_command(0x04);
    send_data(lut[155]);
    send_data(lut[156]);
    send_data(lut[157]);
    send_command(0x2C);
    send_data(lut[158]);
}

static void turn_on_display(void)
{
    send_command(0x22);
    send_data(0xC7);
    send_command(0x20);
    wait_busy();
}

static esp_err_t panel_init_sequence(void)
{
    rst_high();
    vTaskDelay(pdMS_TO_TICKS(50));
    rst_low();
    vTaskDelay(pdMS_TO_TICKS(20));
    rst_high();
    vTaskDelay(pdMS_TO_TICKS(50));

    wait_busy();
    send_command(0x12); /* SWRESET */
    wait_busy();

    send_command(0x01); /* Driver output control */
    send_data(0xC7);
    send_data(0x00);
    send_data(0x01);

    send_command(0x3C); /* Border waveform */
    send_data(0x01);

    send_command(0x18); /* Temperature sensor: interno */
    send_data(0x80);

    send_command(0x22); /* Power-on: enable clock + analog + load temp */
    send_data(0xB1);
    send_command(0x20);
    wait_busy();

    panel_set_ram_window();
    set_lut(WF_FULL_1IN54);
    return ESP_OK;
}

static esp_err_t gpio_spi_init(void)
{
    gpio_config_t out = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << BOARD_EPD_RST_GPIO) | (1ULL << BOARD_EPD_DC_GPIO) |
                        (1ULL << BOARD_EPD_CS_GPIO),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&out), TAG, "gpio out");

    gpio_config_t busy = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << BOARD_EPD_BUSY_GPIO),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&busy), TAG, "gpio busy");

    rst_high();
    cs_high();

    spi_bus_config_t bus = {
        .mosi_io_num = BOARD_EPD_MOSI_GPIO,
        .miso_io_num = -1,
        .sclk_io_num = BOARD_EPD_SCK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = EPAPER_BUF_BYTES,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(BOARD_EPD_SPI_HOST, &bus, SPI_DMA_CH_AUTO), TAG, "spi bus");

    spi_device_interface_config_t dev = {
        .clock_speed_hz = 20 * 1000 * 1000,
        .mode = 0,
        .spics_io_num = -1,
        .queue_size = 7,
    };
    ESP_RETURN_ON_ERROR(spi_bus_add_device(BOARD_EPD_SPI_HOST, &dev, &s_spi), TAG, "spi dev");

    ESP_LOGI(TAG, "SPI OK (MOSI=%d SCK=%d CS=%d DC=%d RST=%d BUSY=%d)",
             BOARD_EPD_MOSI_GPIO, BOARD_EPD_SCK_GPIO, BOARD_EPD_CS_GPIO,
             BOARD_EPD_DC_GPIO, BOARD_EPD_RST_GPIO, BOARD_EPD_BUSY_GPIO);
    return ESP_OK;
}

esp_err_t epaper_drv_init(void)
{
    if (s_ready) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(board_power_init(), TAG, "power");
    ESP_RETURN_ON_ERROR(board_power_epd_on(), TAG, "epd rail");
    vTaskDelay(pdMS_TO_TICKS(50));

    ESP_RETURN_ON_ERROR(gpio_spi_init(), TAG, "spi/gpio");

    s_buf = heap_caps_malloc(EPAPER_BUF_BYTES, MALLOC_CAP_SPIRAM);
    if (s_buf == NULL) {
        s_buf = heap_caps_malloc(EPAPER_BUF_BYTES, MALLOC_CAP_DMA);
    }
    if (s_buf == NULL) {
        return ESP_ERR_NO_MEM;
    }

    s_tx_buf = heap_caps_malloc(EPAPER_BUF_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (s_tx_buf == NULL) {
        free(s_buf);
        s_buf = NULL;
        return ESP_ERR_NO_MEM;
    }

    ESP_RETURN_ON_ERROR(panel_init_sequence(), TAG, "panel init");
    memset(s_buf, 0xFF, EPAPER_BUF_BYTES);
    s_ready = true;

    ESP_LOGI(TAG, "SSD1681 %dx%d pronto (entry=0x03; orient mx=%d my=%d swap=%d)",
             EPAPER_WIDTH, EPAPER_HEIGHT, EPD_MIRROR_X, EPD_MIRROR_Y, EPD_SWAP_XY);
    return ESP_OK;
}

bool epaper_drv_is_ready(void)
{
    return s_ready;
}

uint8_t *epaper_drv_buffer(void)
{
    return s_buf;
}

esp_err_t epaper_drv_fill(bool white)
{
    if (!s_ready || s_buf == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    memset(s_buf, white ? 0xFF : 0x00, EPAPER_BUF_BYTES);
    return ESP_OK;
}

esp_err_t epaper_drv_set_pixel(uint16_t x, uint16_t y, bool black)
{
    if (!s_ready || s_buf == NULL || x >= EPAPER_WIDTH || y >= EPAPER_HEIGHT) {
        return ESP_ERR_INVALID_ARG;
    }

    const uint16_t index = y * EPD_ROW_BYTES + (x >> 3);
    const uint8_t bit = 7 - (x & 0x07);
    if (black) {
        s_buf[index] &= ~(1u << bit);
    } else {
        s_buf[index] |= (1u << bit);
    }
    return ESP_OK;
}

/* Aplica o transform de orientação único: lógico → painel nativo. */
static void build_tx_from_logical(void)
{
    memset(s_tx_buf, 0xFF, EPAPER_BUF_BYTES);

    for (uint16_t ly = 0; ly < EPAPER_HEIGHT; ly++) {
        const uint8_t *row = &s_buf[ly * EPD_ROW_BYTES];
        for (uint16_t lx = 0; lx < EPAPER_WIDTH; lx++) {
            bool black = (row[lx >> 3] & (1u << (7 - (lx & 0x07)))) == 0;
            if (!black) {
                continue;
            }

            uint16_t px = EPD_SWAP_XY ? ly : lx;
            uint16_t py = EPD_SWAP_XY ? lx : ly;
#if EPD_MIRROR_X
            px = (EPAPER_WIDTH - 1) - px;
#endif
#if EPD_MIRROR_Y
            py = (EPAPER_HEIGHT - 1) - py;
#endif
            s_tx_buf[py * EPD_ROW_BYTES + (px >> 3)] &= ~(1u << (7 - (px & 0x07)));
        }
    }
}

esp_err_t epaper_drv_refresh_full(void)
{
    if (!s_ready || s_buf == NULL || s_tx_buf == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    build_tx_from_logical();

    panel_set_ram_window();
    send_command(0x24);
    send_data_buf(s_tx_buf, EPAPER_BUF_BYTES);
    turn_on_display();
    return ESP_OK;
}

static void fill_rect(int x, int y, int w, int h, bool black)
{
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            epaper_drv_set_pixel((uint16_t)(x + i), (uint16_t)(y + j), black);
        }
    }
}

esp_err_t epaper_drv_draw_calibration(void)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    epaper_drv_fill(true);

    /* Moldura para enxergar bordas/recorte */
    fill_rect(0, 0, EPAPER_WIDTH, 2, true);
    fill_rect(0, EPAPER_HEIGHT - 2, EPAPER_WIDTH, 2, true);
    fill_rect(0, 0, 2, EPAPER_HEIGHT, true);
    fill_rect(EPAPER_WIDTH - 2, 0, 2, EPAPER_HEIGHT, true);

    /* Marcador de ORIGEM: bloco sólido no canto superior esquerdo */
    fill_rect(6, 6, 28, 28, true);

    /* Seta apontando para a DIREITA no topo (haste + ponta) */
    fill_rect(70, 18, 70, 4, true);
    for (int k = 0; k < 12; k++) {
        fill_rect(140 - k, 20 - k, 2, 1 + 2 * k, true);
    }

    /* Letra "F" grande e assimétrica (detecta espelho-X, espelho-Y e rotação) */
    int fx = 80, fy = 70, fw = 12, fh = 90;
    fill_rect(fx, fy, fw, fh, true);          /* haste vertical */
    fill_rect(fx, fy, 60, fw, true);          /* braço superior */
    fill_rect(fx, fy + 38, 44, fw, true);     /* braço do meio */

    return epaper_drv_refresh_full();
}

esp_err_t epaper_drv_deinit(void)
{
    if (!s_ready) {
        return ESP_OK;
    }
    if (s_spi) {
        spi_bus_remove_device(s_spi);
        s_spi = NULL;
    }
    spi_bus_free(BOARD_EPD_SPI_HOST);
    free(s_tx_buf);
    free(s_buf);
    s_tx_buf = NULL;
    s_buf = NULL;
    s_ready = false;
    return ESP_OK;
}
