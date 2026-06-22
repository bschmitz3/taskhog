# Documentação única — Waveshare ESP32-S3 1.54inch e-Paper AIoT Development Board V2 (versão sem touch)

> **Placa alvo:** ESP32-S3 1.54inch e-Paper AIoT Development Board V2 **sem touch**, display e-Paper 1.54" 200 × 200, preto/branco, Wi‑Fi + BLE, sem bateria instalada de fábrica.  
> **Contexto do projeto:** a sua unidade está com microSD de 64 GB e receberá uma bateria Li‑Po/Li‑ion de aproximadamente **500 mAh**.  
> **Versão da documentação:** 2026-06-17.  
> **Fontes usadas:** datasheet do display 1.54inch e-Paper V2, datasheet ESP32-S3, TRM ESP32-S3, datasheet SHTC3, datasheet PCF85063A, captura da documentação Waveshare e esquema `ESP32-S3-Touch-ePaper-1.54-Schematic.pdf` usado apenas como referência da família de placas. Como a sua unidade é **sem touch**, todos os recursos/pinos de touch foram removidos da documentação-alvo ou marcados como não aplicáveis.

---

## 1. Resumo executivo

Esta placa é uma plataforma compacta de desenvolvimento baseada no **ESP32-S3-PICO-1-N8R8**, combinando:

- MCU dual-core **Xtensa LX7** até 240 MHz.
- Conectividade **Wi‑Fi 2.4 GHz 802.11 b/g/n** e **Bluetooth 5 Low Energy**.
- **8 MB de Flash** e **8 MB de PSRAM** integrados no módulo `ESP32-S3-PICO-1-N8R8`.
- Display **e-Paper 1.54" preto/branco**, resolução **200 × 200 px**, biestável e com suporte a atualização parcial.
- Sensor ambiental **SHTC3** para temperatura e umidade.
- RTC **PCF85063ATL** com cristal de 32.768 kHz, alarme/timer e interrupção.
- Slot **TF/microSD** para armazenamento externo.
- Subsistema de áudio com codec **ES8311**, microfone e amplificador/speaker, conforme esquema/documentação da família da placa — validar fisicamente se todos os componentes estão povoados na sua unidade.
- Conector USB‑C para alimentação, programação e serial/JTAG.
- Circuito de alimentação com carregador/power path para bateria de lítio, apesar da variante comprada vir sem bateria instalada.
- Botões físicos para **BOOT** e **PWR/RESET/KEY**, conforme versão de placa.
- Headers/expansão para GPIOs, alimentação, bateria e periféricos.

O ponto mais importante para o projeto é que esta não é apenas uma “placa com e-paper”: a versão sem touch ainda traz MCU potente, sensores, RTC, armazenamento e, conforme a família de hardware, recursos de áudio. Isso permite criar dispositivos portáteis com tela sempre visível, baixo consumo em repouso, logs no microSD e modos de captura/alerta usando áudio e sensores.

---

## 2. Identificação da placa

### 2.1 Nome comercial

**ESP32-S3 1.54inch e-Paper AIoT Development Board V2**.

Na página/captura da Waveshare aparecem variantes semelhantes:

| Variante | Touch | Bateria incluída |
|---|---:|---:|
| ESP32-S3-ePaper-1.54 | Não | Não |
| ESP32-S3-ePaper-1.54-B | Não | Sim |
| ESP32-S3-Touch-ePaper-1.54 | Sim | Não |
| ESP32-S3-Touch-ePaper-1.54-B | Sim | Sim |

**Versão alvo desta documentação:** `ESP32-S3-ePaper-1.54` / `ESP32-S3-ePaper-1.54-N`, isto é, **sem touch**. As variantes `Touch-ePaper` aparecem apenas como referência comparativa e não devem ser usadas para definir UX, firmware ou pinos do seu projeto.

A sua unidade foi descrita como **sem touch** e **sem bateria**, e você pretende instalar uma bateria de **500 mAh**.

### 2.2 SKU observado na documentação Waveshare

A captura da documentação lista SKUs relacionados:

| SKU | Produto |
|---:|---|
| 33298 | ESP32-S3-ePaper-1.54 |
| 33299 | ESP32-S3-ePaper-1.54-N |
| 34271 | ESP32-S3-Touch-ePaper-1.54 |
| 34212 | ESP32-S3-Touch-ePaper-1.54-N |

> **Atenção importante:** a sua placa é a variante **sem touch** (`ESP32-S3-ePaper-1.54`). O PDF de esquema anexado está nomeado como `ESP32-S3-Touch-ePaper-1.54-Schematic.pdf`, então ele foi usado apenas para identificar a arquitetura elétrica compartilhada da família. Tudo que é exclusivo de touch screen foi removido da documentação-alvo. Para documentação final do projeto, valide no hardware físico com `gpio`, `i2cdetect`, `esptool.py`, teste de SD e teste de áudio.

---

## 3. Arquitetura geral

```text
USB-C / VBUS
   │
   ├── Circuito de carga/power path ETA6098 ─── VBAT / conector bateria
   │                                      │
   │                                      └── VSYS
   │
   └── Regulador MP1605 ─── 3V3 / VDD3V3 / EPD3V3 / A3V3

ESP32-S3-PICO-1-N8R8
   ├── Wi‑Fi 2.4 GHz + BLE 5 via antena onboard
   ├── USB Serial/JTAG em GPIO19/GPIO20
   ├── e-Paper 1.54" via SPI + controle de energia EPD3V3_EN
   ├── Barramento I2C principal: SHTC3, RTC PCF85063 e ES8311
   ├── microSD via SPI/SD signals
   ├── Áudio I2S: ES8311 + microfone + amplificador NS4150B
   ├── Botões BOOT/PWR/KEY
   └── Headers de expansão GPIO/alimentação
```

