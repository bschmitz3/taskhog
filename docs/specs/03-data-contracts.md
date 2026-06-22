# Taskhog — Spec 03: Contratos de Dados e Definições

> **Companion da PRD v1.0.** A "fonte da verdade" dos contratos entre device ↔ Hub ↔ Todoist: schemas JSON, o prompt completo da LLM, mapeamentos, e a taxonomia canônica de estados e erros.
> Qualquer divergência entre Spec 01/02 e este documento → **este documento prevalece** para formatos de dados.

## Status do contrato

| Campo | Valor |
|---|---|
| **Versão do contrato** | **1.0** |
| **Congelado em** | **2026-06-20** (M0.5) |
| **Alterações** | Exigem PR que atualize **este arquivo primeiro**, depois firmware + hub juntos |

Implementação scaffold alinhada: `taskhog-hub/app/models/schemas.py` (Pydantic) · gates M0 em `taskhog-fw/main/m0_*_gate.c`.

---

## 1. Convenções

- Timestamps: **ISO 8601 com timezone** (ex.: `2026-06-17T15:30:12-03:00`).
- IDs do device (`client_job_id`): `AAAAMMDD_HHMMSS_<rand2>` (ex.: `20260617_153012_a1`).
- IDs do Hub (`recording_id`): opaco, prefixo `rec_`.
- Áudio: WAV PCM 16 kHz / 16-bit / mono.
- Todos os textos em UTF-8; idioma primário PT-BR.

---

## 2. Contrato: metadata de upload (device → Hub)

Enviado no campo `metadata` do multipart de `POST /v1/recordings`.

```json
{
  "client_job_id": "20260617_153012_a1",
  "device_id": "taskhog-01",
  "rtc_timestamp": "2026-06-17T15:30:12-03:00",
  "rtc_valid": true,
  "duration_s": 7.4,
  "battery_pct": 78,
  "fw_version": "1.0.0"
}
```

| Campo | Tipo | Obrigatório | Notas |
|---|---|:---:|---|
| `client_job_id` | string | ✓ | chave de idempotência |
| `device_id` | string | ✓ | identifica o device |
| `rtc_timestamp` | string ISO | ✓ | hora local da captura |
| `rtc_valid` | bool | ✓ | se false, Hub usa `received_at` p/ datas relativas |
| `duration_s` | number | ✓ | duração do WAV |
| `battery_pct` | int | – | telemetria |
| `fw_version` | string | – | telemetria/OTA |

---

## 3. Contrato: arquivo `.job` (estado local no device)

```json
{
  "id": "20260617_153012_a1",
  "created_at_rtc": "2026-06-17T15:30:12-03:00",
  "rtc_valid": true,
  "duration_s": 7.4,
  "wav_path": "/sdcard/queue/20260617_153012_a1.wav",
  "device_id": "taskhog-01",
  "state": "queued",
  "attempts": 0,
  "last_error": null,
  "hub_recording_id": null
}
```

`state` ∈ `{queued, uploading, uploaded, processing, done, error}` (ver §8).

---

## 4. Contrato: respostas da API (Hub → device)

### 4.1 `POST /v1/recordings`
```json
{ "recording_id": "rec_8f3a", "client_job_id": "20260617_153012_a1", "status": "queued", "duplicate": false }
```

### 4.2 `GET /v1/recordings/{id}`
```json
{
  "recording_id": "rec_8f3a",
  "status": "done",
  "transcript": "criar apresentação para o banco do brasil para quinta prioridade alta",
  "tasks": [
    { "todoist_id": "7654321", "content": "Criar apresentação para o Banco do Brasil",
      "project": "Trabalho", "confidence": 0.93, "routed_to": "project",
      "due": "quinta-feira", "priority": 4, "labels": ["taskhog","apresentacao"] }
  ],
  "error": null
}
```
`status` ∈ `{queued, transcribing, structuring, creating, done, error}`. `routed_to` ∈ `{project, inbox}`.

