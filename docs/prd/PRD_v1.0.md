# Taskhog — Product Requirements Document (PRD) v1.0

> **Documento canônico de produto.** Companion dos Specs 01/02/03 e do Roadmap.
> **Versão:** 1.0 · **Data:** 2026-06-20 · **Status:** Congelado para implementação

---

## 1. Visão do produto

### 1.1 Problema

Capturar tarefas por voz no dia a dia é rápido, mas transformar uma nota falada em algo acionável no gerenciador de tarefas exige abrir o celular, transcrever mentalmente, escolher projeto, prazo e prioridade. Na prática, a ideia se perde ou vira uma entrada genérica no Inbox.

### 1.2 Solução

**Taskhog** é um dispositivo portátil de bolso (ESP32-S3 + e-Paper) com botão push-to-talk. O usuário segura REC, fala a tarefa, solta. O áudio é gravado **100% offline** no microSD, sincronizado com um **Hub self-hosted** no homeserver, que transcreve (Whisper local), estrutura (LLM cloud) e cria a(s) tarefa(s) no **Todoist** — roteadas ao projeto correto quando a confiança é alta.

### 1.3 Princípios de produto

| # | Princípio | Implicação |
|---|---|---|
| P1 | **Captura sagrada** | Nenhuma captura se perde por falta de rede ou bateria |
| P2 | **Device burro, Hub inteligente** | Zero IA no firmware; toda inteligência no Hub |
| P3 | **Offline-first** | Gravar e enfileirar funciona sem rede; sync é best-effort |
| P4 | **Zero duplicata** | Idempotência em upload e criação de tarefas |
| P5 | **Privacidade** | Áudio processado localmente (Whisper no homeserver); texto à LLM pode ser cloud |
| P6 | **Feedback claro** | e-Paper mostra estado; erros tranquilizam ("tudo salvo localmente") |

### 1.4 Escopo v1.0

**Incluído:**
- Device físico com captura, fila, sync, deep sleep, provisionamento
- Hub Docker com Whisper + LLM + Todoist
- Roteamento inteligente a projetos do Todoist do usuário
- Acesso remoto via Cloudflare Tunnel
- OTA A/B no device

**Fora de escopo v1.0:**
- App mobile nativo
- Múltiplos devices com painel admin
- Integrações além do Todoist
- Touch screen (placa sem touch)
- Wake word / gravação contínua

---

## 2. Usuário, casos de uso e frases de teste

### 2.1 Persona

Usuário técnico com homeserver, conta Todoist ativa com projetos nomeados, que quer capturar tarefas sem tirar o celular do bolso — em casa, no trabalho ou em trânsito.

### 2.2 Casos de uso principais

| ID | Caso | Fluxo |
|---|---|---|
| UC1 | Tarefa simples | Segura REC → "comprar pão" → solta → tarefa no Todoist |
| UC2 | Tarefa rica | Segura REC → fala projeto, prazo, prioridade → tarefa estruturada no projeto certo |
| UC3 | Múltiplas ações | Uma fala com 2+ ações → 2+ tarefas criadas |
| UC4 | Offline | Sem rede → grava e enfileira → sync automático ao reconectar |
| UC5 | Baixa confiança | Fala ambígua → Inbox + label `revisar` |
| UC6 | Fora de casa | Captura via Wi-Fi externa → Hub via Cloudflare Tunnel → Todoist |
| UC7 | Setup inicial | Portal cativo no celular → Wi-Fi + URL Hub + token |
| UC8 | Bateria baixa | Aviso na tela; captura bloqueada só em shutdown crítico |

### 2.3 Conjunto de teste para acurácia (M4)

~30 frases reais para medir roteamento ≥80%. O Hub injeta a lista real de projetos do Todoist do usuário no prompt.

**Simples (sem campos extras) — 10 frases**

