# Taskhog — Spec 01: Firmware do Device (ESP-IDF)

> **Companion da PRD v1.0.** Aprofunda as camadas do firmware até o nível de módulos, APIs do ESP-IDF, máquinas de estado, timing e tratamento de erro.
> **Alvo:** ESP32-S3-PICO-1-N8R8 · ESP-IDF ≥ v5.2 · variante e-Paper 1.54" sem touch.

---

## 1. Escopo

O firmware é **deliberadamente burro**: captura áudio, persiste localmente, sincroniza com o Hub, mostra estado, dorme. Nenhuma IA, NLP ou chamada ao Todoist roda aqui. Tudo que exige inteligência é responsabilidade do Hub (ver Spec 02).

Contrato fundamental: **uma captura nunca pode ser perdida por falta de rede ou bateria.** Captura é a operação sagrada; todo o resto é best-effort.

---

## 2. Stack e componentes

| Item | Escolha | Justificativa |
|---|---|---|
| Framework | ESP-IDF ≥ 5.2 | Controle fino de deep sleep, I2S std driver, OTA A/B, NVS |
| RTOS | FreeRTOS (incluso) | Tasks dedicadas (captura, sync, UI) |
| Áudio | `driver/i2s_std.h` (novo I2S) + driver ES8311 via I2C | API moderna; legado `i2s.h` está deprecado |
| Storage | `esp_vfs_fat_sdspi_mount` (FATFS sobre SDSPI) | microSD em linhas SPI-like |
| Rede | `esp_wifi` + `esp_http_client` (HTTPS) | Upload multipart |
| Provisionamento | SoftAP + `esp_http_server` + DNS captive | Portal cativo (decisão D5) |
| Tempo | `esp_sntp` + driver PCF85063 (I2C) | NTP online, RTC offline |
| Persistência de config | NVS + `device.json` no SD | Credenciais em NVS; config legível no SD |
| e-Paper | Driver SPI custom (controlador provável **SSD1681**) | 200×200 1bpp, parcial/total |
| Buffers grandes | PSRAM (8 MB) | Ring buffer de áudio, framebuffer |

> **Verificar no bring-up:** o controlador exato do painel 1.54" V2. A família costuma usar **SSD1681**; confirme o datasheet/exemplo Waveshare antes de fixar a sequência de init.

---

## 3. Estrutura de componentes

```text
taskhog-fw/
├── CMakeLists.txt
├── sdkconfig.defaults          # PSRAM octal, USB CDC, partições custom
├── partitions.csv              # A/B OTA + nvs (ver §15)
├── main/
│   ├── app_main.c              # entrypoint, gate de energia, orquestração
│   ├── state_machine.{c,h}     # estado global do device
│   └── events.h                # definições de eventos (esp_event base)
└── components/
    ├── power/                  # deep sleep, wake, bateria
    │   ├── power_mgr.{c,h}
    │   └── battery.{c,h}       # ADC + curva de calibração
    ├── audio/
    │   ├── es8311_codec.{c,h}  # init/config via I2C
    │   ├── audio_capture.{c,h} # I2S std, ring buffer PSRAM
    │   └── wav_writer.{c,h}    # header WAV + flush incremental
    ├── storage/
    │   ├── sdcard.{c,h}        # mount FATFS/SDSPI
    │   ├── queue.{c,h}         # fila de capturas (.job)
    │   └── journal.{c,h}       # append-only recovery
    ├── net/
    │   ├── wifi_sta.{c,h}      # conexão multi-AP
    │   ├── sync_engine.{c,h}   # drenagem da fila, backoff, idempotência
    │   ├── http_uploader.{c,h} # POST multipart p/ Hub
    │   └── time_sync.{c,h}     # SNTP → RTC
    ├── provisioning/
    │   ├── softap.{c,h}
    │   ├── captive_dns.{c,h}
    │   └── portal_http.{c,h}   # página + endpoints de config
    ├── rtc/
    │   └── pcf85063.{c,h}      # I2C, set/get, OS bit, alarme/INT
    ├── sensors/
    │   └── shtc3.{c,h}         # opcional (temp/umidade)
    ├── ui/
    │   ├── epaper_drv.{c,h}    # SSD1681 SPI + framebuffer lógico + transform de orientação
    │   ├── epaper_cfg.h        # knobs de orientação (MIRROR_X/Y, SWAP_XY) + flag calibração
    │   ├── gfx.h               # formatos gfx_font_t / gfx_image_t (1-bit)
    │   ├── assets_fonts.{c,h}  # GERADO: SpaceMono em bitmap (tools/gen_assets.py)
    │   ├── assets_images.{c,h} # GERADO: mascote + ícones 1-bit (tools/gen_assets.py)
    │   ├── framebuffer.{c,h}   # canvas 1bpp: texto, blit de imagem, primitivas
    │   ├── widgets.{c,h}       # barra de status, rodapé, formatação
    │   └── screens.{c,h}       # T0..T11 (ver §12)
    ├── config/
    │   └── config.{c,h}        # NVS + device.json, precedência
    └── ota/
        └── ota_update.{c,h}    # esp_https_ota A/B + rollback
```

