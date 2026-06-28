# Taskhog — Roadmap de Desenvolvimento

> **Guia de execução para o Cursor.** Companion da PRD v1.0 + Specs 01/02/03.
> Use milestone a milestone: cada tarefa é pequena o suficiente para virar uma sessão/prompt no Cursor, e cada milestone tem **critério de saída** verificável antes de avançar.

---

## 📍 Checkpoint — sessão 2026-06-27 (retomar daqui)

**Status geral:** M0 ✅ · M0.5 ✅ · M1 ✅ · M2 ✅ · M3 ✅ E2E · M4 ✅ · **M5-T1+T2 fw** · **M5-T6 hub (código)** · validação device/QA pendente

### Pendente — bateria de testes no device (antes de fechar M5)

- [ ] **M5-T1 journal:** reset durante sync → boot mostra `recovery: … → queued` → reenvio sem duplicar no Todoist
- [ ] **M5-T2 backoff:** Hub offline ou Wi-Fi ruim → logs `backoff N ms` → retentativas com delay 1s/2s/4s… → `ERROR` após 5 tentativas
- [ ] **M4 regressão:** gravar → tarefa roteada no projeto certo (ex. Compras, Magie)
- [ ] Conferir `journal/queue.jnl` no SD após enqueue + mark

### Infraestrutura operacional

| Recurso | Valor |
|---|---|
| Host Proxmox | `192.168.100.226` (SSH `root@192.168.100.226`) |
| LXC Hub (CT 101) | `192.168.100.227` — `pct enter 101` |
| Hub LAN | `http://192.168.100.227:8088/v1/health` |
| Hub remoto (Cloudflare) | **`https://hub.taskhog.win/v1/health`** ✅ |
| Domínio | `taskhog.win` · hostname Hub `hub.taskhog.win` |
| Tunnel | `taskhog` · ID `93648b38-c30c-46e4-ac34-339a5beb38f8` |
| Deploy Hub | `/opt/taskhog-hub` (Docker Compose) |
| Config cloudflared | `/etc/cloudflared/config.yml` (systemd ativo) |

Health esperado (LAN e remoto):

```json
{"ok":true,"whisper":"ready","todoist":"ok","version":"0.1.0"}
```

### Firmware (ESP32-S3 Waveshare 1.54" e-Paper V2)

| Item | Valor |
|---|---|
| Build atual | `taskhog-fw.bin` ~**0x6e6a0** bytes (pós-M0, sem gates no boot) |
| USB (Mac) | `/dev/cu.usbmodem13301` |
| Boot | init normal → `Taskhog firmware scaffold ready (state=IDLE)` |
| Gates M0 | permanecem em `main/m0_*_gate.c` para re-teste manual |

**Bugs M0 corrigidos nesta sessão:**
- **M0-T6 SD CRUD:** nomes FAT 8.3 obrigatórios (`FATFS_LFN_NONE`) — usar `m0t6.bin`, não `m0_t6_crud.bin`
- **M0-T8 RTC alarme:** PCF85063A CTRL2 — `AIE=0x80` (bit 7), `AF=0x40` (bit 6)

### Hub (Track B — pré-M2)

| Item | Status |
|---|---|
| faster-whisper `medium` | ✅ `app/services/transcribe.py` + warmup no lifespan |
| `TODOIST_TOKEN` | ✅ health `"todoist":"ok"` (API **v1**, não `rest/v2`) |
| `LLM_API_KEY` | ✅ `/models` HTTP 200 |
| Projetos Todoist documentados | ✅ `docs/setup/todoist-projects.md` |

### Docs atualizados nesta sessão

- `docs/decisions/003-m05-closeout.md` — M0.5 fechado
- `docs/decisions/002-homeserver-specs.md` — checklist Cloudflare ✅
- `docs/setup/cloudflare-tunnel.md` — runbook com valores reais
- `docs/setup/todoist-projects.md` — Inbox, Compras, Magie, Personal, MagieLive
- `docs/setup/M05-todoist-checklist.md`

### Retomar (M1)