1. "comprar pão"
2. "ligar pro dentista"
3. "mandar email pro João"
4. "pagar conta de luz"
5. "reservar mesa no restaurante"
6. "comprar presente de aniversário"
7. "devolver livro na biblioteca"
8. "trocar filtro do ar condicionado"
9. "agendar corte de cabelo"
10. "comprar leite e ovos"

**Ricas (projeto + prazo + prioridade) — 10 frases**

11. "criar apresentação para o banco do brasil para quinta-feira prioridade alta"
12. "revisar contrato do cliente amanhã prioridade alta"
13. "preparar relatório mensal até sexta"
14. "marcar reunião com equipe terça às 10h"
15. "comprar passagem pro rio até domingo prioridade alta"
16. "enviar proposta comercial pro cliente X amanhã de manhã"
17. "atualizar planilha de gastos até o fim do mês"
18. "estudar capítulo 5 do curso de python até quarta"
19. "levar carro na revisão sábado de manhã"
20. "renovar assinatura do software até dia 30 prioridade alta"

**Múltiplas ações — 5 frases**

21. "comprar pão e ligar pro contador amanhã"
22. "mandar email pro time e agendar call com fornecedor quinta"
23. "pagar internet e pagar aluguel até dia 5"
24. "comprar remédio e buscar encomenda nos correios"
25. "revisar PR do github e atualizar documentação"

**Ambíguas / baixa confiança — 5 frases**

26. "aquela coisa de antes"
27. "fazer o negócio do projeto"
28. "lembrar de resolver aquilo"
29. "tarefa importante"
30. "como eu tinha falado ontem"

**Critério M4:** ≥80% das frases não-ambíguas (1–25) roteadas ao projeto correto com campos corretos.

---

## 3. Arquitetura de alto nível

```text
┌─────────────────┐     HTTPS multipart      ┌──────────────────────────────┐
│  Taskhog Device │ ────────────────────────► │  Hub (homeserver, Docker)    │
│  ESP32-S3       │     POST /v1/recordings   │  FastAPI + Whisper + LLM     │
│  e-Paper + REC  │ ◄─────────────────────── │  SQLite + Todoist client     │
└─────────────────┘     GET /v1/status        └──────────────┬───────────────┘
        │                                                     │
        │ microSD (WAV + .job)                                  │ REST v2
        │ offline-first                                         ▼
        │                                              ┌──────────────┐
        │                                              │   Todoist    │
        │                                              │   (cloud)    │
        └──────────────────────────────────────────────┴──────────────┘

Exposição remota: Cloudflare Tunnel (cloudflared no host) → URL HTTPS pública
```

### 3.1 Repositório

Monorepo com `taskhog-fw/` (ESP-IDF) e `taskhog-hub/` (Python/FastAPI).

### 3.2 Documentação técnica

| Documento | Escopo |
|---|---|
| **PRD v1.0** (este) | Produto, requisitos, aceite |
| Spec 01 | Firmware detalhado |
| Spec 02 | Hub detalhado |
| Spec 03 | Contratos de dados (**prevalece** em formatos) |
| Roadmap | Sequência M0→M8 |

---

## 4. Hardware

### 4.1 Placa alvo

**Waveshare ESP32-S3 1.54" e-Paper AIoT Development Board V2** — variante **sem touch**.

| Componente | Especificação |
|---|---|
| MCU | ESP32-S3-PICO-1-N8R8 (8 MB Flash, 8 MB PSRAM) |
| Display | e-Paper 1.54" 200×200 px, 1 bpp (SSD1681 provável) |
| Áudio | ES8311 codec + microfone + speaker (**montados fisicamente**) |
| RTC | PCF85063ATL (I2C, alarme em GPIO5) |
| Sensor | SHTC3 temperatura/umidade (opcional na UI) |
| Storage | microSD 64 GB (FAT32) |
| Bateria | Li-Po 1S **1000 mAh** (instalada pelo usuário) |
| Botões | BOOT (GPIO0) = REC, PWR (GPIO18) = NAV — ✅ M0-T9 |
| Conectividade | Wi-Fi 2.4 GHz, USB-C (CDC/JTAG) |