---

## 4. Máquina de estados global

```text
                ┌────────────── (USB plugado) ──────────────┐
                ▼                                            │
          ┌──────────┐  bateria OK    ┌──────────┐           │
  reset──►│  BOOT    │───────────────►│  IDLE    │◄──────────┘
          └────┬─────┘                └────┬─────┘
               │ bateria < shutdown        │ segura REC
               ▼                           ▼
          ┌──────────┐               ┌──────────┐
          │ SAFE_OFF │               │RECORDING │
          └──────────┘               └────┬─────┘
                                          │ solta / 120s / erro
               BOOT segurado              ▼
               ──► PROVISION         ┌──────────┐
                                     │FINALIZING│ grava .job
                                     └────┬─────┘
                                          ▼
                                     ┌──────────┐ rede? ┌──────────┐
                                     │ CONFIRM  │──sim──►│  SYNC    │
                                     └────┬─────┘       └────┬─────┘
                                          │ não              │ fila vazia / falha
                                          ▼                  ▼
                                     ┌──────────────── DEEP_SLEEP ─────────────────┐
                                     │ wake: REC | RTC timer | USB | RTC_INT(GPIO5) │
                                     └──────────────────────────────────────────────┘
```

Estados (enum `taskhog_state_t`): `BOOT`, `IDLE`, `RECORDING`, `FINALIZING`, `CONFIRM`, `SYNC`, `PROVISION`, `SAFE_OFF`, `OTA`. Transições disparadas por eventos (`esp_event`) — ver `events.h`.

---

## 5. Sequência de boot

```text
1. app_main: inicializa NVS, carrega config (NVS > device.json).
2. power_mgr: lê causa do wake (esp_sleep_get_wakeup_cause).
3. battery: lê BAT_ADC → calcula %; aplica gate (§7).
      • < shutdown%  → flush journal, tela SAFE_OFF, deep sleep profundo. FIM.
4. rtc: inicializa PCF85063; checa bit OS.
      • OS setado → marca tempo "não confiável" (rtc_valid=false).
5. Despacho por causa de wake:
      • EXT0 (REC)        → vai direto p/ ARMING/RECORDING (caminho rápido).
      • TIMER / RTC_INT   → checa fila; se pendente e rede possível → SYNC; senão volta a dormir.
      • USB / cold boot   → IDLE + tenta sync oportunista.
      • BOOT segurado     → PROVISION.
6. Monta SD sob demanda (não no boot, para economizar em wakes de UI).
```

> **Otimização-chave:** o wake por REC deve preparar o áudio em ≤ 300 ms. Mantenha o caminho REC→RECORDING enxuto: não monte Wi-Fi, não atualize tela cheia, não faça scan I2C completo antes de começar a gravar.

---

## 6. Concorrência (tasks FreeRTOS)

| Task | Prioridade | Responsabilidade | Observação |
|---|---:|---|---|
| `tk_main` | média | máquina de estados, eventos | core 0 |
| `tk_audio` | **alta** | I2S read → ring buffer | core 1, nunca preemptada por rede |
| `tk_writer` | média | ring buffer → WAV no SD | flush incremental |
| `tk_sync` | baixa | drenagem da fila, HTTP | só ativa em SYNC |
| `tk_ui` | baixa | render e-paper | nunca durante RECORDING |