1. ~~**M1-T1**~~ ✅ — state machine em `main/state_machine.{c,h}` + eventos em `events.h`
2. ~~**M1-T2**~~ ✅ — `ring_buffer.c` (128 KB PSRAM) + task `tk_audio` (core 1) + bridge em `app_main.c`
3. ~~**M1-T3**~~ ✅ — `tk_writer` drena ring → WAV 16 kHz/mono, flush incremental + patch header
4. ~~**M1-T4**~~ ✅ — `rec_button.c`: BOOT=REC, debounce 50 ms, anti-toque &lt;500 ms, max 120 s
5. ~~**M1-T5**~~ ✅ — `audio_beep.c`: beep curto ao gravar, duplo ao salvar (PA GPIO46)
6. ~~**M1-T6**~~ ✅ — fila SD em `components/storage/queue.{c,h}` + integração no `main/rec_worker.c`; `.job` emite os 10 campos do Spec 03 §3; nomes FAT 8.3 (`q%04u.wav/.job`) com `wav_path` apontando o arquivo real
7. ~~**M1-T7**~~ ✅ — UI reconstruída (`components/ui/`): Home/Recording/Saved/Sync, fontes SpaceMono + mascote bitmap, orientação `MIRROR_Y=1`; full refresh apenas (contador parcial e partial-refresh adiados)
8. **Pendente:** validação em device do critério de saída M1 (gravar → WAV+`.job` no SD; reabrir WAV no PC). Recuperação por journal (órfãos / `uploading→queued`) é **M5-T1**, fora do escopo M1.

---

## 0. Como usar este roadmap no Cursor

1. **Mantenha os 4 documentos no contexto do projeto** (`docs/prd/PRD_v1.0.md` + `docs/specs/01|02|03`). Eles são a fonte de verdade; este roadmap só ordena o trabalho.
2. **Trabalhe por milestone, não por arquivo.** Só comece o próximo milestone quando o critério de saída do atual estiver verde.
3. **Contratos são imutáveis durante a implementação.** Se precisar mudar um formato de dado, atualize o **Spec 03 primeiro**, depois o código (firmware e hub juntos).
4. **Padrão de prompt no Cursor** (referencie os specs explicitamente):
   > "Implemente a tarefa **M1-T3** do roadmap (WAV writer com patch de header) seguindo o **Spec 01 §8.2**. Crie `components/audio/wav_writer.{c,h}`. Header WAV 16k/16bit/mono, flush incremental, patch do tamanho ao finalizar. Sem tocar em outros módulos."
5. **Legenda de esforço:** `S` = pequeno (1 sessão) · `M` = médio · `L` = grande · `XL` = quebrar em sub-tarefas.
6. **Marque os checkboxes** conforme avança — este arquivo é o seu board.

---

## 1. Visão do caminho crítico

```text
M0 Bring-up ──(GATE de áudio)──► M0.5 Contratos+Scaffold
                                        │
                        ┌───────────────┴───────────────┐
                        ▼ (Track A · firmware)           ▼ (Track B · hub)
                  M1 Captura local                 M2 Hub MVP (Whisper→Todoist cru)
                        └───────────────┬───────────────┘
                                        ▼
                                M3 Integração online (E2E cru)
                                        ▼
                                M4 Inteligência (LLM + roteamento)
                                        ▼
                                M5 Offline-first + robustez
                                        ▼
                                M6 Energia (deep sleep + bateria)
                                        ▼
                                M7 Provisionamento + acesso remoto
                                        ▼
                                M8 Polimento + produção (OTA, telas, obs.)
```

**Paralelismo:** depois de **M0.5**, Track A (firmware) e Track B (hub) andam em paralelo até convergirem em **M3**. Se você trabalha sozinho, faça M1 e M2 alternando; a divisão ajuda a manter contexto separado no Cursor.

**Regra de risco:** **M0 bloqueia tudo.** Não escreva firmware de produto antes de validar o áudio (Spec 01 §18 / PRD §5.2).

---

## 2. Milestone 0 — Bring-up & Validação de Hardware  ✅