### 4.2 Firmware framework

ESP-IDF ≥ v5.2, FreeRTOS, deep sleep como default.

---

## 5. Requisitos funcionais — Device

### 5.1 Captura push-to-talk

| Req | Descrição | Critério |
|---|---|---|
| RF-01 | Segurar REC inicia gravação | ≤ 300 ms até estado RECORDING |
| RF-02 | Soltar REC finaliza e salva | WAV válido + `.job` criado no SD |
| RF-03 | Anti-toque acidental | Toque < 0,5 s descartado (sem .job) |
| RF-04 | Limite de duração | Corte automático em 120 s |
| RF-05 | Feedback sonoro | Beep início + beep duplo ao salvar |
| RF-06 | Feedback visual | Telas T2 (gravando) e T3 (salvo) |
| RF-07 | Parâmetros de áudio | WAV PCM 16 kHz / 16-bit / mono |

### 5.2 Gate de áudio (🔴 bloqueante — M0-T4)

Validação obrigatória **antes** de qualquer firmware de produto.

**Procedimento de validação:**

1. Scan I2C confirma ES8311 em `0x18`.
2. Init ES8311 via I2C + I2S std driver.
3. Gravar WAV de 5–10 s no microSD (fala normal a ~30 cm do mic).
4. Copiar WAV para PC e reproduzir.
5. Avaliar qualidade.

**Critérios de aprovação (todos obrigatórios):**

| # | Critério | Pass? |
|---|---|:---:|
| G1 | WAV abre sem erro em player padrão (Audacity/VLC) | ☐ |
| G2 | Formato correto: 16 kHz, 16-bit, mono PCM | ☐ |
| G3 | Voz humana **reconhecível** — palavras distinguíveis | ☐ |
| G4 | SNR aceitável — sem clipping constante nem silêncio total | ☐ |
| G5 | Nível de volume consistente entre gravações sucessivas | ☐ |

**Reprovação → Plano B:** mic I2S/PDM externo nos headers de expansão. Não avançar para M1 sem G1–G5 verdes.

**Erro em produção:** código `E_AUDIO_INIT` → tela T10; verificar povoamento e pinos.

### 5.3 Fila e sync offline-first

| Req | Descrição |
|---|---|
| RF-10 | Toda captura válida gera `.job` na fila local (FIFO) |
| RF-11 | Sem rede → permanece na fila indefinidamente |
| RF-12 | Com rede → upload multipart para Hub (`POST /v1/recordings`) |
| RF-13 | Reenvio idempotente via `client_job_id` |
| RF-14 | Backoff exponencial em falhas (1s, 2s, 4s… teto 60s) |
| RF-15 | Journal append-only para recovery após power-loss |
| RF-16 | WAV incompleto (crash mid-write) → `error`, retido para inspeção |
| RF-17 | Contadores na Home (T1) refletem fila real |

### 5.4 Mapeamento GPIO (referência + validação M0)

Pinos do esquema Waveshare (variante sem touch). **Validar fisicamente** no bring-up; divergências em `docs/hardware/HARDWARE_NOTES.md`.

| Função | GPIO | Notas |
|---|---:|---|
| REC (push-to-talk) | **0** (BOOT) | Active LOW — ✅ M0-T9 |
| NAV (tap) | **18** (PWR) | Active LOW — ✅ M0-T9 |
| EPD_BUSY | 21 | |
| EPD_RST | 11 | |
| EPD_D/C | 13 | |
| EPD_CS | 12 | |
| EPD_SCLK | 10 | |
| EPD_MOSI | 8 | |
| EPD3V3_EN | 6 | |
| SD_CLK | 41 | |
| SD_MISO | 40 | |
| SD_MOSI | 39 | |
| SD_CS | TBD | Pode não estar populado — validar |
| I2S_MCLK | 14 | |
| I2S_SCLK | 15 | |
| I2S_ASDOUT | 16 | |
| I2S_LRCK | 17 | Conflito potencial com REC |
| I2S_DSDIN | 18 | Conflito potencial com REC |
| BAT_ADC | 4 | ADC1_CH3 |
| RTC_INT | 5 | Wake ext1 |
| PA_EN | 42 | Amplificador |
| PA_CTRL | 46 | ⚠️ Strapping pin |

