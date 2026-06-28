# M5-T7 — Bateria de testes de caos

Checklist para fechar o critério de saída do **Milestone 5**. Testes Hub rodam com `taskhog-hub/scripts/run_m5_chaos.sh`; os do device são manuais.

## Hub (automatizado)

```bash
cd taskhog-hub
./scripts/run_m5_chaos.sh
```

Cobre:

- Reenvio do mesmo `client_job_id` (upload duplicado)
- Restart no meio do `creating` + idempotência por tarefa (M5-T3/T6)
- Retenção WAV `retain_audio_days=0` após `done` (M5-T5)

## Device (manual — QA)

| # | Cenário | Passos | Esperado |
|---|---------|--------|----------|
| D1 | Queda gravando | Segurar REC → falar → reset no meio da gravação | WAV incompleto sinalizado ou `.job` não criado; fila íntegra |
| D2 | Rede no upload | Gravar → parar Hub durante upload | Backoff + reenvio; **uma** tarefa no Todoist |
| D3 | Reset pós-upload | Gravar → reset antes de `poll done` | Boot recovery; re-poll; `sent/` sem duplicata |
| D4 | SD removido | Boot sem cartão | Log `SD não montado`; sem crash; recolocar SD → fila OK |
| D5 | Journal | Após gravar | `QUEUE.JNL` com `E/M/U/C` |
| D6 | Move sent | Após Hub `done` | `queue/` vazio; arquivos em `sent/` |

## Critérios de saída M5

- [ ] D1 — fila íntegra após queda na gravação
- [ ] D2 — reenvio sem duplicar (Todoist)
- [ ] D3 — recovery + `sent/` correto
- [ ] Hub: `pytest tests/test_m5_*.py` verde
- [ ] Nenhuma tarefa duplicada em cenário de restart (D2/D3 + testes Hub)