**Objetivo:** provar que cada periférico funciona isoladamente. Sem produto ainda — só sondas.

| # | Tarefa | Esforço | Ref |
|---|---|:---:|---|
| M0-T1 | Toolchain: instalar ESP-IDF ≥5.2, configurar Cursor, flash/monitor via USB-CDC, "blink/hello world" | M | Spec 01 §2 |
| M0-T2 | Verificar chip: `chip_id`/`flash_id` (8MB), habilitar e testar **PSRAM** (alocar buffer grande) | S | Spec 01 §2 |
| M0-T3 | Scan I2C: confirmar **ES8311 `0x18`**, SHTC3 `0x70`, RTC presente | S | Spec 01 §8.1 |
| **M0-T4** | **🔴 GATE DE ÁUDIO:** init ES8311 + I2S, gravar WAV curto no SD, ouvir e confirmar voz reconhecível | **L** | **Spec 01 §8 / PRD §5.2** |
| M0-T5 | e-Paper "hello world": identificar controlador (provável **SSD1681**), framebuffer 200×200, refresh total | M | Spec 01 §12 |
| M0-T6 | microSD: montar (reformatar **FAT32** se exFAT), CRUD de arquivo, teste de remoção no meio da escrita | M | Spec 01 §9.1 |
| M0-T7 | Bateria: ler `BAT_ADC` (GPIO4), calibrar `DIVIDER_RATIO`, esboçar curva tensão→% | M | Spec 01 §7.2 |
| M0-T8 | RTC PCF85063: set/get, persistir após power-cycle, ler bit `OS`, testar INT/alarme em GPIO5 | M | Spec 01 §13 |
| M0-T9 | Mapear GPIO real vs exemplos Waveshare; documentar divergências num `HARDWARE_NOTES.md` | S | PRD §5.4 |

**🚦 Critério de saída M0:**
- [x] **M0-T4 aprovado** (áudio gravável e audível). **Se reprovar → executar plano B (mic I2S/PDM externo nos headers) antes de seguir.**
- [x] Todos os periféricos respondem isoladamente (T1–T9).
- [x] `HARDWARE_NOTES.md` com pinos confirmados e curva de bateria inicial.

---

## 3. Milestone 0.5 — Congelar contratos & esqueleto  ✅

**Objetivo:** travar formatos e criar os dois repositórios vazios, prontos para receber código.

| # | Tarefa | Esforço | Ref | Status |
|---|---|:---:|---|:---:|
| M05-T1 | Revisar e **congelar** todos os contratos (metadata, .job, respostas API, schema LLM) | S | Spec 03 | ✅ |
| M05-T2 | Scaffold firmware: estrutura de `components/` (stubs vazios que compilam), `partitions.csv`, `sdkconfig.defaults` (PSRAM octal, USB-CDC) | M | Spec 01 §3, §15 | ✅ |
| M05-T3 | Scaffold hub: projeto FastAPI, `hub.yaml`, `Dockerfile`, `docker-compose.yml`, SQLite vazio com schema | M | Spec 02 §3–6 | ✅ |
| M05-T4 | Todoist: gerar token, criar labels `taskhog` e `revisar`, listar projetos e conferir nomes que a LLM vai mapear | S | Spec 02 §10 | ✅ |
| M05-T5 | Decidir e documentar o **caminho de acesso remoto** (Cloudflare Tunnel) + **implantar tunnel ativo** | S | PRD §14.1, Spec 02 §12.2 | ✅ |

**Infra M0.5 (2026-06-20):**

| Item | Evidência |
|---|---|
| Whisper `medium` | health `"whisper":"ready"` — `app/services/transcribe.py` |
| Todoist + labels | `"todoist":"ok"` — labels `taskhog`, `revisar` |
| LLM API key | `/models` HTTP 200 no LXC |
| Cloudflare Tunnel | `https://hub.taskhog.win/v1/health` ✅ — runbook `docs/setup/cloudflare-tunnel.md` |

