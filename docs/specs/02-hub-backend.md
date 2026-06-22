# Taskhog — Spec 02: Backend (Taskhog Hub)

> **Companion da PRD v1.0.** Aprofunda o serviço que roda no homeserver: API, pipeline de processamento, Whisper, LLM plugável, integração Todoist, persistência e deploy.
> **Alvo:** Python 3.11+ · FastAPI · Docker · exposto ao device via Tailscale.

---

## 1. Escopo

O Hub é **onde mora a inteligência**. Ele recebe áudio do device, transcreve (Whisper local), interpreta (LLM), resolve o roteamento contra os projetos reais do usuário e cria a(s) tarefa(s) no Todoist. Mantém o estado dos jobs e os contadores que o device mostra na tela.

Contrato fundamental: **idempotência** (nunca duplicar tarefa) e **observabilidade** (todo job é auditável: áudio → transcrição → JSON da LLM → tarefa criada).

---

## 2. Stack

| Camada | Escolha | Notas |
|---|---|---|
| API | FastAPI + Uvicorn | async, multipart nativo (`UploadFile`) |
| Fila/worker | `asyncio` task + tabela SQLite (`jobs`) | simples; trocável por Redis/RQ se escalar |
| Transcrição | `faster-whisper` (CTranslate2) | GPU se houver; CPU ok p/ clipes ≤120s |
| LLM | Provider plugável (cloud OU Ollama local) | interface única (§9) |
| Persistência | SQLite (WAL) | jobs + cache de projetos/labels |
| Armazenamento de áudio | filesystem local (`/data/audio`) | retenção configurável |
| Deploy | Docker Compose | + Tailscale no host |

---

## 3. Estrutura do projeto

```text
taskhog-hub/
├── docker-compose.yml
├── Dockerfile
├── hub.yaml                  # config (ver §4)
├── pyproject.toml
└── app/
    ├── main.py               # FastAPI app, rotas
    ├── config.py             # carrega hub.yaml + env
    ├── deps.py               # auth (Bearer device_token)
    ├── api/
    │   ├── recordings.py     # POST /v1/recordings, GET /v1/recordings/{id}
    │   ├── status.py         # GET /v1/status, /v1/health
    │   └── admin.py          # POST /v1/projects/refresh, /v1/firmware (opc.)
    ├── worker/
    │   ├── queue.py          # enfileira/consome jobs
    │   └── pipeline.py       # transcribe → structure → resolve → create
    ├── services/
    │   ├── transcribe.py     # faster-whisper
    │   ├── llm/
    │   │   ├── base.py       # Protocol LLMProvider
    │   │   ├── cloud.py      # provider de nuvem (OpenAI-compatible)
    │   │   └── ollama.py     # provider local
    │   ├── structuring.py    # monta prompt, valida JSON de saída
    │   ├── todoist.py        # cliente REST v2 + cache
    │   └── confidence.py     # regra de threshold
    ├── models/
    │   ├── db.py             # SQLite (SQLModel/SQLAlchemy)
    │   └── schemas.py        # Pydantic (contratos — ver Spec 03)
    └── obs/
        └── metrics.py        # contadores, logs estruturados
```

---

## 4. Configuração (`hub.yaml`)

```yaml
server:
  bind: "0.0.0.0:8088"
  device_tokens:            # tokens aceitos (1 por device)
    - "taskhog-01:${TASKHOG01_TOKEN}"

whisper:
  model: "large-v3"         # ou "medium" p/ velocidade
  device: "auto"            # cuda | cpu | auto
  compute_type: "auto"      # int8 | float16 | auto
  language: "pt"
  vad_filter: true          # corta silêncio

llm:
  provider: "cloud"         # cloud | ollama
  model: "<modelo>"
  endpoint: "${LLM_ENDPOINT}"
  api_key: "${LLM_API_KEY}"
  json_strict: true
  max_retries: 2
  confidence_threshold: 0.75

todoist:
  token: "${TODOIST_TOKEN}"
  base_url: "https://api.todoist.com/api/v1"   # unified API v1 (REST v2 desligada 2026-02-10)
  always_label: "taskhog"
  review_label: "revisar"
  inbox_fallback: true
  cache_refresh_min: 60
  due_lang: "pt"

storage:
  audio_dir: "/data/audio"
  retain_audio_days: 7      # 0 = apaga após sucesso
  db_path: "/data/taskhog.db"
```

Segredos sempre via variáveis de ambiente (`.env`), nunca commitados.