### 4.3 `GET /v1/status`
```json
{ "queue_pending": 0, "processing": 1, "processed_today": 12, "errors": 0,
  "last_task": { "content": "Criar apresentação BB", "project": "Trabalho", "routed_to": "project" },
  "server_time": "2026-06-17T15:33:02-03:00" }
```

### 4.4 `GET /v1/health`
```json
{ "ok": true, "whisper": "ready", "todoist": "ok", "version": "0.1.0" }
```

`version` = versão semver do **Hub** (scaffold `0.1.0` em M0.5). `whisper`/`todoist` podem ser `"pending"` até M2 (sem token/modelo carregado).

---

## 5. Contrato: saída estruturada da LLM (o coração do produto)

### 5.1 JSON Schema (formal)

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "type": "object",
  "additionalProperties": false,
  "required": ["language", "tasks", "needs_review"],
  "properties": {
    "language": { "type": "string" },
    "needs_review": { "type": "boolean" },
    "tasks": {
      "type": "array", "minItems": 1,
      "items": {
        "type": "object",
        "additionalProperties": false,
        "required": ["content", "project_suggestion", "project_confidence"],
        "properties": {
          "content": { "type": "string", "minLength": 1 },
          "project_suggestion": { "type": ["string", "null"] },
          "project_confidence": { "type": "number", "minimum": 0, "maximum": 1 },
          "due_string": { "type": ["string", "null"] },
          "priority": { "type": ["integer", "null"], "enum": [1, 2, 3, 4, null] },
          "labels": { "type": "array", "items": { "type": "string" } },
          "subtasks": { "type": "array", "items": { "type": "string" } },
          "notes": { "type": ["string", "null"] }
        }
      }
    }
  }
}
```

### 5.2 Regras semânticas

- **Adaptativo:** preencher `due_string`/`priority`/`labels`/`subtasks` **apenas** se a fala implicar; caso contrário `null`/`[]`.
- `project_suggestion` deve casar com a **lista fornecida** de projetos; se nada casar bem, usar `null` e `project_confidence` baixo (não inventar projeto).
- `priority` segue o valor da **API Todoist** (4 = mais alta; ver §7.3).
- `due_string` em **PT** (linguagem natural Todoist): "amanhã", "quinta-feira", "amanhã 9h", "toda segunda".
- Falas com múltiplas ações → **múltiplas tasks**.
- `labels` aqui **não** inclui `taskhog`/`revisar` (esses o Hub adiciona); inclui só labels temáticas que a fala sugira.

---

## 6. Prompt completo da LLM (structuring)

> Injetado pelo Hub a cada job. `{...}` são placeholders preenchidos em runtime. A lista de projetos vem do cache do Todoist (Spec 02 §10.1).

### 6.1 System prompt

```text
Você é o motor de estruturação do Taskhog. Recebe a transcrição de uma nota de voz
em português e a converte em uma ou mais tarefas para o Todoist.

REGRAS:
1. Responda SOMENTE com um objeto JSON válido, sem markdown, sem comentários, sem texto extra.
2. O JSON deve seguir exatamente o schema fornecido.
3. Extraia apenas os campos que a fala implica. Não invente prazo, prioridade, label
   ou subtarefa que não estejam sugeridos na fala. Campos ausentes = null ou lista vazia.
4. project_suggestion DEVE ser exatamente um dos nomes da lista PROJETOS fornecida,
   ou null se nenhum encaixar bem. Nunca crie um nome de projeto novo.
5. project_confidence (0..1) reflete o quão certo você está do roteamento. Se a fala não
   indica claramente o projeto, use confiança baixa (ex.: 0.3) e/ou null.
6. priority usa a escala da API do Todoist: 4 = mais alta, 3 = alta, 2 = média,
   1 = normal/sem prioridade. "prioridade alta/urgente" -> 4. Sem menção -> null.
7. due_string em português natural ("amanhã", "quinta-feira", "amanhã 9h"). Sem menção -> null.
8. Se a fala contém várias ações independentes, gere várias tasks.
9. labels: apenas etiquetas temáticas sugeridas pela fala (sem "taskhog"/"revisar").
10. needs_review = true se a intenção for ambígua ou a transcrição parecer incompleta.

