# Taskhog Firmware (ESP-IDF)

Scaffold M0.5 ✅ — bring-up **M0 completo** (2026-06-20). Próximo: **M1** captura push-to-talk offline.

## Requisitos

- ESP-IDF **≥ v5.2**
- Target: `esp32s3` (ESP32-S3-PICO-1-N8R8)
- USB-C da placa conectado ao Mac

## Setup ESP-IDF (M0-T1) — Mac Apple Silicon

### 1. Dependências (Homebrew)

```bash
brew install cmake ninja dfu-util python@3.12
```

> O ESP-IDF usa seu próprio Python via `~/.espressif/`. O Python 3.14 do sistema também funciona com v5.2.2 neste Mac.

### 2. Clonar ESP-IDF v5.2.2

```bash
mkdir -p ~/esp && cd ~/esp
git clone -b v5.2.2 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh esp32s3
```

### 3. Ativar o ambiente (cada terminal novo)

```bash
source ~/esp/esp-idf/export.sh
```

**Opcional** — adicionar ao `~/.zshrc`:

```bash
alias get_idf='. ~/esp/esp-idf/export.sh'
```

Depois basta rodar `get_idf` antes de `idf.py`.

### 4. Build verificado ✅

```bash
cd taskhog-fw
idf.py set-target esp32s3   # só na primeira vez
idf.py build
```

## Flash + monitor

```bash
idf.py -p /dev/cu.usbmodem* flash monitor
```

Log esperado no scaffold:

```text
I (xxx) taskhog: Taskhog firmware scaffold ready (state=IDLE)
```

## Estrutura

Ver `docs/specs/01-device-firmware.md` §3.

| Componente | Módulos |
|---|---|
| `power/` | power_mgr, battery |
| `audio/` | es8311_codec, audio_capture, wav_writer |
| `storage/` | sdcard, queue, journal |
| `net/` | wifi_sta, sync_engine, http_uploader, time_sync |
| `provisioning/` | softap, captive_dns, portal_http |
| `rtc/` | pcf85063 |
| `sensors/` | shtc3 |
| `ui/` | epaper_drv, framebuffer, widgets, screens |
| `config/` | config |
| `ota/` | ota_update |

## Próximo passo

**M1** — captura local: REC → WAV + `.job` no SD, state machine, telas e-Paper (`docs/roadmap.md` §4).