---

## 5. API — especificação

> Contratos completos (JSON Schema) no Spec 03. Aqui: semântica, status codes, idempotência.

### 5.1 `POST /v1/recordings`

- **Auth:** `Authorization: Bearer <device_token>`.
- **Body:** `multipart/form-data` → `audio` (arquivo .wav) + `metadata` (campo JSON).
- **Idempotência:** chave = `client_job_id` (do metadata). Se já existe um job com esse id desse device → retorna o job existente (não cria outro).

```text
202 Accepted
{ "recording_id": "rec_8f3a", "client_job_id": "20260617_153012_a1", "status": "queued" }

200 OK (idempotente, já existia)
{ "recording_id": "rec_8f3a", "client_job_id": "...", "status": "done", "duplicate": true }

401/403  token inválido
413       áudio acima do limite
422       metadata inválido
```

Comportamento: salva o WAV em `audio_dir`, cria linha em `jobs (status=queued)`, enfileira no worker, responde **202 imediatamente** (não bloqueia o device esperando transcrição).

### 5.2 `GET /v1/recordings/{recording_id}`

```text
200 OK
{
  "recording_id": "rec_8f3a",
  "status": "done",                 // queued|transcribing|structuring|creating|done|error
  "transcript": "criar apresentação para o banco do brasil para quinta prioridade alta",
  "tasks": [
    { "todoist_id": "7654321", "content": "Criar apresentação para o Banco do Brasil",
      "project": "Trabalho", "confidence": 0.93, "routed_to": "project", "due": "quinta-feira", "priority": 4 }
  ],
  "error": null
}
```

### 5.3 `GET /v1/status`

Resumo leve para a tela do device (consumido em cada sync):

```text
200 OK
{ "queue_pending": 0, "processing": 1, "processed_today": 12,
  "errors": 0, "last_task": { "content": "Criar apresentação BB", "project": "Trabalho", "routed_to": "project" },
  "server_time": "2026-06-17T15:33:02-03:00" }
```

### 5.4 `GET /v1/health`

```text
200 OK { "ok": true, "whisper": "ready", "todoist": "ok", "version": "1.0.0" }
```

Device usa para decidir se o Hub está alcançável **antes** de tentar upload.

### 5.5 `POST /v1/projects/refresh` (admin)

Força recarga do cache de projetos/labels do Todoist. Também roda periodicamente (`cache_refresh_min`).

---

## 6. Modelo de dados (SQLite)

```sql
CREATE TABLE jobs (
  recording_id   TEXT PRIMARY KEY,         -- gerado pelo Hub
  device_id      TEXT NOT NULL,
  client_job_id  TEXT NOT NULL,            -- idempotência
  wav_path       TEXT NOT NULL,
  rtc_timestamp  TEXT,                     -- do device
  rtc_valid      INTEGER DEFAULT 1,
  received_at    TEXT NOT NULL,            -- hora do Hub
  duration_s     REAL,
  status         TEXT NOT NULL,            -- queued|transcribing|structuring|creating|done|error
  transcript     TEXT,
  llm_json       TEXT,                     -- saída crua da LLM (auditoria)
  error          TEXT,
  attempts       INTEGER DEFAULT 0,
  created_tasks  TEXT,                     -- JSON array de tarefas criadas
  UNIQUE(device_id, client_job_id)         -- garante idempotência
);

CREATE TABLE todoist_cache (
  kind   TEXT NOT NULL,                    -- 'project' | 'label'
  ext_id TEXT NOT NULL,                    -- id no Todoist
  name   TEXT NOT NULL,
  PRIMARY KEY (kind, ext_id)
);

CREATE INDEX idx_jobs_status ON jobs(status);
CREATE INDEX idx_jobs_received ON jobs(received_at);
```

WAL ligado (`PRAGMA journal_mode=WAL`) para leitura/escrita concorrente entre API e worker.

---

## 7. Pipeline do worker

```text
job(queued)
  ├─ status=transcribing  → transcript = whisper(wav)
  ├─ status=structuring   → llm_json   = structure(transcript, now, projects, labels)
  ├─                        validar JSON contra schema (Spec 03); inválido → retry/erro
  ├─ status=creating      → p/ cada task: resolve projeto/labels → Todoist API
  │                         (regra de confiança: projeto vs Inbox+@revisar)
  └─ status=done          → grava created_tasks; aplica retenção de áudio
  (qualquer falha → status=error, error=<motivo>, attempts++)
```