Regra de ouro de energia/áudio: **nunca rodar `tk_sync` (Wi-Fi) e `tk_audio` ao mesmo tempo.** Gravar tem prioridade absoluta; sync espera o device voltar ao IDLE.

---

## 7. Gerenciamento de energia

### 7.1 Deep sleep e wake sources

```c
// Wake por botão REC (nível baixo = pressionado, com pull-up):
esp_sleep_enable_ext0_wakeup(REC_GPIO, 0);

// Wake por INT do RTC PCF85063 (open-drain, ativo baixo) em GPIO5:
esp_sleep_enable_ext1_wakeup(BIT64(RTC_INT_GPIO), ESP_EXT1_WAKEUP_ANY_LOW);

// Wake por timer interno (sync periódico, fallback ao RTC alarm):
esp_sleep_enable_timer_wakeup((uint64_t)sync_interval_min * 60ULL * 1000000ULL);

esp_deep_sleep_start(); // não retorna; device reinicia no wake
```

- Preferir **alarme do PCF85063 → GPIO5** ao timer interno quando quiser intervalos longos e precisos com consumo mínimo.
- Antes de dormir: desligar `EPD3V3_EN` (GPIO6), `PA_EN` (GPIO42), desmontar SD, parar Wi-Fi (`esp_wifi_stop`).
- Guardar estado mínimo necessário em **RTC slow memory** (`RTC_DATA_ATTR`) p/ sobreviver ao deep sleep (ex.: contador de parciais p/ refresh total, flag de fila pendente).

### 7.2 Leitura de bateria (BAT_ADC = GPIO4)

```c
// Divisor de alta impedância (R38/R21 = 200k). Usar ADC1 + calibração eFuse.
adc_oneshot_read(handle, ADC_CHANNEL_3 /*GPIO4*/, &raw);
adc_cali_raw_to_voltage(cali, raw, &mv_adc);
int v_bat_mv = mv_adc * DIVIDER_RATIO;   // calibrar DIVIDER_RATIO no bring-up
```

- Calibrar a **curva tensão→% para Li-Po 1S** (não-linear). Tabela de pontos (4.20→100%, 3.85→75%, 3.70→50%, 3.55→25%, 3.40→10%, 3.30→5%, 3.00→0%) interpolada — **ajustar com medição real**.
- Ler com média de N amostras (ruído do divisor); descartar leitura durante picos de Wi-Fi/áudio.

### 7.3 Gate de energia (tabela canônica)

| % bateria | Estado | REC | Sync | Tela |
|---:|---|:---:|---|---|
| > 25 | Normal | ✓ | periódico normal | normal |
| 10–25 | Economia | ✓ | intervalo ↑ | aviso discreto |
| 3–10 | Crítico | ✓ | **só com USB** | "🪫 crítica" |
| < 3 | Shutdown | ✗* | — | SAFE_OFF + deep sleep |

\* No shutdown, a captura é bloqueada **somente** porque gravar com bateria insuficiente arrisca corromper o WAV/SD. Sempre faça flush do journal antes.

---

## 8. Captura de áudio

### 8.1 Inicialização do codec ES8311

- Endereço I2C `0x18`. Sequência de init via I2C (clock, formato I2S, ganho do mic, sample rate 16 kHz). Usar driver de referência do ES8311 (ESP-IDF `esp_codec_dev` ou driver standalone) — **validar registradores contra o exemplo da placa**.
- Habilitar mic (ADC do codec). Manter **PA desligado** durante captura (não precisamos de saída).
- ⚠️ `PA_CTRL`=GPIO46 é **strapping**: garanta nível seguro no boot; só manipule após o boot estabilizar.

### 8.2 Pipeline I2S → WAV

```text
[ES8311 ADC] --I2S(16k/16bit/mono)--> [ring buffer PSRAM] --tk_writer--> [WAV no SD]
```