| I2C Device | Endereço |
|---|---:|
| ES8311 | 0x18 |
| SHTC3 | 0x70 |
| PCF85063 | scan no bring-up |

### 5.5 Energia e deep sleep

| Req | Descrição |
|---|---|
| RF-20 | Deep sleep como estado default |
| RF-21 | Wake: REC (ext0), RTC alarm/INT GPIO5 (ext1), USB, timer |
| RF-22 | Caminho REC→RECORDING sem montar Wi-Fi/tela cheia antes |
| RF-23 | Gate de bateria por faixas (ver §9) |
| RF-24 | Sync periódico via alarme RTC quando há fila pendente |
| RF-25 | Desligar EPD3V3, PA, SD, Wi-Fi antes de dormir |

### 5.6 Provisionamento

| Req | Descrição |
|---|---|
| RF-30 | SoftAP `Taskhog-Setup` + DNS captive |
| RF-31 | Página web: scan redes, salvar SSID/PSK + Hub URL + token |
| RF-32 | Validar `GET /v1/health` antes de salvar |
| RF-33 | Entrada: sem credencial OU BOOT segurado no power-on |
| RF-34 | Credenciais em NVS; config operacional em `device.json` |

### 5.7 OTA

| Req | Descrição |
|---|---|
| RF-40 | OTA A/B via `esp_https_ota` |
| RF-41 | Só com bateria > 30% ou USB conectado |
| RF-42 | Rollback automático se boot falhar |

---

## 6. Requisitos funcionais — Hub

### 6.1 Pipeline de processamento

```text
áudio → Whisper (PT-BR) → LLM (structuring) → Todoist (1+ tarefas)
```

| Req | Descrição |
|---|---|
| RH-01 | `POST /v1/recordings` aceita multipart, responde 202 imediatamente |
| RH-02 | Worker async: queued → transcribing → structuring → creating → done |
| RH-03 | Whisper local (`faster-whisper`), `language=pt`, `vad_filter=true` |
| RH-04 | LLM cloud OpenAI-compatible, JSON mode, schema Spec 03 §5 |
| RH-05 | Cache de projetos/labels Todoist injetado no prompt |
| RH-06 | Confiança ≥ threshold → projeto; senão → Inbox + `revisar` |
| RH-07 | Campos adaptativos: `due_string`, `priority`, `labels`, `subtasks` |
| RH-08 | Label `taskhog` sempre; `revisar` quando Inbox/baixa confiança |
| RH-09 | Idempotência Todoist via `X-Request-Id` / `idempotency_key` |
| RH-10 | `GET /v1/status` para contadores da tela do device |
| RH-11 | `GET /v1/health` para health check antes de upload |
| RH-12 | Auditoria: `llm_json` cru salvo no DB |
| RH-13 | Provider LLM trocável (cloud ↔ ollama) só por config |

### 6.2 Todoist

- Conta do usuário com projetos já nomeados (lista dinâmica via cache).
- Labels obrigatórias: `taskhog` (sempre), `revisar` (triagem).
- Prioridade API: **4 = mais alta** (invertido vs UI do Todoist).

### 6.3 Latência alvo

| Métrica | Alvo v1.0 |
|---|---|
| REC → gravando | ≤ 300 ms |
| Soltar REC → WAV no SD | ≤ 2 s |
| Captura → tarefa no Todoist (online) | ≤ 20 s |
| Acurácia roteamento (frases 1–25) | ≥ 80% |

---

## 7. Requisitos não-funcionais