---

## 4. MCU principal — ESP32-S3-PICO-1-N8R8

### 4.1 Identificação

O esquema identifica o módulo principal como:

```text
U9: ESP32-S3-PICO-1-N8R8
```

Este módulo integra o SoC ESP32-S3 com Flash e PSRAM no próprio pacote. Pela nomenclatura `N8R8`, a configuração esperada é:

- **N8:** 8 MB Flash.
- **R8:** 8 MB PSRAM.

### 4.2 CPU e memória

| Recurso | Especificação |
|---|---|
| CPU | Dual-core Xtensa 32-bit LX7 |
| Frequência | Até 240 MHz |
| FPU | Single precision floating point |
| Instruções vetoriais/SIMD | Sim, úteis para DSP/IA embarcada simples |
| SRAM interna | 512 KB |
| ROM | 384 KB |
| RTC SRAM | 16 KB total, dividida em FAST/SLOW |
| eFuse | 4096 bits, com parte disponível ao usuário |
| Flash onboard | 8 MB, conforme módulo N8 |
| PSRAM onboard | 8 MB, conforme módulo R8 |

### 4.3 Conectividade

| Interface | Especificação |
|---|---|
| Wi‑Fi | 2.4 GHz, IEEE 802.11 b/g/n |
| Largura de canal | 20 MHz e 40 MHz |
| Taxa nominal máxima | Até 150 Mbps em 1T1R |
| Bluetooth | Bluetooth 5 Low Energy |
| BLE PHY | 125 kbps, 500 kbps, 1 Mbps, 2 Mbps |
| Coexistência Wi‑Fi/BLE | Interna, compartilhando antena |
| Antena | Antena onboard no PCB, no esquema como `J11` / `CA-C03` |

### 4.4 Periféricos relevantes do ESP32-S3 para esta placa

- USB Serial/JTAG.
- USB OTG full-speed.
- SPI0/1 para flash/PSRAM internos.
- SPI2/SPI3 gerais para display e periféricos.
- I2C para sensores, RTC e codec de áudio.
- I2S para áudio.
- SD/MMC host, embora nesta placa o microSD pareça roteado no estilo SPI/linhas dedicadas do slot TF.
- ADC para leitura de bateria via divisor.
- ULP RISC‑V/FSM para cenários de baixo consumo.
- RMT, LEDC, timers e watchdogs.
- Criptografia por hardware: AES, SHA, RSA, HMAC, secure boot e flash encryption.

---

## 5. Display e-Paper 1.54" V2

### 5.1 Descrição

O display é um painel **Active Matrix Electrophoretic Display (AMEPD)** de 1.54", com área ativa de **200 × 200 pixels** e capacidade **1-bit preto/branco**.

### 5.2 Especificações mecânicas do painel

| Parâmetro | Valor |
|---|---:|
| Tamanho nominal | 1.54" |
| Resolução | 200 × 200 px |
| Densidade | ~188 DPI |
| Área ativa | 27.00 × 27.00 mm |
| Pixel pitch | 0.135 × 0.135 mm |
| Configuração de pixel | Quadrado |
| Dimensão externa do painel | 37.32 × 31.80 × 1.05 mm |
| Peso | 2.1 ± 0.2 g |

### 5.3 Características funcionais

- Suporte a **partial refresh**.
- Alto contraste e alta refletância.
- Ângulo de visão muito amplo.
- Consumo ultrabaixo.
- Modo reflexivo puro, sem backlight.
- Display **biestável**: mantém a imagem sem consumo contínuo de energia.
- Suporte a orientações landscape/portrait.
- Superfície com hard-coat antiglare.
- Modo deep sleep de corrente muito baixa.
- RAM de display no controlador.
- Sensor de temperatura interno no controlador do painel.
- Detecção de baixa tensão e detecção de tensão alta pronta para acionamento do display.
- Waveform em OTP interno.
- Interface SPI.

### 5.4 Características ópticas

| Parâmetro | Valor típico |
|---|---:|
| Refletância branca | 35% típico, 30% mínimo |
| Contraste indoor | 10:1 típico |
| Vida do painel | ~5 anos em 0 °C a 50 °C, com observações de umidade e atualização diária |

> O datasheet observa que a qualidade de pixels por 5 anos não é garantida se a umidade ficar abaixo de 45% RH ou acima de 70% RH, e recomenda ao menos uma atualização por dia para esse critério de vida útil.

### 5.5 Sequência típica de operação do e-Paper

Fluxo recomendado para atualização:

1. Aplicar alimentação `VCI`/`EPD3V3`.
2. Resetar o driver do e-Paper.
3. Configurar VCOM, VGH e VGL.
4. Ligar clock/oscilador, DC/DC e reguladores internos do display.
5. Definir tamanho/resolução e waveform.
6. Limpar display, se necessário.
7. Carregar os dados de imagem.
8. Executar atualização do display.
9. Desligar os blocos analógicos/oscilador/DC/DC.
10. Entrar em deep sleep/power down.

### 5.6 Interface do painel no conector/FPC

O painel possui 24 pinos principais:

