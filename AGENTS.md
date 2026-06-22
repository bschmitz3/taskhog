# Taskhog — Guia para Agentes (Cursor)

Dispositivo ESP32-S3 com e-Paper que grava notas de voz offline e sincroniza com um **Hub** self-hosted (FastAPI + Whisper + LLM) que cria tarefas no Todoist.

## Documentação canônica

| Documento | Caminho | Quando consultar |
|---|---|---|
| **PRD** | `docs/prd/PRD_v1.0.md` | Visão de produto, requisitos, aceite |
| Spec 01 — Firmware | `docs/specs/01-device-firmware.md` | ESP-IDF, áudio, fila, UI, energia |
| Spec 02 — Hub | `docs/specs/02-hub-backend.md` | API, pipeline, Whisper, Todoist |
| Spec 03 — Contratos | `docs/specs/03-data-contracts.md` | **Fonte de verdade** para formatos JSON |
| Roadmap | `docs/roadmap.md` | Ordem de implementação (M0→M8) |
| Hardware | `docs/hardware/waveshare-esp32-s3-epaper-1.54-v2.md` | GPIOs, bring-up, periféricos |
| Decisões | `docs/decisions/` | ADRs registradas |
| Setup M0.5 | `docs/setup/M05-todoist-checklist.md` | Todoist |
| Todoist projetos | `docs/setup/todoist-projects.md` | Nomes para LLM |
| Cloudflare Tunnel | `docs/setup/cloudflare-tunnel.md` | Acesso remoto Hub |
| Homeserver specs | `docs/decisions/002-homeserver-specs.md` | Whisper, deploy Proxmox |

## Regras de ouro

1. **Captura nunca se perde** — offline-first; rede e IA são best-effort.
2. **Contratos imutáveis durante implementação** — mudança → atualizar **Spec 03** primeiro, depois firmware + hub juntos.
3. **M0 bloqueia tudo** — validar áudio (gate M0-T4) antes de firmware de produto.
4. **Firmware burro, Hub inteligente** — sem IA/NLP/Todoist no device.
5. **Idempotência** — `(device_id, client_job_id)` no upload; `idempotency_key` por tarefa no Todoist.

## Estrutura do monorepo (alvo)

```text
taskhog-fw/     # ESP-IDF ≥5.2 — M0 ✅ · scaffold M0.5 ✅
taskhog-hub/    # Python 3.11+ FastAPI — deploy M0.5 ✅ (LAN :8088)
docs/           # especificações e decisões
```

## Por onde começar

1. **M0** — bring-up hardware ✅
2. **M0.5** — scaffold + contratos + infra Hub ✅ (`docs/decisions/003-m05-closeout.md`)
3. **M1** — captura local offline: **código completo** (T1–T7); falta **validar no device** (gravar → WAV+`.job`; reabrir WAV no PC)
4. **M2** — Hub MVP (Track B): **código completo** (caminho cru, Todoist v1, idempotência; testes em `taskhog-hub/tests/`); falta **validação live** (`curl` WAV → tarefa no Inbox) no Hub deployado
5. **M3** — integração E2E (device → Hub → Todoist) ← **próximo** após validar M1+M2

**URLs Hub:** LAN `http://192.168.100.227:8088` · remoto `https://hub.taskhog.win`

## Decisões já tomadas

Ver `docs/decisions/001-initial-decisions.md` e `002-homeserver-specs.md`.
