# Taskhog — Documentação

Índice da documentação do projeto. Use este arquivo para navegar.

## Estrutura

```text
docs/
├── README.md                 ← você está aqui
├── roadmap.md                # Milestones M0–M8, critérios de saída
├── prd/
│   └── PRD_v1.0.md
├── specs/
│   ├── 01-device-firmware.md
│   ├── 02-hub-backend.md
│   └── 03-data-contracts.md  # ★ fonte de verdade dos contratos
├── design/
│   ├── epaper-ui-spec.md       # ★ UI e-Paper — telas, layout, assets
│   ├── mascot-spec.md          # Coelho pixel art — variantes por status
│   └── wireframes/             # PNGs T0–T11 (+ estados vazios)
├── hardware/
│   ├── waveshare-esp32-s3-epaper-1.54-v2.md
│   └── HARDWARE_NOTES.md       # bring-up real (pinos, e-Paper, bateria)
└── decisions/
    ├── 001-initial-decisions.md
    ├── 002-homeserver-specs.md
    ├── 003-m05-closeout.md
    └── 004-epaper-ui-rebuild.md  # reconstrução UI + orientação e-Paper
```

## Ordem de leitura recomendada

1. `prd/PRD_v1.0.md` — quando disponível (visão e requisitos)
2. `specs/03-data-contracts.md` — contratos entre device ↔ Hub ↔ Todoist
3. `specs/01-device-firmware.md` + `specs/02-hub-backend.md` — implementação
4. `roadmap.md` — sequência de trabalho no Cursor
5. `hardware/...` — bring-up e GPIOs (M0)
6. `design/epaper-ui-spec.md` — interface e-Paper (telas T0–T11, assets, prompts para IA)

## Fluxo de mudança de contrato

```text
Spec 03 → Spec 01/02 (se necessário) → código (fw + hub juntos)
```

## Artefatos gerados durante o desenvolvimento

| Arquivo | Quando | Onde |
|---|---|---|
| `HARDWARE_NOTES.md` | Após M0-T9 | `docs/hardware/` |
| `device.json` schema | PRD §17.1 | `docs/specs/` ou PRD |