| Pino | Sinal | Função |
|---:|---|---|
| 1 | NC | Não conectar |
| 2 | GDR | Controle de gate drive do MOSFET N-channel |
| 3 | RESE | Entrada de sense de corrente |
| 4 | NC | Não conectar |
| 5 | VSH2 | Tensão positiva de source drive |
| 6 | TSCL | I2C clock para sensor de temperatura externo/interno |
| 7 | TSDA | I2C data para sensor de temperatura externo/interno |
| 8 | BS1/BS | Seleção de barramento |
| 9 | BUSY | Saída busy do controlador |
| 10 | RES#/RST | Reset do display |
| 11 | D/C# | Seleção data/command |
| 12 | CS# | Chip select SPI |
| 13 | SCL/SCLK | Clock SPI |
| 14 | SDA/SDI | Dados SPI/MOSI |
| 15 | VDDIO | Alimentação lógica da interface |
| 16 | VCI | Alimentação do chip |
| 17 | VSS | GND |
| 18 | VDD | Alimentação core lógica |
| 19 | VPP | Alimentação para programação OTP |
| 20 | VSH1 | Tensão positiva de source drive |
| 21 | VGH/PREVGH | Tensão positiva de gate drive |
| 22 | VSL | Tensão negativa de source drive |
| 23 | VGL/PREVGL | Tensão negativa de gate drive |
| 24 | VCOM | Tensão comum do painel |

### 5.7 Mapeamento do e-Paper no ESP32-S3 segundo o esquema

| Função e-Paper | GPIO ESP32-S3 | Observação |
|---|---:|---|
| `EPD_BUSY` | GPIO21 | Entrada de busy do display |
| `EPD_RST` | GPIO11 | Reset do display |
| `EPD_D/C` | GPIO13 | Data/Command |
| `EPD_CS` | GPIO12 | Chip Select |
| `EPD_SCLK` | GPIO10 | Clock SPI |
| `EPD_SDI` | GPIO8 | MOSI/SPI data in do display |
| `EPD3V3_EN` | GPIO6 | Habilita alimentação do e-Paper |

### 5.8 Buffer de tela

Como o display tem 200 × 200 px e é 1 bit por pixel:

```text
200 × 200 = 40.000 pixels
40.000 / 8 = 5.000 bytes
```

Portanto, uma imagem full-screen preto/branco ocupa cerca de **5 KB** em framebuffer bruto. Isso é pequeno para a RAM do ESP32-S3 e também para o microSD.

### 5.9 Implicações para UI

- Evitar animações contínuas: e-Paper não é display de alta taxa de atualização.
- Usar telas estáticas e estados bem definidos.
- Preferir ícones em alto contraste, sem antialiasing excessivo.
- Para fontes, usar tamanhos que respeitem 200 × 200 px.
- Para economia de bateria, atualizar a tela apenas quando o estado mudar.
- Usar partial refresh com moderação para evitar ghosting; intercalar refresh completo quando necessário.

---

## 6. Sensor ambiental SHTC3

### 6.1 Função

O **SHTC3** é um sensor digital de **temperatura e umidade** projetado para dispositivos móveis e alimentados por bateria.

### 6.2 Especificações principais

| Parâmetro | Valor |
|---|---:|
| Alimentação | 1.62 V a 3.6 V |
| Umidade medida | 0 a 100 %RH |
| Temperatura medida | -40 °C a +125 °C |
| Precisão típica de umidade | ±2 %RH |
| Precisão típica de temperatura | ±0.2 °C |
| Resolução | 0.01 %RH / 0.01 °C |
| Tempo de power-up/medição | ~1 ms |
| Interface | I2C Normal/Fast/Fast Mode Plus |
| Clock I2C | 0 a 1 MHz |
| Endereço I2C | `0x70` |
| Encapsulamento | DFN 2 × 2 × 0.75 mm |

### 6.3 Pinagem SHTC3

| Pino | Sinal | Função |
|---:|---|---|
| 1 | VDD | Alimentação |
| 2 | SCL | Clock I2C |
| 3 | SDA | Dados I2C |
| 4 | VSS | GND |
| EP | GND | Pad exposto conectado ao GND |

### 6.4 Conexão na placa

No esquema:

| SHTC3 | Placa |
|---|---|
| VDD | 3V3 |
| SCL | SCL |
| SDA | SDA |
| VSS/EP | GND |

### 6.5 Comandos úteis SHTC3

| Ação | Código |
|---|---:|
| Endereço I2C | `0x70` |
| Medição normal, clock stretching, temperatura primeiro | `0x7CA2` |
| Medição normal, clock stretching, umidade primeiro | `0x5C24` |
| Medição normal, sem clock stretching, temperatura primeiro | `0x7866` |
| Medição normal, sem clock stretching, umidade primeiro | `0x58E0` |
| Medição low-power, clock stretching, temperatura primeiro | `0x6458` |
| Medição low-power, clock stretching, umidade primeiro | `0x44DE` |
| Medição low-power, sem clock stretching, temperatura primeiro | `0x609C` |
| Medição low-power, sem clock stretching, umidade primeiro | `0x401A` |
| Soft reset | `0x805D` |
| Read ID | `0xEFC8` |

### 6.6 Boas práticas de uso

- Acordar o sensor, medir, ler e colocá-lo em sleep novamente.
- Para bateria, preferir comandos low-power quando a precisão máxima não for necessária.
- Evitar atividade no barramento I2C durante a medição, especialmente quando clock stretching não estiver habilitado.
- Usar CRC quando os dados forem críticos.
- Manter o sensor longe de fontes de calor da placa, principalmente reguladores, bateria e ESP32 durante Wi‑Fi ativo.

---

## 7. RTC PCF85063ATL

### 7.1 Função

O **PCF85063A** é um RTC/calendário CMOS de baixo consumo com interface I2C, oscilador de 32.768 kHz, alarme, timer e saída de interrupção.