- Ring buffer em PSRAM (ex.: 64–128 KB → ~2–4 s de buffer), evita perda se a escrita no SD atrasar.
- `tk_writer` consome blocos e faz **flush incremental** ao arquivo `.wav` (header escrito no início com tamanho provisório; **patch do header** ao finalizar com o tamanho real).
- Parâmetros (de `device.json`): 16 kHz, 16-bit, mono, máx 120 s.

### 8.3 Timing push-to-talk

```text
t0  REC pressionado (interrupt/wake)
t0+ ≤300ms  codec/I2S prontos  → beep curto  → estado RECORDING, mostra T2
...  enquanto pressionado: lê I2S, atualiza contador mm:ss (refresh parcial)
tN  REC solto  → para I2S → finaliza WAV → cria .job → beep duplo → T3 "Gravado ✓"
```

- **Anti-toque-acidental:** se `tN - t0 < 0.5 s`, descartar (sem .job, sem beep duplo).
- **Limite de 120 s:** ao atingir, finalizar automaticamente + tela "Limite atingido, salvo".
- Beeps: ligar `PA_EN` só no instante do beep; desligar logo após (economia + evita ruído).

---

## 9. Storage e fila

### 9.1 Mount

```c
esp_vfs_fat_sdspi_mount("/sdcard", &host, &slot_config, &mount_config, &card);
// host SPI: SCLK=41, MOSI=39, MISO=40 (validar CS no bring-up)
```

- Montar **sob demanda** (antes de gravar/sincronizar) e desmontar antes de dormir.
- Se `exFAT` falhar → instruir reformat **FAT32** (no bring-up).

### 9.2 Estrutura (ver PRD §8.1) e operações da fila

`queue.{c,h}` expõe:

```c
esp_err_t queue_enqueue(const recording_meta_t *meta, const char *wav_path); // cria .job
esp_err_t queue_peek_next(job_t *out);          // FIFO por timestamp
esp_err_t queue_mark(const char *id, job_state_t st, const char *err);
esp_err_t queue_complete(const char *id);        // move p/ sent/ (ou apaga)
int       queue_pending_count(void);
int       queue_error_count(void);
```

### 9.3 Escrita segura / power-loss

- **Journal append-only** (`journal/queue.jnl` — nome físico FAT 8.3; SD sem LFN): cada transição de job é uma linha. No boot, reproduzir o journal para reconstruir o estado real da fila (arquivos órfãos, jobs em "uploading" → voltam a "queued").
- WAV é considerado válido só após o header ser "patchado" com o tamanho final. WAV sem patch (crash no meio) → marcar como `error: incomplete` e reter para inspeção.

---

## 10. Rede e sincronização

### 10.1 Conexão multi-AP

- `wifi.json` guarda lista de redes `{ssid, psk}` (casa, trabalho, hotspot). Arquivo físico no SD: **`wifi.cfg`** (FAT 8.3).
- Ao precisar de rede: scan → conectar à de maior RSSI conhecida → checar `GET {hub}/v1/health`.
- Se nenhuma rede conhecida ou Hub inalcançável → voltar a IDLE/dormir (fila intacta).

### 10.2 Upload (multipart)

```text
POST {hub}/v1/recordings
Headers: Authorization: Bearer <device_token>
Body (multipart/form-data):
  - audio:    arquivo .wav
  - metadata: JSON (recording_meta — ver Spec 03)
```

- `esp_http_client` com `transport_type = HTTP_TRANSPORT_OVER_SSL`.
- Streaming do arquivo do SD direto pro socket (não carregar WAV inteiro em RAM).
- Timeout generoso (uploads em redes ruins); abortar limpo se cair (job volta a `queued`).

### 10.3 Sync engine (drenagem)

```text
sync_run():
  while (job = queue_peek_next()) and rede_ok and bateria_permite:
     mark(job, uploading)
     resp = upload(job)            # idempotente via client_job_id
     if resp.ok:  mark(job, uploaded); registra hub_recording_id
     else:        job.attempts++; backoff(); if attempts>MAX → mark(error)
  atualiza contadores via GET /v1/status   # p/ a tela
```

