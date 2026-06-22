# ADR 003 — Fechamento M0.5 (contratos + scaffold)

**Data:** 2026-06-20  
**Status:** Aceito · **M0.5 fechado**

## Objetivo

Congelar contratos (Spec 03), validar scaffolds firmware + hub, registrar decisão de acesso remoto, preparar Todoist para M2.

## Auditoria M05-T1 — Contratos (Spec 03 v1.0 congelado)

| Contrato | Spec 03 | Implementação scaffold | Status |
|---|---|---|---|
| Metadata upload | §2 | `RecordingMetadata` em `schemas.py` | ✅ alinhado |
| Arquivo `.job` | §3 | `queue.c` stub — implementar M1-T6 | ✅ spec pronto |
| Respostas API | §4 | `HealthResponse`, `StatusResponse`, `RecordingAccepted`, `RecordingDetail` | ✅ alinhado |
| Saída LLM | §5 | `structuring.py` stub | ✅ spec pronto |
| Estados device/job | §8 | `state_machine.c` stub | ✅ spec pronto |
| Erros | §9 | códigos documentados | ✅ spec pronto |
| Idempotência | §10 | `recordings.py` stub M2 | ✅ spec pronto |

**Regra ativa:** mudança de formato → Spec 03 primeiro → firmware + hub.

## M05-T2 — Firmware scaffold ✅

| Item | Evidência |
|---|---|
| `components/` (audio, storage, net, ui, rtc, …) | 81 arquivos; stubs + drivers M0 |
| `partitions.csv` | factory 2M + OTA A/B |
| `sdkconfig.defaults` | PSRAM octal, USB-CDC, VFS_MAX_COUNT=16 |
| Build | `taskhog-fw.bin` **0x6e6a0** (2026-06-20, pós-M0) |
| Boot | `Taskhog firmware scaffold ready (state=IDLE)` |

## M05-T3 — Hub scaffold ✅

| Item | Evidência |
|---|---|
| FastAPI + routers | `app/main.py`, `/v1/health`, `/v1/status` |
| Docker | `Dockerfile`, `docker-compose.yml`, `hub.yaml` |
| SQLite | `app/models/db.py`, schema inicial |
| Deploy LXC | CT 101 @ `192.168.100.227`, path `/opt/taskhog-hub` |
| Health LAN | `GET http://192.168.100.227:8088/v1/health` → `{"ok":true,...,"version":"0.1.0"}` |

## M05-T4 — Todoist ✅ (2026-06-20)

- Labels `taskhog` e `revisar` criadas (API v1)
- `TODOIST_TOKEN` no `/opt/taskhog-hub/.env` do LXC CT 101
- Health: `GET http://192.168.100.227:8088/v1/health` → `"todoist":"ok"`

## M05-T5 — Acesso remoto (Cloudflare Tunnel)

**Decisão:** ADR 001 §4 — Cloudflare Tunnel no LXC CT 101.

| Item | Status |
|---|---|
| `cloudflared` instalado | ✅ |
| Runbook | `docs/setup/cloudflare-tunnel.md` |
| Tunnel + DNS + serviço systemd | ✅ `taskhog` / `93648b38-c30c-46e4-ac34-339a5beb38f8` |
| URL pública | ✅ `https://hub.taskhog.win/v1/health` (2026-06-20) |

## Critérios de saída M0.5

- [x] Firmware compila; hub responde `/v1/health` na LAN
- [x] Spec 03 v1.0 congelado; Pydantic alinhado
- [x] Decisão de acesso remoto registrada
- [x] Todoist + labels + projetos documentados (`docs/setup/todoist-projects.md`)
- [x] Whisper `medium` baixado; health `"whisper":"ready"`
- [x] `LLM_API_KEY` no `.env` do LXC — API `/models` HTTP 200
- [x] Cloudflare Tunnel ativo + health HTTPS externo — `https://hub.taskhog.win/v1/health`

## Próximo milestone

**M1 — Captura local** (Track A): REC → WAV + `.job` offline, telas e-Paper.