### 7.2 Especificações principais

| Parâmetro | Valor |
|---|---:|
| Interface | I2C |
| Clock I2C máximo | 400 kbit/s |
| Tensão de operação do relógio | 0.9 V a 5.5 V |
| Tensão para I2C ativo a 400 kHz | 1.8 V a 5.5 V |
| Corrente típica | ~220 nA em 3.3 V, 25 °C, interface inativa |
| Cristal | 32.768 kHz |
| Funções | ano, mês, dia, weekday, horas, minutos, segundos |
| Recursos extras | alarme, countdown timer, interrupção, ajuste fino por offset, detecção de oscilador parado |
| Encapsulamento da placa | PCF85063ATL, DFN2626-10 |

### 7.3 Pinagem PCF85063ATL

| Pino | Sinal | Função |
|---:|---|---|
| 1 | OSCI | Entrada do cristal |
| 2 | OSCO | Saída do cristal |
| 3 | CLKOE | Habilitação CLKOUT |
| 4 | INT | Saída de interrupção open-drain |
| 5 | VSS | GND |
| 6 | SDA | I2C data |
| 7 | SCL | I2C clock |
| 8 | n.c. | Não conectado |
| 9 | CLKOUT | Saída de clock push-pull |
| 10 | VDD | Alimentação |
| 11 | EP | Pad exposto |

### 7.4 Conexão na placa

| RTC | Placa |
|---|---|
| VDD | 3V3 |
| VSS | GND |
| SDA | `RTC_SDA` / barramento SDA |
| SCL | `RTC_SCL` / barramento SCL |
| INT | `RTC_INT` → GPIO5 |
| Cristal | Y2, 32.768 kHz |

### 7.5 Registros e comportamento

- O RTC possui 18 registradores de 8 bits com auto-incremento.
- Registradores `00h` e `01h`: controle/status.
- Registrador `02h`: offset para ajuste fino do clock.
- Registrador `03h`: byte RAM livre.
- Registradores `04h` a `0Ah`: contadores de tempo/data.
- Registradores `0Bh` a `0Fh`: alarmes.
- Registradores `10h` e `11h`: timer.
- Campos de tempo são BCD.
- O bit `OS` em segundos indica parada/interrupção do oscilador e deve ser verificado após boot.

### 7.6 Uso recomendado no projeto

- Usar o RTC para timestamp confiável de logs no microSD.
- Usar `RTC_INT` em GPIO5 para acordar o ESP32 em intervalos definidos.
- Após boot, verificar o bit `OS`; se estiver setado, a hora pode não ser confiável.
- Sincronizar RTC via NTP quando houver Wi‑Fi.
- Em modo offline, usar o RTC como fonte principal de tempo.

---

## 8. Armazenamento — microSD/TF Card

### 8.1 Situação da sua unidade

Você informou que a placa está com um cartão de memória de **64 GB**.

### 8.2 Interface no esquema

O slot TF aparece como `TF1`, com sinais:

| Sinal | GPIO indicado no esquema | Observação |
|---|---:|---|
| `SD_CLK` | GPIO41 | Clock do cartão |
| `SD_MISO` | GPIO40 | D0/MISO |
| `SD_MOSI` | GPIO39 | CMD/MOSI |
| `SD_CS` | Não claramente populado; resistor `R41` marcado como NC | Pode indicar uso sem CS dedicado ou variação de montagem |
| `SD_D1` | Sinal presente no slot, sem GPIO claramente associado |
| `SD_D2` | Sinal presente no slot, sem GPIO claramente associado |
| `CD` | Card detect no slot, não claramente usado |

### 8.3 Implicação prática

A documentação/esquema sugere que o cartão é exposto principalmente por **linhas SPI-like** (`CLK`, `MISO`, `MOSI`) e possivelmente sem `CS` populado na variante analisada. Antes de definir a arquitetura de storage do projeto, é importante validar no firmware exemplo da Waveshare qual driver eles usam para o TF card.

### 8.4 Recomendações para microSD 64 GB

- Formatar em **FAT32** se quiser máxima compatibilidade com bibliotecas Arduino/ESP-IDF. Cartões de 64 GB normalmente vêm em exFAT, que pode exigir suporte específico.
- Para ESP-IDF, considerar FATFS com wear leveling quando usando flash interna; para SD, usar VFS FAT sobre SD/MMC/SDSPI.
- Definir uma estrutura de pastas previsível:

```text
/sdcard/
  config/
    device.json
    wifi.json
  logs/
    2026-06-17.csv
  frames/
    boot.raw
    idle.raw
  audio/
    2026-06-17_153000.wav
  cache/
  ota/
```

- Evitar escrita contínua pequena e frequente; usar buffer em RAM e flush periódico.
- Para logs críticos, gravar em formato append-only.
- Considerar arquivo de “journal” para recuperação após queda de energia.

---

## 9. Áudio — ES8311, microfone, amplificador e speaker

> Esta seção vem do esquema da variante analisada. Confirme se sua placa física possui microfone/speaker/componentes de áudio povoados.

### 9.1 Componentes

| Componente | Função |
|---|---|
| `U10 ES8311` | Codec de áudio I2S com ADC/DAC |
| `MIC2` | Microfone |
| `U11 NS4150B` | Amplificador de áudio |
| Speaker header | Saída para speaker externo/interno |

### 9.2 Endereço I2C do codec

O esquema marca o ES8311 com endereço:

```text
0x18
```

### 9.3 Mapeamento I2S/I2C no ESP32-S3