| Req | Descrição | Alvo |
|---|---|---|
| RNF-01 | Capturas perdidas | **Zero** |
| RNF-02 | Tarefas duplicadas | **Zero** (idempotência) |
| RNF-03 | Autonomia bateria (perfil típico) | ≥ 5 dias com 1000 mAh |
| RNF-04 | Privacidade áudio | Processamento local (Whisper) |
| RNF-05 | Segredos | Só em env do Hub; token device em NVS |
| RNF-06 | Transporte | TLS (Cloudflare Tunnel ou LAN) |
| RNF-07 | Retenção áudio Hub | Configurável (`retain_audio_days`, default 7) |
| RNF-08 | Observabilidade | Logs estruturados por job + métricas |

---

## 8. Armazenamento no microSD

### 8.1 Estrutura de diretórios

```text
/sdcard/
├── config/
│   ├── device.json          # parâmetros operacionais (schema §17.1)
│   └── wifi.json            # lista de redes {ssid, psk}
├── queue/                   # capturas pendentes de sync
│   ├── 20260617_153012_a1.wav
│   └── 20260617_153012_a1.job
├── sent/                    # capturas confirmadas done (opcional, ou apaga)
├── journal/
│   └── queue.journal        # append-only recovery log
└── logs/
    └── 2026-06-17.log       # logs rotativos do device
```

### 8.2 Regras

- Filesystem: **FAT32** (reformatar se exFAT).
- SD montado sob demanda (gravar/sync); desmontado antes de deep sleep.
- WAV + `.job` criados atomicamente (job só após WAV com header patchado).
- Limpeza de WAV após Hub reportar `done` (configurável).

---

## 9. Energia e bateria

### 9.1 Especificação

- Bateria: Li-Po 1S, **1000 mAh**, 3.7 V nominal.
- Leitura: `BAT_ADC` (GPIO4) com divisor R38/R21 (200kΩ).
- Curva tensão→% calibrada no bring-up (tabela abaixo como ponto de partida).

| Tensão (V) | % indicado |
|---:|---:|
| 4.20 | 100 |
| 3.85 | 75 |
| 3.70 | 50 |
| 3.55 | 25 |
| 3.40 | 10 |
| 3.30 | 5 |
| 3.00 | 0 |

### 9.2 Faixas de operação

| % | Estado | REC | Sync | Tela |
|---:|---|:---:|:---:|---|
| > 25 | Normal | ✓ | periódico | normal |
| 10–25 | Economia | ✓ | intervalo ↑ | aviso discreto |
| 3–10 | Crítico | ✓ | só com USB | T8 "🪫 crítica" |
| < 3 | Shutdown | ✗ | — | SAFE_OFF + deep sleep |

### 9.3 Metas de consumo (§9.5)

Medir em bancada no M6-T7. Perfil típico de uso: ~10 capturas/dia, sync 2–3×/dia, deep sleep o resto.

| Estado | Meta (a medir) | Notas |
|---|---|---|
| DEEP_SLEEP | < 500 µA | EPD off, SD off, Wi-Fi off |
| IDLE (tela estática) | < 50 mA | Sem Wi-Fi |
| RECORDING | < 150 mA | I2S + SD write |
| SYNC (Wi-Fi ativo) | < 250 mA | Upload + HTTP |

**Meta de autonomia:** ≥ **5 dias** com 1000 mAh no perfil típico. Se não atingir → ajustar intervalos de sync ou aceitar trade-off consciente.

---

## 10. Partições da flash interna

### 10.4 Tabela de partições (`partitions.csv`)

| Partição | Tamanho | Uso |
|---|---:|---|
| nvs | 24 KB | Configurações, credenciais |
| otadata | 8 KB | Estado OTA |
| phy_init | 4 KB | RF init |
| factory | 2 MB | Firmware de fábrica |
| ota_0 | 2 MB | Slot OTA A |
| ota_1 | 2 MB | Slot OTA B |

Logs grandes e áudio ficam no microSD, não na flash interna.

---

## 11. Segurança

