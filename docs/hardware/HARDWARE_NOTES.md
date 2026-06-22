# Taskhog — Hardware Notes (bring-up)

> Documento vivo — preenchido durante M0. Atualizado conforme testes físicos.

**Placa:** Waveshare ESP32-S3 1.54" e-Paper **V2** (sem touch)  
**Última atualização:** 2026-06-20 (M0 completo)

## Status M0 — ✅ COMPLETO

| Gate | Item | Status |
|---|---|---|
| T1/T2 | Toolchain + chip 8MB/PSRAM | ✅ |
| T3 | I2C scan ES8311/RTC/SHTC3 | ✅ |
| T4 | Áudio WAV no SD | ✅ |
| T5 | e-Paper hello world | ✅ |
| T6 | SD CRUD | ✅ |
| T7 | Bateria BAT_ADC | ✅ |
| T8 | RTC set/get + alarme | ✅ |
| T9 | Botões BOOT/PWR | ✅ |

Gates de validação permanecem em `main/m0_*_gate.c` para re-teste manual; removidos do boot em 2026-06-20.

---

## Identificação confirmada (M0-T1/T2)

| Item | Valor medido |
|---|---|
| Chip | **ESP32-S3-PICO-1** (revision v0.2) |
| Flash | **8 MB** (GD, embedded) |
| PSRAM | **8 MB** (AP_3v3, embedded) |
| USB | USB-Serial/JTAG (`/dev/cu.usbmodem13301`) |
| MAC | `14:c1:9f:d4:58:4c` |
| ESP-IDF | v5.2.2 |
| Firmware scaffold | ✅ flash OK |

## Porta serial (Mac)

```text
/dev/cu.usbmodem13301
```

```bash
get_idf
cd taskhog-fw
idf.py -p /dev/cu.usbmodem13301 flash monitor
```

## GPIOs V2 (fonte: exemplo Waveshare 08_Audio_Test / user_config.h)

| Função | GPIO | Status |
|---|---|---|
| I2C SDA | **47** | ✅ M0-T3 |
| I2C SCL | **48** | ✅ M0-T3 |
| I2S MCLK/BCLK/WS/DOUT/DIN | 14/15/38/45/16 | ✅ M0-T4 |
| Audio PWR | 42 (active LOW) | ✅ M0-T4 |
| PA | 46 (strapping) | ✅ M0-T4 |
| EPD PWR | 6 | ✅ M0-T5 |
| EPD DC/CS/SCK/MOSI | 10/11/12/13 | ✅ M0-T5 |
| EPD RST/BUSY | 9/8 | ✅ M0-T5 |
| SD CLK/CMD/D0 | 39/41/40 (SDMMC 1-line) | ✅ M0-T6 |
| BAT_ADC | 4 | ✅ M0-T7 |
| RTC_INT | 5 | ✅ M0-T8 |
| BOOT / PWR btn | 0 / 18 (active LOW) | ✅ M0-T9 |
| VBAT_PWR / BAT_Control | 17 | ✅ latch HIGH no boot |

## I2C scan (M0-T3) ✅

Barramento: **SDA=GPIO47, SCL=GPIO48** @ 100 kHz

| Device | Addr esperado | Detectado |
|---|---|---|
| ES8311 | 0x18 | ✅ |
| PCF85063 (RTC) | 0x51 | ✅ |
| SHTC3 | 0x70 | ✅ |

Log de referência (2026-06-20):

```text
I (725) i2c_bus:   0x18 — ES8311 (codec)
I (734) i2c_bus:   0x51 — PCF85063 (RTC)
I (739) i2c_bus:   0x70 — SHTC3 (temp/hum)
I (740) i2c_bus: Total: 3 dispositivo(s)
I (741) i2c_bus: Gate M0-T3: 0x18 OK
I (741) i2c_bus: Gate M0-T3: 0x51 OK
I (742) i2c_bus: Gate M0-T3: 0x70 OK
```

## microSD (M0-T6) ✅

