# Projetos Todoist — mapeamento LLM (M0.5)

> Gerado em 2026-06-20. Nomes **exatos** para o prompt de structuring (Spec 02 §10).  
> Atualizar com: `curl -s -H "Authorization: Bearer $TODOIST_TOKEN" https://api.todoist.com/api/v1/projects | jq '.results[] | {name, id}'`

| Nome (exact) | ID | Notas |
|---|---|---|
| Inbox | `6CrfJ82xjrqRPQ52` | Fallback quando confiança baixa |
| Compras | `6fmQVwv3P3V2m32g` | |
| Magie | `6gq7xH5JgFwPHCVv` | |
| Personal | `6gq7xHvV6F48XH8G` | |
| MagieLive | `6gv46H6wfP77q6Fv` | |

## Labels (M05-T4)

| Label | Uso |
|---|---|
| `taskhog` | Sempre em tarefas criadas pelo Hub |
| `revisar` | Baixa confiança / Inbox triagem |

Configurado em `hub.yaml` → `todoist.always_label` / `review_label`.