| Item | Abordagem |
|---|---|
| Auth device → Hub | Bearer token por device (rotacionável) |
| Auth Hub → Todoist | Token REST em env |
| Auth Hub → LLM | API key em env |
| Dados em trânsito | TLS (Cloudflare Tunnel fornece HTTPS) |
| Dados em repouso | Credenciais em NVS (device); env/secrets (Hub) |
| Áudio | Nunca enviado a terceiros; Whisper local |

---

## 12. Deploy do Hub

### 12.1 Infraestrutura

- **Homeserver** do usuário (specs a levantar via SSH no M0.5: CPU, RAM, GPU).
- Docker Compose com volume persistente (`/data`: áudio + SQLite).
- Whisper: `large-v3` se GPU disponível; `medium` se só CPU.

### 12.2 Acesso remoto — Cloudflare Tunnel (decisão confirmada)

```text
Internet ──► Cloudflare Edge (TLS) ──► cloudflared (no homeserver) ──► Hub :8088
```

**Configuração:**

1. Instalar `cloudflared` no homeserver.
2. Criar tunnel no dashboard Cloudflare (ou via CLI).
3. Apontar hostname (ex.: `taskhog.seudominio.com`) → `http://localhost:8088`.
4. Device usa URL HTTPS pública no provisionamento.
5. Em casa (LAN): pode usar IP local diretamente (sem tunnel).

**Vantagens:** TLS automático, sem abrir porta no roteador, ESP32 só precisa de URL + Bearer token.

**M05-T5:** documentar URL final e testar `GET /v1/health` de fora da LAN antes de M7.

---

## 13. UI — e-Paper (200×200)

### 13.1 Telas

| ID | Nome | Quando |
|---|---|---|
| T0 | Boot/Splash | Power-on breve |
| T1 | Home/Idle | Default acordado |
| T2 | Recording | REC pressionado |
| T3 | Saved/Queued | Após soltar REC |
| T4 | Syncing | Durante upload |
| T5 | Last result | Após sync com resultado |
| T6 | Queue/Pending | Tap NAV na Home |
| T7 | Setup | Modo provisionamento |
| T8 | Low battery | Bateria crítica (3–10%) |
| T9 | Charging | USB conectado |
| T10 | Error | Erro recuperável |
| T11 | Info/Diag | Tap NAV na fila |

Wireframes T1–T7: Spec 01 §12.2. Wireframes T8–T11: §15.3 abaixo.

### 13.2 Navegação

```text
IDLE(T1) ─tap NAV→ Queue(T6) ─tap→ Info(T11) ─tap→ IDLE(T1)
   │ segura REC
   ▼
RECORDING(T2) ─solta→ Saved(T3) ─auto→ Sync(T4)/IDLE
```

---

## 14. (Reservado)

---

## 15. Wireframes — telas restantes (§15.3)

### 15.3 T8 — Low battery

```text
┌────────────────────────────┐
│ 15:30  📶○  🪫8%           │
│────────────────────────────│
│                            │
│           🪫               │
│     Bateria crítica        │
│                            │
│  Conecte o USB para        │
│  sincronizar.              │
│                            │
│  ✓ Tudo salvo localmente   │
│────────────────────────────│
│  segure ⏺ para gravar      │
└────────────────────────────┘
```

### 15.3 T9 — Charging

```text
┌────────────────────────────┐
│ 15:30  📶●  🔋45%  ⚡      │
│────────────────────────────│
│                            │
│           ⚡               │
│      Carregando…           │
│        🔋 45%              │
│                            │
│  Sincronizando fila…       │
│                            │
│────────────────────────────│
│  segure ⏺ para gravar      │
└────────────────────────────┘
```

### 15.3 T10 — Error

```text
┌────────────────────────────┐
│ 15:30  📶○  🔋78%      ↑2   │
│────────────────────────────│
│                            │
│           ⚠                │
│     Erro de conexão        │
│                            │
│  ✓ Tudo salvo localmente   │
│  Tentará de novo depois.   │
│                            │
│────────────────────────────│
│  segure ⏺ para gravar      │
└────────────────────────────┘
```