**🚦 Critério de saída M0.5:**
- [x] Firmware compila e hub sobe (`GET /v1/health` responde na LAN — `192.168.100.227:8088`).
- [x] Contratos congelados (Spec 03 **v1.0**, 2026-06-20); Pydantic alinhado.
- [x] Decisão de acesso remoto registrada e **operacional** (Cloudflare Tunnel — ADR 001/003).
- [x] Labels/projetos Todoist no `.env` do Hub — health `"todoist":"ok"` (2026-06-20).
- [x] Whisper `medium` baixado — health `"whisper":"ready"` (2026-06-20).
- [x] `LLM_API_KEY` no `.env` do LXC — API `/models` HTTP 200 (2026-06-20).
- [x] Cloudflare Tunnel ativo + health HTTPS externo — `https://hub.taskhog.win/v1/health` (2026-06-20).

Ver auditoria completa: `docs/decisions/003-m05-closeout.md`.

---

## 4. Milestone 1 — Captura local (Track A · firmware)

**Objetivo:** push-to-talk grava e enfileira **100% offline**, com feedback na tela. Sem rede ainda.

| # | Tarefa | Esforço | Ref | Status |
|---|---|:---:|---|:---:|
| M1-T1 | Máquina de estados global (`BOOT/IDLE/RECORDING/FINALIZING/CONFIRM`) + eventos | M | Spec 01 §4 | ✅ |
| M1-T2 | Captura I2S→ring buffer PSRAM (`audio_capture`), task `tk_audio` em core 1 | L | Spec 01 §6, §8.2 | ✅ |
| M1-T3 | `wav_writer`: header 16k/16bit/mono, flush incremental, **patch do header** ao finalizar | M | Spec 01 §8.2 | ✅ |
| M1-T4 | Botão REC: detecção segurar/soltar, debounce, **anti-toque <0,5s**, limite **120s** | M | Spec 01 §8.3 | ✅ |
| M1-T5 | Beeps (início/duplo) ligando `PA_EN` só no instante (cuidado GPIO46 strapping) | S | Spec 01 §8.1, §8.3 | ✅ |
| M1-T6 | Fila no SD: `queue_enqueue/peek/mark/complete`, criar `.job` (schema Spec 03 §3) | M | Spec 01 §9.2, Spec 03 §3 | ✅ |
| M1-T7 | UI: telas **T1 Home**, **T2 Recording**, **T3 Saved** (contador parcial / partial-refresh adiados) | M | Spec 01 §12.2 | ✅ |

**🚦 Critério de saída M1:**
- [ ] Segurar REC → gravando em ≤300ms; soltar → WAV válido no SD + `.job` criado.
- [ ] Toque acidental descartado; corte automático em 120s.
- [ ] Reabrir o WAV no PC e ouvir corretamente.
- [ ] Tela mostra Home/Recording/Saved sem rede nenhuma.

---

## 5. Milestone 2 — Hub MVP (Track B · backend)

**Objetivo:** receber um WAV (via `curl`), transcrever e criar uma tarefa **crua** (texto puro) no **Inbox** do Todoist. Sem LLM ainda.

| # | Tarefa | Esforço | Ref | Status |
|---|---|:---:|---|:---:|
| M2-T1 | FastAPI + auth Bearer + `POST /v1/recordings` (multipart), salva WAV, cria job `queued` | M | Spec 02 §5.1, §6 | ✅ |
| M2-T2 | Worker `asyncio` consumindo `jobs` + persistência de transições | M | Spec 02 §7 | ✅ |
| M2-T3 | Integração **faster-whisper** (`transcribe`), `language=pt`, `vad_filter` | M | Spec 02 §8 | ✅ |
| M2-T4 | Cliente Todoist mínimo: criar tarefa com o transcript no Inbox + label `taskhog` | M | Spec 02 §10.2 | ✅ |
| M2-T5 | `GET /v1/recordings/{id}`, `GET /v1/status`, `GET /v1/health` | M | Spec 02 §5.2–5.4 | ✅ |
| M2-T6 | Idempotência por `(device_id, client_job_id)` | S | Spec 02 §5.1, Spec 03 §10 | ✅ |