| Função | GPIO |
|---|---:|
| `I2S_MCLK` | GPIO14 |
| `I2S_SCLK` | GPIO15 |
| `I2S_ASDOUT` | GPIO16 |
| `I2S_LRCK` | GPIO17 |
| `I2S_DSDIN` | GPIO18 |
| `ES8311_SDA` | SDA |
| `ES8311_SCL` | SCL |
| `PA_EN` | GPIO42 |
| `PA_CTRL` | GPIO46 |

### 9.4 Usos possíveis

- Captura de áudio local.
- Alertas sonoros simples.
- Feedback sonoro de estado.
- Gravação de pequenos WAVs no microSD.
- Detecção de eventos por áudio, dependendo do firmware.

### 9.5 Cuidados

- Áudio + Wi‑Fi + e-Paper pode gerar picos de consumo; dimensionar bateria e reguladores.
- Desligar PA/speaker quando não estiver usando (`PA_EN`/`PA_CTRL`).
- Verificar se GPIO46 não entra em conflito com strapping/boot na sua configuração.
- Em projetos de bateria, desligar codec e amplificador quando o dispositivo estiver dormindo.

---

## 10. Alimentação, bateria e carregamento

### 10.1 Entradas e trilhos principais

| Sinal | Função |
|---|---|
| `VBUS` | 5 V vindo do USB‑C |
| `VBAT` / `VBAT+` | Bateria de lítio |
| `VSYS` | Barramento do sistema/power path |
| `3V3` | Alimentação lógica principal |
| `VDD3V3` | Alimentação do ESP32-S3 |
| `EPD3V3` | Alimentação do e-Paper, controlada |
| `A3V3` | Alimentação analógica/áudio |
| `PAVCC` | Alimentação do amplificador de áudio |

### 10.2 Regulador principal

O esquema usa **MP1605** como conversor para 3.3 V:

```text
VIN: 2.3 V – 5.5 V
Iout: máximo 2 A
VOUT = 0.6 × (1 + 200 / 44.2) ≈ 3.314 V
```

### 10.3 Carregador/power path

O esquema usa **ETA6098** para gerenciamento de bateria/alimentação, com `VBUS`, `VBAT`, `VSYS`, `STAT`, `ISET` e conversor associado.

O resistor `R35` de `ISET` aparece como **820 kΩ**. A tabela no esquema indica:

| R35/ISET | Corrente de carga aproximada |
|---:|---:|
| 820 kΩ | 0.2 A |
| 360 kΩ | 0.5 A |
| 220 kΩ | 0.8 A |
| 180 kΩ | 1.0 A |
| 120 kΩ | 1.5 A |
| 82 kΩ | 2.0 A |

### 10.4 Implicação para sua bateria de 500 mAh

Com `R35 = 820 kΩ`, a corrente de carga indicada é **0.2 A / 200 mA**.

Para uma bateria de **500 mAh**:

```text
200 mA / 500 mAh = 0.4C
```

Isso é uma taxa de carga razoável para muitas células Li‑Po pequenas, mas confirme a especificação real da bateria. Algumas células muito pequenas recomendam 0.2C–0.5C; outras aceitam 1C.

### 10.5 Leitura de bateria

O esquema mostra:

| Função | GPIO |
|---|---:|
| `BAT_ADC` | GPIO4 |
| `BAT_Control` | Sinal de controle de leitura/alimentação da medição |
| `BAT_KEY` | GPIO1/GPIO2/GPIO3 conforme bloco de chaves |

O divisor de bateria usa resistores altos, incluindo `R38` e `R21` de 200 kΩ no trecho `VBAT` → `BAT_ADC`, com capacitor de filtragem. Isso reduz consumo em repouso.

### 10.6 Recomendações para bateria

- Usar bateria Li‑Po/Li‑ion **1S nominal 3.7 V**, carga máxima 4.2 V.
- Confirmar polaridade do conector antes de ligar: `VBAT+` e `GND`.
- Não usar bateria sem proteção, a menos que o circuito/pack tenha proteção adequada.
- Medir a tensão nos pads antes da primeira conexão.
- Para 500 mAh, a autonomia real dependerá fortemente de Wi‑Fi, áudio e frequência de atualização do e-Paper.
- Usar deep sleep sempre que possível.
- Desligar `EPD3V3`, codec e PA durante sleep.

### 10.7 Estimativa qualitativa de autonomia

| Perfil | Descrição | Autonomia esperada |
|---|---|---|
| Sempre ativo com Wi‑Fi | CPU + rádio ativos, logs/áudio | Baixa, horas |
| Ciclos curtos Wi‑Fi | Acorda, sincroniza, atualiza tela, dorme | Média/alta |
| E-paper estático + RTC wake | Dorme quase sempre, atualiza ocasionalmente | Alta, potencialmente dias/semanas |
| Áudio frequente | Microfone/codec/SD ativos | Média/baixa |

Para estimativa precisa, medir corrente em três estados: boot, ativo com Wi‑Fi, deep sleep.

---

## 11. USB-C, programação e boot

### 11.1 USB-C

O esquema mostra conector USB‑C com:

| Sinal | Função |
|---|---|
| `VBUS` | Alimentação 5 V |
| `U_N` | USB D− |
| `U_P` | USB D+ |
| `CC1/CC2` | Resistores 5.1 kΩ para identificação como dispositivo USB |
| `GND` | Terra |

### 11.2 USB Serial/JTAG

No ESP32-S3, USB D− e D+ usam:

| USB | GPIO |
|---|---:|
| D− | GPIO19 |
| D+ | GPIO20 |

Esses pinos também têm funções analógicas/RTC, mas nesta placa estão dedicados ao USB.