DATA/HORA ATUAL: {now_iso} (use para resolver datas relativas).
PROJETOS DISPONÍVEIS: {projects_list}
LABELS DISPONÍVEIS: {labels_list}

SCHEMA:
{json_schema}
```

### 6.2 User prompt

```text
Transcrição: "{transcript}"
```

### 6.3 Few-shots (incluir no prompt para calibrar)

```text
# Exemplo 1 — tarefa simples, sem campos extras
Transcrição: "comprar pão"
{
  "language": "pt", "needs_review": false,
  "tasks": [{ "content": "Comprar pão", "project_suggestion": "Compras",
    "project_confidence": 0.7, "due_string": null, "priority": null,
    "labels": [], "subtasks": [], "notes": null }]
}

# Exemplo 2 — tarefa rica
Transcrição: "criar apresentação para o banco do brasil para quinta-feira, prioridade alta"
{
  "language": "pt", "needs_review": false,
  "tasks": [{ "content": "Criar apresentação para o Banco do Brasil",
    "project_suggestion": "Trabalho", "project_confidence": 0.92,
    "due_string": "quinta-feira", "priority": 4,
    "labels": [], "subtasks": [], "notes": null }]
}

# Exemplo 3 — múltiplas ações
Transcrição: "comprar pão e ligar pro contador amanhã"
{
  "language": "pt", "needs_review": false,
  "tasks": [
    { "content": "Comprar pão", "project_suggestion": "Compras", "project_confidence": 0.7,
      "due_string": null, "priority": null, "labels": [], "subtasks": [], "notes": null },
    { "content": "Ligar para o contador", "project_suggestion": "Pessoal", "project_confidence": 0.55,
      "due_string": "amanhã", "priority": null, "labels": [], "subtasks": [], "notes": null }
  ]
}

# Exemplo 4 — ambíguo
Transcrição: "aquela coisa de antes"
{ "language": "pt", "needs_review": true,
  "tasks": [{ "content": "aquela coisa de antes", "project_suggestion": null,
    "project_confidence": 0.1, "due_string": null, "priority": null,
    "labels": [], "subtasks": [], "notes": "transcrição ambígua" }] }
```

---

## 7. Mapeamento Todoist (definitivo)

### 7.1 Campos

| Saída LLM | Campo Todoist (REST v2) | Regra |
|---|---|---|
| `content` | `content` | título |
| `project_suggestion` (resolvido) | `project_id` | match no cache; sem match/baixa conf → omitir (Inbox) |
| `due_string` | `due_string` + `due_lang="pt"` | só se != null |
| `priority` | `priority` | só se != null (escala API) |
| `labels` + `taskhog` (+`revisar`) | `labels` | `taskhog` sempre; `revisar` se Inbox/needs_review |
| `subtasks[]` | tarefas-filhas (`parent_id`) | uma por item |

### 7.2 Decisão de roteamento

```text
resolve(project_suggestion) → match?
  sim e score≥threshold  → project_id = match.id ; routed_to=project ; labels:[taskhog,...]
  não / score<threshold  → project_id ausente (Inbox) ; routed_to=inbox ; labels:[taskhog,revisar]
