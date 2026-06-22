# ADR 001 — Decisões iniciais do projeto

**Data:** 2026-06-20  
**Status:** Aceito

## Contexto

Decisões tomadas na fase de planejamento, antes da implementação (M0).

## Decisões

### 1. Repositório: monorepo

- `taskhog-fw/` (ESP-IDF) e `taskhog-hub/` (FastAPI) no mesmo repositório.
- Facilita mudanças de contrato sincronizadas (Spec 03).

### 2. LLM: cloud API (OpenAI-compatible)

- Provider inicial: endpoint OpenAI-compatible com JSON mode.
- Ollama local fica como opção futura (M4-T7), só por config.
- Segredos (`LLM_API_KEY`) apenas no Hub via env.

### 3. Bateria: 1000 mAh

- Capacidade confirmada: **1000 mAh** (substitui referência de 500 mAh no doc de hardware).
- Meta de autonomia do roadmap M6 (≥5 dias) permanece válida.

### 4. Acesso remoto: Cloudflare Tunnel ✅

**Decisão confirmada (2026-06-20):**

- **Cloudflare Tunnel** (`cloudflared` no homeserver) expõe o Hub via HTTPS.
- Hub roda 100% local; apenas o túnel faz proxy TLS na borda Cloudflare.
- ESP32 usa URL HTTPS pública no provisionamento (ex.: `https://taskhog.seudominio.com`).
- Em LAN: pode usar IP local diretamente.
- Documentar URL final e testar `GET /v1/health` de fora da LAN em **M05-T5**, antes de M7.

### 5. PRD v1.0 ✅

- Recriada em `docs/prd/PRD_v1.0.md` (2026-06-20).

### 6. Hardware confirmado

- Microfone e speaker **fisicamente montados** na placa.
- Conta Todoist ativa com projetos nomeados.
- Homeserver acessível via SSH para levantar specs (CPU/GPU/RAM) no M0.5.

## Consequências

- M05-T5: configurar Cloudflare Tunnel e testar health de fora da LAN — **M7** (decisão registrada em M0.5).
- PRD disponível em `docs/prd/PRD_v1.0.md`.