### 11.3 UART0

| Função | GPIO |
|---|---:|
| `TXD` / U0TXD | GPIO43 |
| `RXD` / U0RXD | GPIO44 |

### 11.4 BOOT e modo download

O ESP32-S3 usa pinos de strapping para definir modo de boot. O esquema mostra `BOOT0` ligado ao **GPIO0** com pull-up de 10 kΩ.

Regra prática:

- Boot normal: GPIO0 alto no reset.
- Download/flash mode: segurar BOOT, resetar/ligar, soltar BOOT.

### 11.5 Strapping pins importantes

| GPIO | Relevância |
|---:|---|
| GPIO0 | Boot/download mode |
| GPIO45 | Seleção relacionada a `VDD_SPI` no reset |
| GPIO46 | Participa de boot/download e aparece também em `PA_CTRL` no esquema |
| GPIO3 | Relacionado à seleção de JTAG no boot |

Evite colocar periféricos externos que forcem estados indesejados nesses pinos durante reset.

---

## 12. Mapa de GPIOs da placa

> Tabela consolidada a partir do esquema. Verifique com firmware de teste porque pode haver diferenças entre variantes.

| GPIO | Uso na placa | Observações |
|---:|---|---|
| GPIO0 | `BOOT0` | Strapping/download mode |
| GPIO1 | `BAT_KEY` / header | Pode estar associado a botão/entrada |
| GPIO2 | `BAT_KEY` / header | Pode estar associado a botão/entrada |
| GPIO3 | `BAT_KEY`, `LED_G`, strapping/JTAG | Cuidado no boot |
| GPIO4 | `BAT_ADC` | ADC bateria; também ADC1_CH3/TOUCH4 |
| GPIO5 | `RTC_INT` | Interrupção do RTC |
| GPIO6 | `EPD3V3_EN` | Liga alimentação do e-Paper |
| GPIO7 | Não usado para touch nesta versão | Na versão sem touch, tratar como NC/reservado até validar no hardware |
| GPIO8 | `EPD_SDI` | MOSI do e-Paper |
| GPIO9 | Não usado para touch nesta versão | Na versão sem touch, tratar como NC/reservado até validar no hardware |
| GPIO10 | `EPD_SCLK` | Clock SPI e-Paper |
| GPIO11 | `EPD_RST` | Reset e-Paper |
| GPIO12 | `EPD_CS` | Chip select e-Paper |
| GPIO13 | `EPD_D/C` | Data/command e-Paper |
| GPIO14 | `I2S_MCLK` | Áudio |
| GPIO15 | `I2S_SCLK` | Áudio |
| GPIO16 | `I2S_ASDOUT` | Áudio |
| GPIO17 | `I2S_LRCK` / Key | Áudio / botão conforme montagem |
| GPIO18 | `I2S_DSDIN` / Key | Áudio / botão conforme montagem |
| GPIO19 | `U_N` | USB D− |
| GPIO20 | `U_P` | USB D+ |
| GPIO21 | `EPD_BUSY` | Busy e-Paper |
| GPIO33 | Header/expansão | Livre conforme variante |
| GPIO34 | Header/expansão | Livre conforme variante |
| GPIO35 | Header/expansão | Livre conforme variante |
| GPIO36 | Header/expansão | Livre conforme variante |
| GPIO37 | Header/expansão | Livre conforme variante |
| GPIO38 | `STAT` / header | Status carga/LED conforme esquema |
| GPIO39 | `SD_MOSI` | microSD |
| GPIO40 | `SD_MISO` | microSD |
| GPIO41 | `SD_CLK` | microSD |
| GPIO42 | `PA_EN` | Habilita amplificador |
| GPIO43 | `TXD` | UART0 TX |
| GPIO44 | `RXD` | UART0 RX |
| GPIO45 | Header/strapping | Cuidado no boot |
| GPIO46 | `PA_CTRL` / strapping | Cuidado no boot |
| GPIO47 | Header/expansão | Livre conforme variante |
| GPIO48 | Header/expansão | Livre conforme variante |

### 12.1 Barramento I2C consolidado

O esquema usa nets genéricas `SDA` e `SCL` para vários componentes:

| Dispositivo | Endereço conhecido | Sinais |
|---|---:|---|
| SHTC3 | `0x70` | SDA/SCL |
| ES8311 | `0x18` | SDA/SCL |
| PCF85063A | endereço I2C típico do chip, validar por scan | RTC_SDA/RTC_SCL ligados ao barramento |

### 12.2 Pinos críticos que não devem ser usados sem análise

- GPIO0: boot.
- GPIO19/GPIO20: USB.
- GPIO43/GPIO44: UART0.
- GPIO45/GPIO46: strapping/boot; GPIO46 também aparece em áudio.
- GPIO39/40/41: microSD.
- GPIO6/8/10/11/12/13/21: e-Paper.
- GPIO14–18/42/46: áudio, se componentes de áudio estiverem povoados.

---

## 13. Recursos físicos/onboard resources

Com base na documentação e no esquema:

1. **ESP32-S3-PICO-1-N8R8** com Wi‑Fi, BLE, Flash e PSRAM integradas.
2. **Slot TF/microSD** para cartão, já usado com cartão de 64 GB no seu setup.
3. **ES8311 audio codec** para entrada/saída de áudio.
4. **Botão BOOT** para modo download.
5. **Botão PWR/KEY** para power/entrada do usuário, conforme firmware.
6. **USB‑C** para alimentação, flashing e serial debugging.
7. **Microfone** para captura de áudio, se povoado.
8. **SHTC3** para temperatura e umidade.
9. **Header de speaker**/speaker para saída de áudio, se povoado.
10. **Header de bateria 1.25 mm 2P** para Li‑Po/Li‑ion 1S.
11. **Antena onboard** para Wi‑Fi/BLE.
12. **RTC PCF85063** para timekeeping.
13. **Headers de expansão GPIO**.
14. **e-Paper 1.54" 200 × 200**.