- Worker `asyncio` consome `jobs WHERE status='queued' ORDER BY received_at`.
- Concorrência limitada (ex.: 1 job por vez se CPU-bound no Whisper; configurável).
- Cada transição persiste no DB (auditoria + recuperação se o Hub reiniciar no meio).

---

## 8. Transcrição (faster-whisper)

```python
from faster_whisper import WhisperModel

model = WhisperModel(cfg.whisper.model, device=cfg.whisper.device,
                     compute_type=cfg.whisper.compute_type)

def transcribe(wav_path: str) -> str:
    segments, info = model.transcribe(
        wav_path, language=cfg.whisper.language, vad_filter=cfg.whisper.vad_filter)
    return " ".join(s.text.strip() for s in segments).strip()
```

- `vad_filter=True` remove silêncio (capturas push-to-talk têm sobra no início/fim).
- `large-v3` = melhor qualidade PT-BR; `medium` = ~3–5× mais rápido em CPU. Escolher pelo hardware do homeserver.
- Se o device marcou `rtc_valid=false`, usar `received_at` do Hub como referência temporal para datas relativas ("amanhã") no passo de structuring.

---

## 9. LLM — interface plugável

### 9.1 Contrato

```python
from typing import Protocol
from app.models.schemas import StructuredResult

class LLMProvider(Protocol):
    def structure(self, transcript: str, *, now, projects: list[str],
                  labels: list[str]) -> StructuredResult: ...
```

### 9.2 Implementações

- **`cloud.py`** — endpoint OpenAI-compatible (cobre vários provedores e também Ollama no modo OpenAI). Usa "JSON mode"/`response_format` quando disponível para forçar JSON.
- **`ollama.py`** — chamada local; mesmo prompt; validação extra de JSON (modelos locais erram mais o formato).

### 9.3 Robustez de parsing

- Sempre validar a saída contra o **JSON Schema** (Spec 03) com Pydantic.
- Se inválido: 1 retry com instrução de correção ("responda SOMENTE JSON válido conforme o schema"). Persistir falha após `max_retries` → `status=error`.
- `json_strict`: rejeitar texto fora do objeto JSON (sem markdown/backticks).

> O **prompt completo** (system + user template + few-shots) está no Spec 03 §6. O Hub injeta a lista real de projetos/labels (do cache) a cada chamada — é isso que garante o roteamento correto.

---

## 10. Integração Todoist

> **⚠️ Atualização 2026:** a Todoist **REST API v2** (`/rest/v2`) foi desligada em **2026-02-10**. Tudo usa a **unified API v1** (`https://api.todoist.com/api/v1`). Os exemplos abaixo mantêm os mesmos campos de payload (a forma da criação de tarefa é compatível); só muda a base URL. Endpoints de listagem (projects/labels — M4) passam a devolver `{"results": [...], "next_cursor": ...}` (paginação por cursor).

### 10.1 Cache de projetos/labels

```python
# GET {base}/projects e {base}/labels → popular todoist_cache
# atualizar a cada cache_refresh_min e sob demanda (/v1/projects/refresh)
```

O cache vira a lista que o prompt da LLM recebe. **Sem cache atualizado, a categorização degrada.**

### 10.2 Criação de tarefa

```python
import httpx

def create_task(t: TaskSpec, project_id: str | None) -> dict:
    payload = {"content": t.content}
    if project_id:        payload["project_id"] = project_id      # senão → Inbox
    if t.due_string:      payload["due_string"] = t.due_string
                          payload["due_lang"] = cfg.todoist.due_lang  # "pt"
    if t.priority:        payload["priority"] = t.priority         # 1..4 (ver gotcha §10.4)
    payload["labels"] = list({*t.labels, cfg.todoist.always_label,  # sempre "taskhog"
                              *( [cfg.todoist.review_label] if t.needs_review else [] )})
    r = httpx.post(f"{base}/tasks", json=payload,
                   headers={"Authorization": f"Bearer {token}",
                            "X-Request-Id": t.idempotency_key})     # evita duplicata
    r.raise_for_status()
    task = r.json()
    for sub in t.subtasks:                                          # subtarefas = filhas
        httpx.post(f"{base}/tasks",
                   json={"content": sub, "project_id": task.get("project_id"),
                         "parent_id": task["id"]}, headers=...)
    return task
```

### 10.3 Resolução de projeto + regra de confiança