Mensagens por código de erro:

| Código | Título na tela |
|---|---|
| `E_HUB_UNREACHABLE` | Erro de conexão |
| `E_HUB_AUTH` | Token inválido |
| `E_SD_MOUNT` | Erro no cartão |
| `E_AUDIO_INIT` | Erro de áudio |

### 15.3 T11 — Info / Diagnóstico

```text
┌────────────────────────────┐
│         INFORMAÇÕES        │
│────────────────────────────│
│  ID: taskhog-01            │
│  FW: 1.0.0                 │
│  🔋 78%  SD: 58 GB livre   │
│  Hub: taskhog.exemplo.com  │
│  Último sync: 15:28 ✓      │
│  Hoje: 12  Fila: 2         │
│────────────────────────────│
│  tap → voltar              │
└────────────────────────────┘
```

---

## 16. (Reservado)

---

## 17. Configuração — `device.json`

### 17.1 Schema

Arquivo em `/sdcard/config/device.json`. Precedência: **NVS > device.json > defaults compilados**.
Credenciais sensíveis (token, PSK) ficam em **NVS**, não neste arquivo.

```json
{
  "device_id": "taskhog-01",
  "fw_version": "1.0.0",

  "audio": {
    "sample_rate_hz": 16000,
    "bits_per_sample": 16,
    "channels": 1,
    "max_duration_s": 120,
    "anti_tap_ms": 500
  },

  "sync": {
    "hub_url": "https://taskhog.seudominio.com",
    "max_attempts": 10,
    "backoff_base_s": 1,
    "backoff_max_s": 60,
    "sync_interval_min": 30,
    "retain_audio_after_done": false
  },

  "power": {
    "battery_shutdown_pct": 3,
    "battery_economy_pct": 25,
    "battery_critical_pct": 10,
    "deep_sleep_default": true
  },

  "ui": {
    "show_environment": true,
    "full_refresh_every": 10,
    "timezone": "America/Sao_Paulo"
  },

  "ota": {
    "check_on_sync": true,
    "min_battery_pct": 30,
    "url": "https://taskhog.seudominio.com/v1/firmware"
  },

  "logging": {
    "level": "info",
    "max_days": 7
  }
}
```

| Campo | Tipo | Default | Descrição |
|---|---|---|---|
| `device_id` | string | `"taskhog-01"` | Identificador único do device |
| `audio.sample_rate_hz` | int | 16000 | Taxa de amostragem |
| `audio.max_duration_s` | int | 120 | Limite de gravação |
| `audio.anti_tap_ms` | int | 500 | Duração mínima para gravar |
| `sync.hub_url` | string | — | URL HTTPS do Hub (tunnel ou LAN) |
| `sync.max_attempts` | int | 10 | Tentativas antes de `error` |
| `sync.sync_interval_min` | int | 30 | Intervalo RTC para sync periódico |
| `power.battery_shutdown_pct` | int | 3 | Bloqueia REC abaixo disso |
| `ui.show_environment` | bool | true | Mostra temp/umidade SHTC3 |
| `ui.full_refresh_every` | int | 10 | Parciais antes de refresh total |
| `ota.min_battery_pct` | int | 30 | Mínimo para OTA sem USB |

`wifi.json` (separado, mesmo diretório):

```json
{
  "networks": [
    { "ssid": "Casa", "psk": "..." },
    { "ssid": "Trabalho", "psk": "..." }
  ]
}
```

PSKs preferencialmente em NVS após provisionamento; `wifi.json` como fallback legível.

---

## 18. (Reservado)

---

## 19. Glossário