- **Idempotência:** o device envia `client_job_id` (= id do .job). O Hub deduplica; reenvio após resposta perdida **não** cria tarefa duplicada.
- **Backoff exponencial:** 1s, 2s, 4s, … até teto (ex.: 60s); `MAX_ATTEMPTS` configurável.
- Apagar/mover WAV só após o Hub reportar `done` (consulta `/v1/recordings/{id}` ou via `/v1/status`).

### 10.4 Tempo (NTP → RTC)

- Em SYNC com rede: `esp_sntp` → ao obter hora, escrever no PCF85063 e limpar flag `rtc_valid=false`.
- Capturas offline carimbam com o RTC; se `rtc_valid=false`, o Hub pode ajustar pelo horário de chegada (ver Spec 03).

### 10.5 Status de implementação (M3 — código completo, validação em device pendente)

- `wifi_sta`: STA on-demand; carrega redes de `wifi.cfg` (fallback Kconfig); **scan + maior RSSI**; event group + retry; ícone Wi-Fi na barra.
- `http_uploader`: `http_uploader_health()` e `http_uploader_upload()` (multipart `metadata`+`audio`, streaming do WAV do SD em blocos de 1 KB, TLS via `esp_crt_bundle_attach`). Metadata conforme Spec 03 §2.
- `sync_engine`: task própria; `sync_engine_drain()` faz connect → health → snapshot FIFO de pendentes → upload por job com transições no `.job`. Disparada ao entrar em `SYNC` e automaticamente após `CONFIRM`/`BOOT` se há fila.
- Configuração por `main/Kconfig.projbuild` (menu **Taskhog**): SSID, senha, `HUB_URL`, `DEVICE_TOKEN`, `SYNC_MAX_ATTEMPTS`. SSID vazio = sync desligado (device segue gravando offline).
- **Pendências (pós-M3):** ~~§10.1 multi-AP/`wifi.cfg`~~ ✅ · §10.4 NTP→RTC · ~~confirmação `done` → `sent/`~~ ✅

---

## 11. Provisionamento (portal cativo)

### 11.1 Entrada no modo

- Sem `wifi.json`/credencial **ou** botão BOOT segurado no power-on → `PROVISION`.

### 11.2 Fluxo

```text
1. softap: sobe AP "Taskhog-Setup" (WPA2 com senha exibida na tela T7).
2. captive_dns: responde toda query DNS com 192.168.4.1 (captura o navegador).
3. portal_http (esp_http_server):
     GET  /          → página de config (HTML embutido)
     GET  /scan      → JSON com redes Wi-Fi visíveis (esp_wifi_scan)
     POST /save      → {ssid, psk, hub_url, device_token}
4. Ao salvar: testa conectar + GET {hub}/v1/health.
     ok   → grava wifi.json + NVS (token), encerra AP, sai do PROVISION.
     erro → mostra erro na página, mantém AP.
```

- Página leve, estática, sem dependências externas (offline, dentro do AP).
- Tela T7 mostra SSID, senha e `http://192.168.4.1`.

---

## 12. UI e-Paper (wireframes detalhados)

### 12.1 Regras de render

- Framebuffer 1bpp = **5000 bytes** (200×200/8).
- **Parcial** para contadores/relógio/ícones; **total** a cada `full_refresh_every` parciais (default 10) ou 1×/dia para limpar ghosting.
  - *Status atual (2026-06-22): só full refresh implementado; parcial fica para uma leva futura.*
- Atualizar **só quando o estado muda** (display biestável).
- Zonas fixas: status (topo ~24px) · conteúdo · rodapé (~20px) → maximiza reuso de parcial.
- Fontes: título ~28px, corpo ~16px, ícones monocromáticos de alto contraste.

#### 12.1.1 Arquitetura de render (implementação 2026-06-22)

