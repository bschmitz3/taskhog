# M0.5 — Checklist Todoist (M05-T4)

Execute **antes do Milestone 2** (Hub criando tarefas). Não bloqueia M1 (captura offline).

## 1. Token API

1. Todoist → **Settings** → **Integrations** → **Developer** → copiar API token
2. No LXC Hub (`/opt/taskhog-hub/.env`) ou `.env` local:

```bash
TODOIST_TOKEN=xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
```

3. Reiniciar Hub: `docker compose up -d --build`

4. Confirmar health:

```bash
curl -s http://192.168.100.227:8088/v1/health | jq
# esperado: "todoist": "ok"
```

## 2. Labels obrigatórias

| Label | Uso |
|---|---|
| `taskhog` | Toda tarefa criada pelo Hub (`hub.yaml` → `always_label`) |
| `revisar` | Baixa confiança / Inbox (`hub.yaml` → `review_label`) |

Criar no app Todoist ou via API (**v1** — `rest/v2` está deprecado):

```bash
curl -s -X POST -H "Authorization: Bearer $TODOIST_TOKEN" -H "Content-Type: application/json" -d '{"name":"taskhog"}' https://api.todoist.com/api/v1/labels

curl -s -X POST -H "Authorization: Bearer $TODOIST_TOKEN" -H "Content-Type: application/json" -d '{"name":"revisar"}' https://api.todoist.com/api/v1/labels
```

Listar labels:

```bash
curl -s -H "Authorization: Bearer $TODOIST_TOKEN" https://api.todoist.com/api/v1/labels | jq '.results[] | .name'
```

## 3. Projetos para roteamento LLM

Listar projetos e anotar nomes **exatos** (case-sensitive no prompt):

```bash
curl -s -H "Authorization: Bearer $TODOIST_TOKEN" https://api.todoist.com/api/v1/projects | jq '.results[] | {name, id}'
```

Exemplos típicos: `Inbox`, `Trabalho`, `Pessoal` — conferir com Spec 02 §10 e sua conta real.

## 4. Marcar concluído

Health completo na LAN (2026-06-20):

```json
{"ok": true, "whisper": "ready", "todoist": "ok", "version": "0.1.0"}
```

Projetos anotados em `docs/setup/todoist-projects.md`.