**Notas de implementação (2026-06-22):**
- Caminho **cru** (sem LLM): `queued → transcribing → creating → done`; tarefa vai pro Inbox com label `taskhog` (LLM/roteamento são M4).
- **Todoist unified API v1** (`/api/v1`) — REST v2 desligada 2026-02-10; `hub.yaml`/Spec 02 §4/§10 atualizados.
- Worker `asyncio` single-job; fila persiste no DB; recovery no boot (`reset_stuck_jobs`: in-flight → `queued`).
- Idempotência: `UNIQUE(device_id, client_job_id)` + reenvio devolve job existente (200 `duplicate:true`); `X-Request-Id` por tarefa cobre restart no meio do `creating`.
- Testes: `taskhog-hub/tests/test_m2_pipeline.py` (Whisper+Todoist mockados) — 5/5 verdes.

**🚦 Critério de saída M2 — ✅ validado live (2026-06-22, `hub.taskhog.win`):**
- [x] `curl` enviando um WAV → tarefa no Inbox do Todoist com o texto transcrito ("Compra pão!", label `taskhog`).
- [x] Reenvio do mesmo `client_job_id` → `duplicate:true`, **não** duplica a tarefa.
- [x] `/v1/status` reflete fila e processados (`processed_today`, `last_task`).

Script de validação: `taskhog-hub/scripts/test_e2e.sh`.

---

## 6. Milestone 3 — Integração online (E2E cru)

**Objetivo:** conectar Track A + Track B. Gravar no device → tarefa no Todoist (ainda no Inbox, sem inteligência). Online apenas.

| # | Tarefa | Esforço | Ref | Status |
|---|---|:---:|---|:---:|
| M3-T1 | Wi-Fi STA: conectar a 1 rede fixa (hardcoded por enquanto), `GET /v1/health` | M | Spec 01 §10.1 | ✅ |
| M3-T2 | `http_uploader`: `POST` multipart (wav + metadata), streaming do SD, TLS | L | Spec 01 §10.2, Spec 03 §2 | ✅ |
| M3-T3 | Sync básico pós-captura: se há rede → envia; senão mantém na fila | M | Spec 01 §10.3 | ✅ |
| M3-T4 | Device consulta status e atualiza contadores na Home (T1) | S | Spec 01 §10.3, §12 | ✅ |
| M3-T5 | Teste E2E manual: gravar 5 capturas → 5 tarefas no Todoist | S | — | ⏳ device |

**Notas de implementação (2026-06-22) — código completo, validação em device pendente:**
- `components/net/` deixou de ser stub: `wifi_sta` (STA on-demand, event group, ícone na barra de status), `http_uploader` (health + upload multipart com streaming do WAV do SD, TLS via cert bundle Mozilla), `sync_engine` (task + drenagem snapshot da fila).
- **Config via Kconfig** (`main/Kconfig.projbuild` → menu "Taskhog"): `TASKHOG_WIFI_SSID/PASSWORD`, `TASKHOG_HUB_URL` (default `https://hub.taskhog.win`), `TASKHOG_DEVICE_TOKEN`, `TASKHOG_SYNC_MAX_ATTEMPTS`. Segredos ficam no `sdkconfig` (gitignored), setados com `idf.py menuconfig`.
- Drenagem (`sync_engine_drain`): conecta Wi-Fi → `health` → snapshot de pendentes (FIFO, ids) → por job `mark UPLOADING` → upload → `mark UPLOADED`+`hub_recording_id` ou (≥max) `ERROR` / senão volta a `QUEUED`. Sem starvation (snapshot, não peek-lowest).
- Disparo: ao entrar em `SYNC` → `sync_engine_trigger`; auto-sync após `CONFIRM`/`BOOT` quando há fila pendente (sem loop: guardado por estado anterior). Conclusão → `SYNC_DONE`.
- Idempotência ponta-a-ponta: device envia `client_job_id`; Hub deduplica (validado no M2).
- **Fora do escopo M3** (adiado): NTP→RTC (§10.4), multi-AP/`wifi.json` (§10.1 usa rede fixa), backoff exponencial fino (M3 reenvia no próximo trigger), polling de `/v1/recordings/{id}` p/ mover `sent/` (M5).