- **Orientação isolada num único lugar.** O init do SSD1681 é canônico (sem hacks); todo espelhamento/rotação é aplicado no flush do framebuffer via `epaper_cfg.h` (`EPD_MIRROR_X/Y`, `EPD_SWAP_XY`). Valor travado para este painel: **`MIRROR_Y=1`**. Ver `docs/decisions/004-epaper-ui-rebuild.md` e HARDWARE_NOTES §e-Paper.
- **Buffer lógico** com origem topo-esquerda (x→direita, y→baixo), igual aos mockups. `epaper_drv_set_pixel`/canvas trabalham nessa convenção; o transform converte para a RAM nativa no `epaper_drv_refresh_full`.
- **Fontes = SpaceMono** (Regular/Bold) rasterizadas em bitmap por `tools/gen_assets.py` → `assets_fonts.c`. Tamanhos: `g_font_sm` (13), `g_font_md` (16), `g_font_hd` (18 bold), `g_font_tt` (26 bold), `g_font_xl` (40 bold).
- **Assets = SVG** de `ui/assets/{mascot,icon}` convertidos para 1-bit por `tools/gen_assets.py` (cairosvg) → `assets_images.c` (mascote: default/happy/sleeping/angry/listening; ícones: wifi/no_wifi/bateria/mic/check/cloud/folder/warning).
- **Pipeline de geração:** `tools/.venv/bin/python tools/gen_assets.py` (venv com Pillow + cairosvg). Os `.c/.h` gerados são commitados; não editar à mão.
- **Mapeamento estado→mascote:** IDLE c/ fila = default · IDLE s/ fila = sleeping · RECORDING = angry · CONFIRM = happy.
- **Telas implementadas nesta leva:** T0 splash, T1 Home (com/sem fila), T2 Recording, T3 Saved, T4 Sync. Demais (T5/T6/T7/T8/T9/T10/T11) pendentes.
- **Pendências conhecidas:** status de Wi-Fi e de carregamento ainda não têm fonte real → barra mostra "sem wifi" e bateria sem ícone de carga até `widget_read_status` ser ligado a essas fontes.

### 12.2 Wireframes (ASCII representando 200×200)

**T1 — Home / Idle**
```text
┌────────────────────────────┐
│ 15:30  📶●  🔋78%      ↑2   │  status
│────────────────────────────│
│                            │
│         15:30              │  relógio grande (28–40px)
│       qua, 17 jun          │
│                            │
│   ✓ 12 hoje   ↑ 2 fila     │  contadores
│   🌡 24°C  💧 55%          │  (se show_environment)
│────────────────────────────│
│  segure ⏺ para gravar      │  rodapé/dica
└────────────────────────────┘
```

**T2 — Recording** (inconfundível!)
```text
┌────────────────────────────┐
│ 15:31  📶●  🔋78%          │
│────────────────────────────│
│                            │
│           🎙️              │  ícone grande
│         GRAVANDO           │
│          0:07              │  contador mm:ss (parcial)
│   ▓▓▓▓▓▓░░░░░░░░░░ 07/120  │  barra até o limite
│────────────────────────────│
│   solte ⏺ para finalizar   │
└────────────────────────────┘
```

**T3 — Saved/Queued**
```text
┌────────────────────────────┐
│ 15:31  📶●  🔋78%      ↑3   │
│────────────────────────────│
│                            │
│           ✓                │
│       Gravado!             │
│    na fila (↑3) ·  0:07    │
│                            │
│────────────────────────────│
│   sincroniza ao reconectar │
└────────────────────────────┘
```

**T4 — Syncing**
```text
│  Sincronizando 2/3  •••     │
```

**T5 — Last result**
```text
┌────────────────────────────┐
│ 15:33  📶●  🔋77%      ↑0   │
│────────────────────────────│
│  Última tarefa:            │
│  "Criar apresentação BB"   │  título curto (truncado)
│                            │
│  📁 Trabalho   ✓ alta conf │  ou: "⚠ Inbox (revisar)"
│────────────────────────────│
│  ⏺ gravar  ·  ▸ fila       │
└────────────────────────────┘
```

**T6 — Queue/Pending**
```text
│  Fila: ↑3 pendentes  ⚠1 erro │
│  15:31 · 0:07 · enviando…    │
│  15:28 · 0:12 · na fila      │
│  15:10 · 0:05 · ⚠ falha (3x) │
```