| Termo | Definição |
|---|---|
| Captura | Um evento push-to-talk (1 WAV + 1 .job) |
| Hub | Serviço FastAPI no homeserver |
| Job | Captura em processamento (device ou Hub) |
| Structuring | LLM converte transcrição → JSON de tarefas |
| Gate de áudio | Validação M0-T4 do pipeline ES8311/I2S |
| Cloudflare Tunnel | Exposição HTTPS do Hub sem abrir porta |

---

## 20. Critérios de aceite — v1.0

### 20.1 Por componente

**Device (ver também Spec 01 §18):**

- [ ] REC→gravando ≤ 300 ms; WAV válido; beeps corretos
- [ ] Offline 100% confiável; journal recupera após power-loss
- [ ] Sync idempotente; zero duplicatas
- [ ] Deep sleep default; wake REC/RTC/USB OK
- [ ] Gate de bateria respeitado
- [ ] Portal cativo funcional
- [ ] Telas T0–T11 implementadas

**Hub (ver também Spec 02 §15):**

- [ ] Pipeline completo PT-BR → JSON → Todoist
- [ ] Idempotência upload + tarefas
- [ ] Roteamento com cache real; baixa confiança → Inbox + `revisar`
- [ ] Prioridade API correta (4=alta)
- [ ] Subtarefas como filhas
- [ ] Recuperação após restart sem duplicar
- [ ] Provider LLM trocável por config

### 20.2 Aceite final v1.0 (M8)

Todos os itens abaixo devem estar verdes:

| # | Critério | Como verificar |
|---|---|---|
| A1 | Gate de áudio M0 aprovado | G1–G5 do §5.2 |
| A2 | Captura offline → sync → Todoist E2E | 5 capturas, 5 tarefas, ≤ 20 s cada (online) |
| A3 | Offline sem perda | 3 capturas sem rede → sync ao reconectar |
| A4 | Zero duplicata | Reenvio mesmo `client_job_id` + restart Hub mid-job |
| A5 | Roteamento ≥ 80% | Conjunto §2.3 frases 1–25 |
| A6 | Baixa confiança → Inbox + `revisar` | Frases 26–30 |
| A7 | Autonomia ≥ 5 dias | Medição bancada M6 (1000 mAh) |
| A8 | Provisionamento do zero | Setup via celular, sem cabo |
| A9 | Acesso remoto | Captura fora da LAN via Cloudflare Tunnel |
| A10 | OTA com rollback | Flash OTA + simular boot falho |
| A11 | Power-loss recovery | Corte energia gravando → fila íntegra no boot |
| A12 | Todas as telas T0–T11 | Inspeção visual + navegação |
| A13 | Erros tranquilizadores | T8/T10 mostram "tudo salvo localmente" |
| A14 | Métricas Hub visíveis | Latência, % roteado vs Inbox, taxa erro |
| A15 | Segurança | Segredos só em env; TLS confirmado |

---

## 21. Referências

| Documento | Caminho |
|---|---|
| Spec 01 — Firmware | `docs/specs/01-device-firmware.md` |
| Spec 02 — Hub | `docs/specs/02-hub-backend.md` |
| Spec 03 — Contratos | `docs/specs/03-data-contracts.md` |
| Roadmap | `docs/roadmap.md` |
| Hardware Waveshare | `docs/hardware/waveshare-esp32-s3-epaper-1.54-v2.md` |
| Decisões | `docs/decisions/001-initial-decisions.md` |

---

## 22. Histórico de versões

| Versão | Data | Mudanças |
|---|---|---|
| 1.0 | 2026-06-20 | Versão inicial recriada a partir dos Specs 01/02/03 |

---

## 23. Notas sobre acesso remoto

O ESP32-S3 **não executa** cliente VPN (Tailscale/WireGuard). Por isso, a exposição do Hub para redes externas requer um endpoint HTTPS acessível publicamente. A decisão v1.0 é **Cloudflare Tunnel** com `cloudflared` rodando no homeserver — o Hub permanece 100% local; apenas o túnel faz proxy TLS.

Alternativas documentadas mas não adotadas: port forward + DDNS, Tailscale subnet router.
