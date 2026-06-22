# ADR 002 — Specs do Homeserver

**Data:** 2026-06-20  
**Status:** Aceito · **LXC provisionado em 2026-06-20**  
**Host PVE:** `root@192.168.100.226` (LAN)  
**LXC Hub:** `taskhog-hub` — CT **101** @ `192.168.100.227`

## Hardware coletado

| Item | Valor |
|---|---|
| **Hostname** | `homeserver` |
| **OS** | Debian 13 (trixie) — **Proxmox VE 9.2.3** (kernel `7.0.6-2-pve`) |
| **CPU** | Intel Core i5-8400T @ 1.70 GHz — **6 cores**, sem HT |
| **RAM** | **31 GiB** total · ~21 GiB disponível |
| **Disco** | 94 GB LVM (`pve-root`) · **84 GB livres** |
| **GPU** | Intel UHD 630 integrada — **sem NVIDIA/CUDA** |
| **Rede LAN** | `192.168.100.226` |

## Software presente (no host PVE)

| Ferramenta | Status |
|---|---|
| Docker | ❌ não instalado |
| Docker Compose | ❌ não instalado |
| cloudflared | ❌ não instalado |
| python3 | ✅ **3.13.5** no host PVE (Hub rodará no LXC com imagem própria) |
| nvidia-smi | ❌ não aplicável |

> **Nota:** o script de discovery teve corrupção de colagem na seção GPU/DOCKER (linhas concatenadas). Os dados acima foram extraídos das seções que rodaram corretamente.

## Implicações para o Taskhog Hub

### Whisper (sem GPU)

| Parâmetro | Recomendação | Justificativa |
|---|---|---|
| `model` | **`medium`** | i5-8400T @ 1.7 GHz é CPU-only; `medium` equilibra qualidade PT-BR e latência |
| `device` | `cpu` | Sem CUDA |
| `compute_type` | `int8` | Menor RAM e mais rápido em CPU |
| `language` | `pt` | Conforme Spec 02 |
| `vad_filter` | `true` | Push-to-talk com silêncio nas pontas |

**Alternativa:** `large-v3` + `int8` se priorizar qualidade sobre latência. Em clipes de ~10 s, pode adicionar 5–15 s só na transcrição — risco de estourar o alvo de ≤20 s E2E (PRD §6.3).

**Não usar:** `device: cuda` ou `compute_type: float16` — hardware não suporta.

### Latência estimada (CPU medium int8, clipe ~10 s)

| Etapa | Estimativa |
|---|---|
| Upload (LAN) | < 1 s |
| Whisper `medium` int8 | 3–8 s |
| LLM cloud (structuring) | 2–5 s |
| Todoist API | 1–2 s |
| **Total** | **~8–16 s** ✅ dentro do alvo ≤20 s |

### LLM

- Confirmado: **cloud OpenAI-compatible** (ADR 001).
- CPU local irrelevante para LLM — chamada HTTP externa.

### Armazenamento Hub

| Recurso | Estimativa |
|---|---|
| Modelo Whisper `medium` | ~1.5 GB (download na primeira execução) |
| SQLite + áudio (`retain_audio_days: 7`) | < 5 GB típico |
| **Volume `/data` sugerido** | **20 GB** (folga para logs e crescimento) |

Disco com 84 GB livres no host — espaço mais que suficiente.

### RAM

31 GiB no host; o Hub em container/VM precisa de **4–8 GiB** alocados:
- Whisper `medium` int8: ~2–4 GiB em pico
- FastAPI + worker: < 500 MiB

## Deploy — Proxmox VE

O host é **hypervisor Proxmox**, não um servidor Debian “puro”. **Não instalar Docker diretamente no host PVE** (prática desencorajada — mistura carga com o hypervisor).

### LXC Debian 13 — **provisionado** ✅

| Parâmetro | Valor |
|---|---|
| CT ID | **101** |
| Hostname | `taskhog-hub` |
| IP LAN | `192.168.100.227/24` |
| Gateway | `192.168.100.1` |
| CPU / RAM | 4 cores / 8192 MiB |
| Disco | 32 GB (`local-lvm`) |
| Features | `nesting=1`, `keyctl=1` |
| Status | `running`, `onboot=1` |

### Cloudflare Tunnel

Instalar `cloudflared` **dentro do mesmo LXC/VM** do Hub (ou no host, apontando para o IP do LXC):

```text
Internet → Cloudflare Edge → cloudflared → Hub :8088
```

Configurar em **M05-T5** / **M7**.

## `hub.yaml` — defaults recomendados para este hardware

```yaml
whisper:
  model: "medium"
  device: "cpu"
  compute_type: "int8"
  language: "pt"
  vad_filter: true

llm:
  provider: "cloud"
  # modelo: definir conforme provider escolhido (ex.: gpt-4o-mini)
```

## Software no LXC `taskhog-hub` (CT 101) — provisionado

| Ferramenta | Versão |
|---|---|
| Docker | 29.6.0 |
| Docker Compose | v5.1.4 |
| cloudflared | 2026.6.1 |
| Disco `/` | 32 GB (29 GB livres) |

No host PVE: python3 3.13.5 (Hub usa imagem Docker própria).

## Checklist de setup (M0.5 / M05)

- [x] Criar LXC Debian 13 no Proxmox (CT 101, 4 CPU, 8 GB RAM, 32 GB disco)
- [x] Instalar Docker + Compose no LXC
- [x] Instalar `cloudflared` no LXC
- [x] Reservar IP LAN estável: `192.168.100.227`
- [x] Criar diretório `/data` no LXC
- [x] Scaffold `taskhog-hub/` e subir `/v1/health`
- [x] Preencher `TODOIST_TOKEN` no `.env` do LXC — ✅ health `"todoist":"ok"` (2026-06-20)
- [x] Preencher `LLM_API_KEY` no `.env` do LXC — ✅ `/models` HTTP 200 (2026-06-20)
- [x] Download do modelo Whisper `medium` — ✅ health `"whisper":"ready"` (2026-06-20)
- [x] Configurar Cloudflare Tunnel — `https://hub.taskhog.win` (2026-06-20)
- [x] Testar `GET /v1/health` de fora da LAN — health OK via HTTPS

## Acesso ao LXC

```bash
# Do Mac (via host PVE)
ssh root@192.168.100.226 "pct enter 101"

# Ou executar comando remoto
ssh root@192.168.100.226 "pct exec 101 -- docker ps"
```

Hub URL LAN: `http://192.168.100.227:8088`  
Deploy path no LXC: `/opt/taskhog-hub`

### Device token (gerado no deploy M0.5)

Salvo em `/opt/taskhog-hub/.env` no LXC como `TASKHOG01_TOKEN`.  
**Não commitar.** Anotar em gerenciador de senhas para provisionamento do ESP32.

```
26c59f1c0856c01cb03a9b204d19e0f46bbd164bb8f58ae50624fc7700e2f695
```