---

## 14. Desenvolvimento de firmware

### 14.1 Ambientes possíveis

- **ESP-IDF**: recomendado para controle fino de energia, I2S, SD, deep sleep, RTC wake e OTA.
- **Arduino core for ESP32**: mais simples para prototipagem rápida.
- **PlatformIO**: bom equilíbrio entre Arduino e ESP-IDF.
- **MicroPython/CircuitPython**: possível para protótipos, mas pode ter limitações com e-Paper, áudio e deep sleep.

### 14.2 Configuração base ESP-IDF provável

```text
Target: esp32s3
Flash: 8 MB
PSRAM: enabled, octal/opi conforme board support
USB CDC/JTAG: enabled
Partition table: custom, se usar OTA/logs internos
Filesystem externo: SD/FATFS
```

### 14.3 Particionamento sugerido da flash

Para projeto robusto com OTA:

| Partição | Tamanho sugerido | Uso |
|---|---:|---|
| nvs | 24 KB | Configurações |
| otadata | 8 KB | OTA state |
| phy_init | 4 KB | RF init |
| factory | 2 MB | Firmware inicial |
| ota_0 | 2 MB | Firmware OTA A |
| ota_1 | 2 MB | Firmware OTA B |
| storage/littlefs | restante | Config/local cache |

Como há microSD de 64 GB, evite usar flash interna para logs grandes.

---

## 15. Estratégia de energia recomendada para projeto com bateria

### 15.1 Estados sugeridos

| Estado | Componentes ativos | Uso |
|---|---|---|
| `BOOT` | ESP32, RTC, SHTC3, SD opcional | Inicialização, validação |
| `IDLE_DISPLAY` | e-Paper sem alimentação contínua, RTC ativo | Tela estática |
| `MEASURE` | ESP32 + SHTC3 | Ler ambiente |
| `LOG` | ESP32 + SD | Gravar dados |
| `SYNC` | ESP32 + Wi‑Fi | NTP, upload, OTA |
| `AUDIO_CAPTURE` | ESP32 + ES8311 + SD | Gravação/captura |
| `SLEEP` | RTC/ULP/RTC memory | Economia máxima |

### 15.2 Sequência para baixo consumo

1. Boot.
2. Verificar bateria via `BAT_ADC`.
3. Ler RTC e validar bit `OS`.
4. Ler SHTC3 se necessário.
5. Montar SD apenas quando for ler/gravar.
6. Atualizar e-Paper:
   - ligar `EPD3V3_EN`;
   - inicializar display;
   - enviar framebuffer;
   - aguardar `EPD_BUSY`;
   - desligar display;
   - desabilitar `EPD3V3_EN`.
7. Ligar Wi‑Fi apenas para sincronização.
8. Desligar codec/PA se não usar áudio.
9. Configurar wake via RTC timer, `RTC_INT` ou botão físico.
10. Entrar em deep sleep.

### 15.3 Wake sources úteis

- RTC timer interno do ESP32-S3.
- Interrupção externa do PCF85063 (`RTC_INT` em GPIO5), se validada como wake-capable no modo escolhido.
- Botão físico.
- ULP para medições leves.

---

## 16. Checklist de bring-up da placa

### 16.1 Pelo terminal no Mac

1. Identificar porta USB:

```bash
ls /dev/cu.*
```

2. Instalar/verificar `esptool.py`:

```bash
python3 -m pip install --upgrade esptool
esptool.py version
```

3. Ler chip:

```bash
esptool.py --chip esp32s3 --port /dev/cu.usbmodemXXXX chip_id
esptool.py --chip esp32s3 --port /dev/cu.usbmodemXXXX flash_id
```

4. Confirmar flash/PSRAM via firmware ou log de boot ESP-IDF.

### 16.2 Scan I2C

Validar dispositivos esperados:

| Dispositivo | Endereço esperado |
|---|---:|
| SHTC3 | `0x70` |
| ES8311 | `0x18` |
| RTC PCF85063 | validar por scan |

### 16.3 Teste do e-Paper

- Testar init completo.
- Desenhar padrão quadriculado.
- Testar tela cheia preta/branca.
- Testar partial refresh.
- Testar desligamento `EPD3V3_EN` e retorno.

### 16.4 Teste do SD 64 GB

- Verificar filesystem.
- Se exFAT falhar, formatar FAT32.
- Criar, escrever, ler e remover arquivo.
- Testar gravação append-only.
- Testar resistência a power loss.

### 16.5 Teste da bateria

- Medir tensão real no conector antes de plugar.
- Confirmar polaridade do JST/GH 1.25 mm.
- Ligar por USB e observar `STAT`/carregamento.
- Ler `BAT_ADC` e calibrar curva.
- Medir corrente em sleep.

### 16.6 Teste do RTC

- Ler hora/data.
- Setar hora por NTP.
- Desligar/religar e verificar persistência.
- Testar alarme/interrupção em GPIO5.
- Verificar bit `OS` após power cycle.

### 16.7 Teste do SHTC3

- Ler temperatura/umidade.
- Validar ID register.
- Testar normal mode e low-power mode.
- Testar sleep/wakeup do sensor.

### 16.8 Teste de áudio

