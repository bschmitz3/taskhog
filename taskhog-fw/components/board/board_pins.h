#pragma once

#include "driver/gpio.h"

/* Waveshare ESP32-S3 1.54" V2 — validado M0-T3 (I2C) / ref. exemplo 08_Audio_Test. */

/* I2C principal */
#define BOARD_I2C_SDA_GPIO  47
#define BOARD_I2C_SCL_GPIO  48
#define BOARD_I2C_FREQ_HZ   100000

#define BOARD_I2C_ADDR_ES8311    0x18
#define BOARD_I2C_ADDR_PCF85063  0x51
#define BOARD_I2C_ADDR_SHTC3     0x70

/* Power rails (active LOW = ON para EPD/Audio) */
#define BOARD_EPD_PWR_GPIO    GPIO_NUM_6
#define BOARD_AUDIO_PWR_GPIO  GPIO_NUM_42
#define BOARD_VBAT_PWR_GPIO   GPIO_NUM_17

/* ES8311 I2S — board_cfg.txt S3_ePaper_1_54 */
#define BOARD_I2S_MCLK_GPIO   GPIO_NUM_14
#define BOARD_I2S_BCLK_GPIO   GPIO_NUM_15
#define BOARD_I2S_WS_GPIO     GPIO_NUM_38
#define BOARD_I2S_DOUT_GPIO   GPIO_NUM_45
#define BOARD_I2S_DIN_GPIO    GPIO_NUM_16
#define BOARD_PA_GPIO         GPIO_NUM_46

/* microSD — SDMMC 1-line (04_SD_Card Waveshare) */
#define BOARD_SD_CLK_GPIO     GPIO_NUM_39
#define BOARD_SD_CMD_GPIO     GPIO_NUM_41
#define BOARD_SD_D0_GPIO      GPIO_NUM_40
#define BOARD_SD_MOUNT_POINT  "/sdcard"

/* e-Paper SSD1681 1.54" 200×200 (Waveshare V2) */
#define BOARD_EPD_WIDTH       200
#define BOARD_EPD_HEIGHT      200
#define BOARD_EPD_BUF_BYTES   (BOARD_EPD_WIDTH * BOARD_EPD_HEIGHT / 8)
#define BOARD_EPD_SPI_HOST    SPI2_HOST

#define BOARD_EPD_DC_GPIO     GPIO_NUM_10
#define BOARD_EPD_CS_GPIO     GPIO_NUM_11
#define BOARD_EPD_SCK_GPIO    GPIO_NUM_12
#define BOARD_EPD_MOSI_GPIO   GPIO_NUM_13
#define BOARD_EPD_RST_GPIO    GPIO_NUM_9
#define BOARD_EPD_BUSY_GPIO   GPIO_NUM_8

/* Botões físicos (Waveshare V2 — validar M0-T9) */
#define BOARD_BOOT_BTN_GPIO   GPIO_NUM_0   /* silk: BOOT */
#define BOARD_PWR_BTN_GPIO    GPIO_NUM_18  /* silk: PWR  */

/** Mapeamento produto (pós M0-T9, ref. demo Waveshare). */
#define BOARD_REC_BTN_GPIO    BOARD_BOOT_BTN_GPIO
/** PWR = BAT_KEY — desliga placa; não mapear como NAV. */
#define BOARD_NAV_BTN_GPIO    BOARD_PWR_BTN_GPIO

/* Bateria */
#define BOARD_BAT_ADC_GPIO    GPIO_NUM_4

/* RTC PCF85063 — INT open-drain, active LOW */
#define BOARD_RTC_INT_GPIO    GPIO_NUM_5
