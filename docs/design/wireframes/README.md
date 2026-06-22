# Wireframes e-Paper — Taskhog

Wireframes monocromáticos **200×200 px** (1 bpp) para todas as telas T0–T11.

## Arquivos

| Arquivo | Tela |
|---|---|
| `t00_boot.png` | T0 — Boot / Splash |
| `t01_home.png` | T1 — Home / Idle |
| `t01_home_empty.png` | T1 — Home sem fila (mascote) |
| `t02_recording.png` | T2 — Recording (**fundo invertido**) |
| `t03_saved.png` | T3 — Gravado / Fila |
| `t04_syncing.png` | T4 — Sincronizando |
| `t05_result.png` | T5 — Última tarefa |
| `t06_queue.png` | T6 — Fila pendente |
| `t06_queue_empty.png` | T6 — Fila vazia (mascote) |
| `t07_setup.png` | T7 — Modo setup |
| `t08_low_battery.png` | T8 — Bateria crítica |
| `t09_charging.png` | T9 — Carregando |
| `t10_error.png` | T10 — Erro |
| `t11_info.png` | T11 — Informações |
| `overview.png` | Grade com as 12 telas (800×800 cada, 4×) |
| `native/*.png` | Versões nativas 200×200 (sem escala) |

## Regenerar

```bash
cd docs/design
python3 -m venv .venv   # primeira vez
.venv/bin/pip install Pillow
.venv/bin/python generate_wireframes.py
```

## Decisões aplicadas nestes wireframes

- **T2:** fundo preto invertido (estado de gravação inconfundível)
- **T9:** tela dedicada (não overlay na Home)
- **T0:** coelho pixel art (mascote) — ver [`mascot-spec.md`](../mascot-spec.md)
- **Textos:** ASCII sem acentos onde o espaço aperta (firmware pode usar PT-BR completo)

Spec completa: [`../epaper-ui-spec.md`](../epaper-ui-spec.md)