**🚦 Critério de saída M3 (validar no device):**
- [ ] Falar no device → tarefa no Todoist em ≤20s (online).
- [ ] Sem rede → fica na fila; ao voltar a rede → envia (drenagem simples já funciona).
- [ ] Contadores da Home batem com o Hub.

---

## 7. Milestone 4 — Inteligência (Track B · LLM + roteamento)

**Objetivo:** transformar transcrição em tarefa **estruturada e roteada** ao projeto correto. É aqui que o produto "fica esperto".

| # | Tarefa | Esforço | Ref |
|---|---|:---:|---|
| M4-T1 | Cache de projetos/labels do Todoist (`/projects`, `/labels`) + refresh periódico | M | Spec 02 §10.1 |
| M4-T2 | Interface `LLMProvider` + impl **cloud** (OpenAI-compatible, JSON mode) | M | Spec 02 §9 |
| M4-T3 | `structuring`: montar prompt completo (system+user+few-shots) injetando projetos/labels | M | Spec 03 §6 |
| M4-T4 | Validação do JSON de saída contra o schema + 1 retry de correção | M | Spec 02 §9.3, Spec 03 §5.1 |
| M4-T5 | `confidence` + roteamento: projeto vs **Inbox + `revisar`** | M | Spec 02 §11, Spec 03 §7.2 |
| M4-T6 | Campos adaptativos: `due_string`(`due_lang=pt`), `priority` (**gotcha 4=alta**), labels, **subtarefas** (parent_id) | L | Spec 02 §10.2/§10.4, Spec 03 §7 |
| M4-T7 | Impl **ollama** do `LLMProvider` (paridade de prompt) — opcional nesta fase | M | Spec 02 §9.2 |
| M4-T8 | Conjunto de teste: ~30 frases reais (simples e ricas) p/ medir acurácia de roteamento | M | PRD §2 |

**🚦 Critério de saída M4:**
- [ ] "Comprar pão" → tarefa simples; "Criar apresentação BB quinta, prioridade alta" → projeto + prazo + prioridade 4.
- [ ] Frase com 2 ações → 2 tarefas.
- [ ] Baixa confiança → Inbox + `revisar`.
- [ ] Acurácia de roteamento **≥80%** no conjunto de teste.
- [ ] Trocar `provider: cloud → ollama` só por config (se M4-T7 feito).

---

## 8. Milestone 5 — Offline-first & robustez

**Objetivo:** garantir o contrato sagrado — **zero capturas perdidas**, **zero tarefas duplicadas**, mesmo com quedas de energia e rede.

| # | Tarefa | Esforço | Ref |
|---|---|:---:|---|
| M5-T1 | Journal append-only + recuperação no boot (jobs órfãos/"uploading"→"queued") | L | Spec 01 §9.3 |
| M5-T2 | Sync engine completo: FIFO, **backoff exponencial**, `MAX_ATTEMPTS`, marcação de erro | M | Spec 01 §10.3 |
| M5-T3 | Idempotência por tarefa no Hub (`idempotency_key` por task) | M | Spec 02 §10.5, Spec 03 §10 |
| M5-T4 | Wi-Fi multi-AP (casa/trabalho/hotspot), seleção por RSSI | M | Spec 01 §10.1 |
| M5-T5 | Retenção/limpeza de WAV após `done` (`retain_audio_days`) | S | Spec 01 §9.2, Spec 02 §4 |
| M5-T6 | Recuperação do Hub: reiniciar no meio de `creating` não duplica tarefa | M | Spec 02 §7, §10.5 |
| M5-T7 | Testes de caos: corte de energia gravando; rede caindo no upload; SD removido | L | — |

**🚦 Critério de saída M5:**
- [ ] Bateria/queda no meio da gravação → no boot, fila íntegra (WAV incompleto sinalizado, não perdido).
- [ ] Rede caindo durante upload → reenvio sem duplicar.
- [ ] Hub reiniciado no meio do job → resultado correto, sem tarefa dupla.