| Item | Valor |
|---|---|
| Interface | SDMMC 1-line (CLK=39, CMD=41, D0=40) |
| Formato | **FAT32** único, MBR, volume `TASKHOG` |
| Capacidade | ~62 GB (cartão CBADS) |
| Mount ESP | `/sdcard` |
| Gate | `m0_sdcard_gate_run()` — CREATE/READ/UPDATE/DELETE |

**Validado em 2026-06-20** — gate `m0t6.bin` (nomes **8.3**; `FATFS_LFN_NONE` rejeita nomes longos).

Log de referência:

```text
I (781) m0_sdcard: Gate M0-T6: CREATE+WRITE 21 bytes
I (783) m0_sdcard: Gate M0-T6: READ OK
I (791) m0_sdcard: Gate M0-T6: DELETE OK
I (804) m0_sdcard: Gate M0-T6: CRUD completo
```

**Problema resolvido (2026-06-20):** cartão vinha com partições Raspberry Pi (`bootfs` + Linux). O ESP gravava na partição FAT pequena; no Mac o arquivo não aparecia na partição visível. Reformatado como **FAT32 único**.

Estrutura criada no cartão:

```text
/Volumes/TASKHOG/
├── config/
├── queue/
├── sent/
├── journal/
└── logs/
```

**Formatar no Mac (se precisar de novo):**

```bash
# Confirmar disco (cuidado — apaga tudo!)
diskutil list external
diskutil unmountDisk force /dev/diskX
diskutil eraseDisk FAT32 TASKHOG MBRFormat /dev/diskX
```

## Gate de áudio (M0-T4) ✅

Firmware grava `/sdcard/m0_gate.wav` (8 s, 16 kHz mono) no boot.

| Critério | Pass? |
|---|---|
| G1 WAV abre no PC | ✅ Audacity/VLC |
| G2 16 kHz / 16-bit / mono | ✅ |
| G3 Voz humana reconhecível | ✅ (fala a partir de ~4,5 s no teste) |
| G4 SNR aceitável | ✅ |
| G5 Nível consistente | ⏳ (revalidar em gravações sucessivas no M1) |

**Validado em 2026-06-20** — cartão FAT32 `TASKHOG`, 257024 bytes PCM, pipeline ES8311+I2S+SDMMC OK.

## e-Paper (M0-T5) ✅

Driver **SSD1681** 200×200, refresh total.

| Critério | Pass? |
|---|---|
| Painel inicializa sem erro | ✅ |
| Texto / moldura visível | ✅ |
| Orientação correta (não espelhada) | ✅ |

**Validado em 2026-06-20** · **revisado 2026-06-22** (reconstrução da UI).

### Pinos e SPI
- DC/CS/SCK/MOSI = **10/11/12/13**, RST/BUSY = **9/8**, PWR(EPD3V3_EN) = **6**.
- SPI2 @ **20 MHz** (reduzido de 40 MHz na reconstrução; sem mudança de comportamento).

### Orientação — resolvido de vez (2026-06-22)
O painel deste módulo precisa de **espelhamento vertical** vs. o endereçamento canônico.
- Init do controlador é **100% canônico**: data entry mode `0x11=0x03` (X inc, Y inc), janelas/contadores RAM começando em 0. **Nenhum hack de orientação nos registradores** (a abordagem antiga de inverter janela X + entry `0x00` causava espelhamentos imprevisíveis).
- Toda correção vive em **um único transform de software** no flush (`build_tx_from_logical`), controlado por `components/ui/epaper_cfg.h`:
  - `EPD_MIRROR_X = 0`, **`EPD_MIRROR_Y = 1`**, `EPD_SWAP_XY = 0`.
- Calibração feita com padrão assimétrico (bloco no canto sup-esq + "F" + seta), via flag `EPD_CALIBRATION` no `epaper_cfg.h` (deixar `0` em produção).

### Waveform / LUT
- O **OTP de waveform deste painel é não-confiável**: usar `0x22=0xF7` (full from OTP) **não atualiza** o painel (mantém imagem antiga).
- Solução: **LUT full-refresh custom** (`WF_FULL_1IN54`) carregada via `0x32`, power-on do booster no init (`0x22=0xB1`) e update com `0x22=0xC7`. Esse é o caminho comprovado que atualiza o painel.

## Botões (M0-T9) ✅