```text
match = resolve(project_suggestion, cache.projects)   # exato → fuzzy → None
if match and confidence >= threshold:
    project_id = match.id; routed_to = "project"
else:
    project_id = None  (Inbox); needs_review = True; routed_to = "inbox"
```

### 10.4 ⚠️ Gotcha de prioridade do Todoist

Na **REST API**, `priority` é invertido em relação à UI:

| API | UI (app) | Significado |
|---:|---|---|
| 4 | "Prioridade 1" (vermelho) | mais alta |
| 3 | "Prioridade 2" | alta |
| 2 | "Prioridade 3" | média |
| 1 | "Prioridade 4" (sem cor) | normal/default |

O contrato da LLM (Spec 03) usa o **valor da API (4=mais alta)**. "Prioridade alta" na fala → `4`.

### 10.5 Erros

- 5xx/timeout → retry com backoff; job permanece `creating`.
- 401/4xx permanente (token) → `status=error`; reflete em `/v1/status` (device mostra T10 "Erro Todoist").
- Sempre aplicar `idempotency_key` por tarefa para o reprocessamento não duplicar.

---

## 11. Modelo de confiança

`confidence.py`:

```text
score final = f(project_confidence da LLM, qualidade do match no cache)
  - match exato de nome de projeto  → mantém score da LLM
  - match fuzzy (similaridade)       → multiplica por fator (<1)
  - sem match                        → força Inbox

decisão:
  score >= threshold → projeto sugerido + label taskhog
  score <  threshold → Inbox + labels taskhog + revisar
```

`threshold` configurável (default 0.75). Ajustar observando a taxa "Inbox vs roteado" nas métricas.

---

## 12. Deploy

### 12.1 docker-compose

```yaml
services:
  taskhog-hub:
    build: .
    env_file: .env
    volumes:
      - ./hub.yaml:/app/hub.yaml:ro
      - taskhog-data:/data           # áudio + sqlite
    ports:
      - "8088:8088"
    # GPU opcional p/ Whisper:
    # deploy: { resources: { reservations: { devices: [{capabilities: [gpu]}] } } }
    restart: unless-stopped
volumes:
  taskhog-data:
```

### 12.2 Acesso remoto (Tailscale)

- Instalar Tailscale **no host** do homeserver; o Hub fica acessível pelo IP/MagicDNS do tailnet (ex.: `https://taskhog-hub.<tailnet>.ts.net`).
- O **device** entra na URL do Hub via tailnet. Como o ESP32 não roda cliente Tailscale nativo, opções (decidir no setup — PRD §14.1/§23):
  1. **Túnel/reverse proxy** com TLS + auth na borda (ex.: Cloudflare Tunnel, Caddy) apontando para o Hub.
  2. **VPN no roteador** da rede que o device usa.
  3. **Funnel/HTTPS endpoint** exposto de forma controlada.
- Em casa, o device acessa direto na LAN (sem sair pra internet).

---

## 13. Observabilidade

- **Logs estruturados por job:** `recording_id`, duração, tempo de transcrição, modelo, `routed_to`, `confidence`, tarefa criada.
- **Métricas:** jobs/dia, latência média (recebido→done), taxa de erro, **% roteado vs Inbox** (proxy de acurácia), tempo de Whisper.
- **Auditoria:** `llm_json` cru salvo no DB permite revisar decisões e refinar o prompt/threshold.

---

## 14. Segurança

- `device_token` por device (Bearer); rotacionável.
- Token do Todoist e chave da LLM **só** em env/secret, nunca no device nem no SD.
- TLS no transporte (tailnet ou proxy com TLS).
- Áudio nunca enviado a terceiros (Whisper local). Texto à LLM: pode ser 100% local com Ollama.
- Retenção de áudio configurável (`retain_audio_days`; 0 = apaga após sucesso).

---

## 15. Critérios de aceite (Hub)

- [ ] `POST /v1/recordings` aceita multipart, responde 202, é **idempotente** por `client_job_id`.
- [ ] Pipeline completo: transcrição PT-BR → JSON válido → tarefa no Todoist.
- [ ] Roteamento usa cache real de projetos; baixa confiança → Inbox + `@revisar`.
- [ ] Campos adaptativos corretos (prioridade respeita o gotcha da API).
- [ ] Subtarefas criadas como filhas.
- [ ] `/v1/status` reflete fila/processados/erros para a tela do device.
- [ ] Recupera estado após restart no meio de um job (sem duplicar tarefa).
- [ ] Provider de LLM trocável (cloud ↔ ollama) só por config.