---

## 9. Milestone 6 — Energia (deep sleep + bateria)

**Objetivo:** transformar o protótipo "sempre ligado" em um device portátil que dorme por padrão e dura dias.

| # | Tarefa | Esforço | Ref |
|---|---|:---:|---|
| M6-T1 | Deep sleep como default + despacho por causa de wake no boot | L | Spec 01 §5, §7.1 |
| M6-T2 | Wake sources: REC (ext0), RTC_INT/alarme (ext1, GPIO5), USB, timer interno | M | Spec 01 §7.1 |
| M6-T3 | Caminho rápido REC→RECORDING ≤300ms (sem montar Wi-Fi/tela cheia antes) | M | Spec 01 §5 |
| M6-T4 | Gate de bateria (faixas Normal/Economia/Crítico/Shutdown) + `SAFE_OFF` | M | Spec 01 §7.3 |
| M6-T5 | Sync periódico via alarme do RTC quando há fila pendente | M | Spec 01 §7.1 |
| M6-T6 | Estado mínimo em `RTC_DATA_ATTR` (contador de parciais, flag de fila) | S | Spec 01 §7.1 |
| M6-T7 | **Medição em bancada:** corrente em DEEP_SLEEP/RECORDING/SYNC; estimar autonomia | M | PRD §9.5 |

**🚦 Critério de saída M6:**
- [ ] Device dorme sozinho; acorda por REC/RTC/USB corretamente.
- [ ] Captura nunca sacrificada acima do limiar de shutdown.
- [ ] Consumo medido; **autonomia ≥ meta (≥5 dias no perfil típico com 1000 mAh)** — ou decisão consciente de ajustar bateria/intervalos.

---

## 10. Milestone 7 — Provisionamento & acesso remoto

**Objetivo:** configurar o device sem touch e fazê-lo funcionar **fora de casa**.

| # | Tarefa | Esforço | Ref |
|---|---|:---:|---|
| M7-T1 | SoftAP `Taskhog-Setup` + DNS captive + `esp_http_server` | M | Spec 01 §11 |
| M7-T2 | Página de config (HTML embutido): scan de redes, salvar SSID/PSK + **URL do Hub + token** | M | Spec 01 §11.2 |
| M7-T3 | Entrada no modo setup: sem credencial **ou** BOOT segurado no power-on; tela **T7** | S | Spec 01 §11.1, §12 |
| M7-T4 | Validar conexão + `GET /v1/health` antes de salvar; persistir em `wifi.json`/NVS | S | Spec 01 §11.2, §14 |
| M7-T5 | Acesso remoto: Cloudflare Tunnel + teste **fora da LAN** | M | Spec 02 §12.2 | ✅ feito em M0.5 (`hub.taskhog.win`) |
| M7-T6 | NTP→RTC ao sincronizar; ajustar datas relativas quando `rtc_valid=false` | M | Spec 01 §10.4, Spec 02 §8 |

**🚦 Critério de saída M7:**
- [ ] Provisionar do zero pelo celular (sem cabo) funciona.
- [ ] Capturar fora de casa → tarefa chega ao Todoist via acesso remoto.
- [ ] Relógio sincroniza por NTP e sobrevive a power-cycle.
- [x] Tunnel Cloudflare operacional (`https://hub.taskhog.win`) — adiantado em M0.5.

---

## 11. Milestone 8 — Polimento & produção

**Objetivo:** robustez de produção, telas restantes, atualização de campo e observabilidade.