**T7 — Setup**
```text
┌────────────────────────────┐
│        MODO SETUP          │
│────────────────────────────│
│  1. Conecte o celular à     │
│     rede Wi-Fi:             │
│       Taskhog-Setup         │
│     senha: taskhog123       │
│                            │
│  2. Abra no navegador:      │
│       192.168.4.1          │
└────────────────────────────┘
```

**T8 — Low battery** / **T9 — Charging** / **T10 — Error** / **T11 — Info**: ver tabela na PRD §15.3 (mesmo padrão de zonas; mensagens tranquilizadoras "tudo salvo localmente" em T8/T10).

### 12.3 Navegação (2 botões)

```text
IDLE(T1) ─tap NAV→ Queue(T6) ─tap→ Info(T11) ─tap→ IDLE(T1)
   │ segura REC
   ▼
RECORDING(T2) ─solta→ Saved(T3) ─auto→ Sync(T4)/IDLE
```

---

## 13. RTC PCF85063

- I2C; registradores BCD. `pcf85063.{c,h}`: `rtc_get(struct tm*)`, `rtc_set(struct tm*)`, `rtc_check_os()`, `rtc_set_alarm(...)`.
- No boot: ler bit `OS` (em segundos). Setado → oscilador parou → `rtc_valid=false`.
- Alarme/INT em GPIO5 (open-drain) → wake source (§7.1).

---

## 14. Configuração (precedência)

```text
NVS (credenciais/token) > device.json (no SD) > defaults compilados
```

- Credenciais sensíveis (token do device, PSKs) → **NVS** (não em texto plano no SD se possível).
- Parâmetros operacionais (sample rate, thresholds, intervalos) → `device.json` (legível/editável).
- Schema do `device.json` na PRD §17.1 e no Spec 03.

---

## 15. Partições e OTA

`partitions.csv` (ver PRD §10.4): `nvs`, `otadata`, `phy_init`, `factory`, `ota_0`, `ota_1`.

- OTA via `esp_https_ota` durante SYNC, **só com bateria > 30% ou USB**.
- Validar checksum antes de marcar o novo slot como bootável; **rollback automático** (`esp_ota_mark_app_valid_cancel_rollback` só após boot saudável confirmado).

---

## 16. Logging e diagnóstico

- Logs rotativos em `/sdcard/logs/AAAA-MM-DD.log` (nível configurável).
- Tela T11 (Info): `device_id`, versão fw, % bateria, SD livre, IP/host, último sync OK, contadores.
- `esp_log` no console USB-CDC durante desenvolvimento.

---

## 17. Taxonomia de erros do device

| Código | Significado | Comportamento |
|---|---|---|
| `E_SD_MOUNT` | SD não monta | Bloqueia captura; tela de erro; tentar reformat FAT32 |
| `E_WAV_INCOMPLETE` | WAV sem patch de header (crash) | Marca job `error`, retém |
| `E_AUDIO_INIT` | ES8311/I2S falhou | Tela de erro; verificar povoamento (PRD §5.2) |
| `E_HUB_UNREACHABLE` | sem health do Hub | Mantém fila; tela T10; "tudo salvo" |
| `E_HUB_AUTH` | 401/403 no Hub | Tela T10 "token inválido"; reprovisionar |
| `E_UPLOAD_RETRY` | falha transitória | Backoff; permanece na fila |
| `E_BATT_CRITICAL` | < shutdown% | SAFE_OFF + deep sleep |

---

## 18. Critérios de aceite (firmware)

- [ ] REC→gravando em ≤ 300 ms; WAV válido no SD; beeps corretos.
- [ ] Captura offline 100% confiável; journal recupera após power-loss.
- [ ] Sync idempotente drena fila ao reconectar; sem duplicatas.
- [ ] Deep sleep default; wake por REC/RTC/USB funcionando; consumo medido.
- [ ] Gate de bateria respeitado; captura nunca sacrificada acima de shutdown%.
- [ ] Portal cativo provisiona Wi-Fi + Hub + token.
- [ ] e-Paper: todas as telas T0–T11; parcial+total; sem ghosting acumulado.