- Detectar ES8311 em `0x18`.
- Inicializar I2S.
- Gravar WAV curto no SD.
- Tocar beep no speaker.
- Desligar PA e medir queda de consumo.

---

## 17. Riscos e pontos de atenção

### 17.1 Diferença entre variantes

A sua placa é a versão `ESP32-S3-ePaper-1.54`, **sem touch** e sem bateria. O esquema anexado é da versão `Touch-ePaper`, então recursos exclusivos do touch screen não devem ser considerados parte da sua unidade. Antes de assumir áudio/pinos exatos, confirme fisicamente e via firmware.

### 17.2 microSD de 64 GB

Cartões de 64 GB frequentemente vêm em exFAT. Muitas bibliotecas ESP32 funcionam melhor com FAT32. Isso pode ser a diferença entre “não monta” e “funciona”.

### 17.3 Bateria

- Nunca inverter polaridade.
- Não carregar bateria de química errada.
- Não usar bateria sem proteção se o pack não tiver circuito de proteção.
- A taxa de carga sugerida pelo R35 atual é 200 mA; confirme compatibilidade com sua célula de 500 mAh.

### 17.4 e-Paper

- Evitar atualizações muito frequentes.
- Considerar refresh completo periódico para reduzir ghosting.
- Não deixar o display meses com a mesma imagem sem refresh se quiser preservar qualidade.

### 17.5 Pinos de boot

GPIO0, GPIO45 e GPIO46 precisam de cuidado. Um periférico externo ou circuito que force nível errado nesses pinos pode impedir boot ou causar modo download inesperado.

### 17.6 Áudio e consumo

Codec, PA, microSD e Wi‑Fi podem dominar consumo. Para projeto portátil, desligue tudo que não estiver em uso.

---

## 18. Modelo de configuração do projeto

Arquivo sugerido: `/sdcard/config/device.json`

```json
{
  "device": {
    "name": "esp32s3-epaper-154",
    "board": "Waveshare ESP32-S3 ePaper 1.54 V2",
    "display": "1.54inch e-Paper V2 200x200 BW",
    "touch": false,
    "battery_mAh": 500
  },
  "pins": {
    "epd_busy": 21,
    "epd_rst": 11,
    "epd_dc": 13,
    "epd_cs": 12,
    "epd_sclk": 10,
    "epd_mosi": 8,
    "epd_power_en": 6,
    "battery_adc": 4,
    "rtc_int": 5,
    "sd_clk": 41,
    "sd_miso": 40,
    "sd_mosi": 39,
    "i2s_mclk": 14,
    "i2s_sclk": 15,
    "i2s_asdout": 16,
    "i2s_lrck": 17,
    "i2s_dsdin": 18,
    "pa_en": 42,
    "pa_ctrl": 46
  },
  "i2c": {
    "shtc3": "0x70",
    "es8311": "0x18",
    "rtc": "scan"
  },
  "storage": {
    "sdcard": true,
    "recommended_filesystem": "FAT32"
  },
  "power": {
    "charge_current_mA_from_schematic": 200,
    "deep_sleep_default": true
  }
}
```

---

## 19. Perguntas em aberto para fechar a documentação do projeto

1. Ela tem **microfone/speaker** fisicamente povoados ou só os pads/componentes no esquema?
2. Qual será o objetivo principal do projeto: dashboard, logger ambiental, gravador, assistente físico, controle remoto, ou outro?
3. A bateria de 500 mAh terá conector compatível com a placa? Você já sabe a polaridade do cabo?
4. Você pretende usar Arduino, ESP-IDF ou PlatformIO?
5. O microSD precisa armazenar áudio/imagens/logs grandes ou apenas configurações?

---

## 20. Próximos passos recomendados

1. Rodar inventário por terminal: `esptool.py`, log de boot e scan I2C.
2. Confirmar mapa real de GPIO com exemplos Waveshare.
3. Testar SD 64 GB e, se necessário, formatar FAT32.
4. Testar e-Paper com framebuffer 200 × 200.
5. Testar bateria em bancada, primeiro via USB com multímetro.
6. Medir consumo em ativo/deep sleep.
7. Criar um `hardware.md` vivo com resultados reais medidos na sua unidade.

---

## 21. Glossário rápido

| Termo | Significado |
|---|---|
| e-Paper/EPD | Display eletroforético, biestável, sem backlight |
| Partial refresh | Atualização de apenas parte da tela |
| Ghosting | Resíduo visual de atualizações anteriores no e-Paper |
| PSRAM | RAM externa/in-package usada para buffers grandes |
| RTC | Real-Time Clock, relógio de tempo real |
| ULP | Coprocessador de baixíssimo consumo do ESP32-S3 |
| I2C | Barramento serial de 2 fios, usado por sensores/RTC/codec |
| SPI | Barramento serial rápido, usado pelo e-Paper e possivelmente microSD |
| I2S | Interface digital de áudio |
| VBUS | 5 V do USB |
| VBAT | Tensão da bateria |
| VSYS | Tensão principal do sistema após power path |
| 3V3 | Alimentação regulada de 3.3 V |

---

## 22. Referências locais usadas

- `1.54inch_e-paper_V2_Datasheet.pdf`
- `SHTC3_Datasheet.pdf`
- `Pcf85063atl1118-NdPQpTGE-loeW7GbZ7.pdf`
- `esp32-s3_datasheet_en.pdf`
- `esp32-s3_technical_reference_manual_en.pdf`
- `ESP32-S3-Touch-ePaper-1.54-Schematic.pdf`
- `docs.waveshare.com-ESP32-S3-ePaper-1.54WaveShareDocumentationPlatform.jpeg`