| # | Tarefa | Esforço | Ref |
|---|---|:---:|---|
| M8-T1 | OTA A/B via `esp_https_ota` (só com bateria>30% ou USB) + rollback automático | L | Spec 01 §15, Spec 02 §5.5 |
| M8-T2 | Telas restantes: **T4 Sync, T5 Last result, T6 Queue, T8 Low batt, T9 Charging, T10 Error, T11 Info** | M | Spec 01 §12.2 |
| M8-T3 | Disciplina de refresh: parcial vs total periódico, anti-ghosting | M | Spec 01 §12.1 |
| M8-T4 | Observabilidade do Hub: logs estruturados por job + métricas (latência, % Inbox vs roteado, taxa de erro) | M | Spec 02 §13 |
| M8-T5 | Logging no device + tela diagnóstico (T11) | S | Spec 01 §16 |
| M8-T6 | Tratamento de erros end-to-end conforme taxonomia (mensagens "tudo salvo localmente") | M | Spec 03 §9 |
| M8-T7 | Ajuste fino do `threshold` de confiança observando métricas reais | S | Spec 02 §11 |
| M8-T8 | Hardening de segurança: rotação de token, segredos só em env, TLS confirmado | S | Spec 02 §14 |

**🚦 Critério de saída M8 (= v1.0 pronta):**
- [ ] Todos os critérios de aceite da PRD §20.2 + Spec 01 §18 + Spec 02 §15 verdes.
- [ ] OTA testado com rollback.
- [ ] Todas as telas T0–T11 implementadas.
- [ ] Métricas do Hub visíveis; threshold ajustado.

---

## 12. Estratégia de teste por fase

| Fase | Foco de teste | Como |
|---|---|---|
| M0 | Hardware isolado | Sondas manuais, osciloscópio/multímetro p/ bateria |
| M1 | Integridade de áudio/fila | Ouvir WAVs no PC; inspecionar `.job` |
| M2 | Pipeline do Hub | `curl` com WAVs de amostra; checar Todoist |
| M3 | E2E feliz | Gravar→Todoist; cronometrar latência |
| M4 | Acurácia | Conjunto de ~30 frases; medir % roteamento + campos |
| M5 | **Caos** | Cortes de energia/rede; verificar zero-perda/zero-dupla |
| M6 | Energia | Bancada de medição; teste de autonomia multi-dia |
| M7 | Provisionamento/remoto | Setup do zero; teste fora da LAN |
| M8 | Regressão + produção | Rodar todos os critérios de aceite |

---

## 13. Checklist mestre (resumo)

- [x] **M0** Bring-up + gate de áudio
- [x] **M0.5** Contratos congelados + scaffolds + infra Hub (Todoist, Whisper, Cloudflare)
- [ ] **M1** Captura local offline (firmware) ← **próximo**
- [ ] **M2** Hub MVP (Whisper→Todoist cru)
- [ ] **M3** Integração online E2E
- [ ] **M4** Inteligência (LLM + roteamento ≥80%)
- [ ] **M5** Offline-first + robustez (zero-perda / zero-dupla)
- [ ] **M6** Energia (deep sleep + autonomia)
- [ ] **M7** Provisionamento + acesso remoto
- [ ] **M8** Polimento + produção (OTA, telas, observabilidade)

---

## 14. Atalhos de prompt para o Cursor (exemplos prontos)

- **Implementar tarefa:**
  > "Implemente **M2-T3** (faster-whisper) conforme **Spec 02 §8**. Crie `app/services/transcribe.py` com função `transcribe(wav_path)->str`, `language=pt`, `vad_filter`. Carregue o modelo a partir do `hub.yaml`. Inclua tratamento de erro `H_TRANSCRIBE_FAIL` (Spec 03 §9.2)."
- **Revisar contra contrato:**
  > "Revise `http_uploader.c` contra **Spec 03 §2** (metadata) e **Spec 01 §10.2**. Confirme campos obrigatórios, streaming do SD e idempotência via `client_job_id`."
- **Fechar um milestone:**
  > "Liste o que falta para cumprir o **critério de saída do M4** (roadmap §7) e proponha testes para os itens em aberto."
- **Mudança de contrato (fluxo correto):**
  > "Preciso adicionar o campo X ao schema da LLM. Atualize primeiro o **Spec 03 §5**, depois ajuste `structuring.py` (hub) e o parser correspondente. Não altere outros contratos."

---

*Sequência recomendada de início: **M0-T1 → M0-T4 (gate de áudio)**. Não avance para M1/M2 sem o áudio validado — é o único risco capaz de invalidar o produto, e o roadmap inteiro foi ordenado para descobrir isso primeiro.*