Placa Waveshare V2 **sem touch** — 2 botões físicos (ref. `button_bsp` Waveshare):

| Silk | GPIO | Active | Papel Taskhog |
|---|---|---|---|
| BOOT | **0** | LOW | **REC** (push-to-talk) — **usar só este para gravar** |
| PWR | **18** | LOW | **Desliga a placa** (BAT_KEY) — **não usar como botão NAV** |

> **Importante (Waveshare V2):** GPIO18 = `BAT_KEY` / botão PWR — pressionar **desliga** alimentação e apaga LEDs. GPIO17 = `BAT_Control` — firmware mantém **HIGH** para segurar a alimentação na bateria. Demo Waveshare: `08_BATT_PWR_Test`.

**Validado em 2026-06-20** — gate `m0_buttons_gate_run()`, PRESSED/RELEASED em BOOT e PWR.

Log de referência:

```text
I (3119) m0_buttons: Gate M0-T9: PWR GPIO18 PRESSED
I (3500) m0_buttons: Gate M0-T9: BOOT GPIO0 PRESSED
...
```

Driver: `components/board/board_buttons.c` · pinos `BOARD_REC_BTN_GPIO` / `BOARD_NAV_BTN_GPIO`.

## RTC PCF85063 (M0-T8) ✅

| Item | Valor |
|---|---|
| I2C | 0x51 @ SDA=47, SCL=48 |
| INT | GPIO5 (open-drain, active LOW) |
| CTRL2 | AIE=bit7 (`0x80`), AF=bit6 (`0x40`) — mapa PCF85063A |
| Gate | `m0_rtc_gate_run()` — set/get, +3 s drift, alarme INT/AF |

Firmware: `components/rtc/pcf85063.c` — `rtc_get/set`, `rtc_check_os()`, alarme.

**Validado em 2026-06-20:**

```text
I (3813) m0_rtc: Gate M0-T8: relógio avançando OK
I (3821) m0_rtc: Gate M0-T8: CTRL2 após enable = 0x80 (AIE=1 AF=0)
I (7982) m0_rtc: Gate M0-T8: alarme INT=LOW AF=SET
I (7990) m0_bringup: M0 COMPLETE — T6+T8 OK
```

Opcional: desligar USB e religar — hora persiste (~2026-06-20 17:45:xx).

## Bateria (M0-T7) ✅

| Item | Valor |
|---|---|
| ADC | GPIO4 = ADC1_CH3 |
| Divisor | R38/R21 = 200k/200k → **ratio 2.0** |
| VBAT_PWR | GPIO17 HIGH = rail medição ON |
| Capacidade | Li-Po 1S **1000 mAh** |

**Validado em 2026-06-20** — bateria conectada, USB + VBAT:

```text
I (974)  m0_battery: Gate M0-T7 [1/5]: 3680 mV (46%) valid=sim
I (3098) m0_battery: Gate M0-T7 [5/5]: 3680 mV (46%) valid=sim
I (3099) m0_battery: Resumo M0-T7: min=3676 mV max=3686 mV spread=10 mV
```

| Métrica | Resultado |
|---|---|
| Tensão | ~**3680 mV** (~46%) |
| Estabilidade | spread **10 mV** (5 amostras) |
| Ratio 2.0 | coerente c/ curva Li-Po (3700 mV → 50%) |

Driver: `components/power/battery.c` — `battery_read()`, curva Li-Po spec §7.2.

**Calibração fina (opcional):** multímetro em VBAT vs log; ajustar `BOARD_BAT_DIVIDER_RATIO` em `battery.h` se divergir >50 mV.

## Divergências vs doc interna / V1

| Item | Doc antiga (PRD §5.4) | V2 real (Waveshare) |
|---|---|---|
| I2S WS | GPIO17 | **GPIO38** |
| I2S DOUT | GPIO16 | **GPIO45** |
| I2S DIN | GPIO18 | **GPIO16** |
| SD interface | SPI (CS TBD) | **SDMMC 1-line** (39/41/40) |
| EPD pinos | 21/11/13/12/10/8 | **8/9/10/11/12/13** |
| Audio power | PA_EN=42 | **Audio_PWR=42** (rail), PA=46 |