```

### 7.3 Escala de prioridade (gotcha)

| LLM/API | UI Todoist | Fala típica |
|---:|---|---|
| 4 | Prioridade 1 (vermelho) | "urgente", "prioridade alta", "importantíssimo" |
| 3 | Prioridade 2 | "prioridade média-alta" |
| 2 | Prioridade 3 | "quando der", baixa |
| 1 | Prioridade 4 (default) | sem menção → preferir `null` |

---

## 8. Definições de estado (canônicas)

### 8.1 Estado global do device (`taskhog_state_t`)

| Estado | Descrição |
|---|---|
| `BOOT` | inicialização pós-wake/reset |
| `IDLE` | ocioso, tela Home; default antes de dormir |
| `RECORDING` | gravando (REC pressionado) |
| `FINALIZING` | fechando WAV, criando .job |
| `CONFIRM` | tela "Gravado ✓" |
| `SYNC` | drenando fila para o Hub |
| `PROVISION` | portal cativo ativo |
| `OTA` | aplicando atualização |
| `SAFE_OFF` | desligamento seguro por bateria crítica |
| `DEEP_SLEEP` | consumo mínimo (não é "estado de app", é o repouso) |

### 8.2 Estado do `.job` no device

`queued → uploading → uploaded → processing → done` (caminho feliz); qualquer ponto → `error` (com `attempts`/`last_error`).

### 8.3 Estado do job no Hub

`queued → transcribing → structuring → creating → done`; falha → `error`.

### 8.4 Correspondência device ↔ Hub

| Device | Hub |
|---|---|
| `uploaded` | `queued`/`transcribing` |
| `processing` | `structuring`/`creating` |
| `done` | `done` |
| `error` | `error` |

O device sincroniza o estado consultando `/v1/recordings/{id}` ou `/v1/status`.

---

## 9. Taxonomia de erros (unificada)

### 9.1 Device

| Código | Camada | Recuperável | Ação |
|---|---|:---:|---|
| `E_SD_MOUNT` | storage | parcial | bloqueia captura; tela erro; reformat FAT32 |
| `E_WAV_INCOMPLETE` | audio | não | marca job error, retém WAV |
| `E_AUDIO_INIT` | audio | não | tela erro; validar povoamento (PRD §5.2) |
| `E_HUB_UNREACHABLE` | net | sim | mantém fila; T10 "tudo salvo" |
| `E_HUB_AUTH` | net | manual | T10 "token inválido"; reprovisionar |
| `E_UPLOAD_RETRY` | net | sim | backoff; permanece na fila |
| `E_BATT_CRITICAL` | power | — | SAFE_OFF |

### 9.2 Hub

| Código | Etapa | Recuperável | Ação |
|---|---|:---:|---|
| `H_TRANSCRIBE_FAIL` | whisper | sim | retry; persistente → error |
| `H_LLM_INVALID_JSON` | structuring | sim | 1 retry c/ correção; senão error |
| `H_LLM_UNAVAILABLE` | structuring | sim | retry/backoff |
| `H_TODOIST_5XX` | creating | sim | retry/backoff |
| `H_TODOIST_AUTH` | creating | manual | error; reflete em /v1/status |
| `H_PROJECT_UNRESOLVED` | resolve | n/a | não é erro: cai em Inbox+revisar |

### 9.3 Princípio

**Erro nunca apaga dado.** Áudio/job só são removidos após `done` confirmado. Erros são sinalizados ao usuário (tela do device + `/v1/status`) com a mensagem tranquilizadora de que nada foi perdido.

---

## 10. Idempotência (regra única)

- Device gera `client_job_id` por captura.
- `POST /v1/recordings` é idempotente por `(device_id, client_job_id)` — reenvio retorna o job existente.
- Cada **tarefa** criada no Todoist usa uma `idempotency_key` derivada de `recording_id` + índice da task, para que reprocessamento (ex.: Hub reiniciou no meio do `creating`) não duplique.

---

## 11. Glossário estendido

| Termo | Definição |
|---|---|
| Captura | Um evento de gravação push-to-talk (1 WAV + 1 .job) |
| Job | Uma captura em processamento (estado no device e no Hub) |
| Structuring | Passo da LLM: transcrição → JSON de tarefas |
| Routed_to | Destino da tarefa: `project` (roteada) ou `inbox` (triagem) |
| Confidence | Certeza do roteamento (0..1); decide projeto vs Inbox |
| Idempotency key | Chave que evita criar a mesma tarefa duas vezes |
| rtc_valid | Se o relógio do device é confiável (bit OS do PCF85063) |
| Cache do Todoist | Lista local de projetos/labels reais, injetada no prompt |
| Threshold | Limiar de confiança (default 0.75) |

---

*Estes três specs (01 firmware, 02 hub, 03 contratos) + a PRD formam o conjunto canônico do Taskhog. Mudanças de contrato devem ser refletidas primeiro aqui (Spec 03).*
