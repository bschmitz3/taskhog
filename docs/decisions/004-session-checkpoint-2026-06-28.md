# Checkpoint de sessão — 2026-06-28

Documento de retomada ao voltar ao Taskhog após pausa.

## Status por milestone

| Milestone | Código | Deploy / device | Notas |
|-----------|--------|-----------------|-------|
| M0–M4 | ✅ | ✅ | E2E smoke OK |
| M5 | ✅ | Hub `3ed76b5` em prod | QA device **parcial** — ver checklist |
| M6 | **T1+T2** em branch local (não commitado até este doc) | Flash pendente pós-fix latch | T3–T7 abertos |

## Hub (produção)

- **LAN:** `http://192.168.100.227:8088/v1/health`
- **Remoto:** `https://hub.taskhog.win/v1/health`
- **Deploy:** `pct exec 101` → `/opt/taskhog-hub` → `docker compose build && up -d`
- **Parar/religar (no Proxmox):** `pct exec 101 -- bash -lc "cd /opt/taskhog-hub && docker compose stop|start"`
- **Testes M5 Hub:** `cd taskhog-hub && ./scripts/run_m5_chaos.sh` (11 passed)

### M5 Hub fechado em código

- M5-T3: `todoist_task_keys` + `create_task_idempotent()`
- M5-T5: `audio_retention.py`
- M5-T6: resume `creating`
- M5-T7: `tests/test_m5_*.py` + `docs/setup/M5-chaos-checklist.md`

## Firmware

- **USB Mac:** `/dev/cu.usbmodem13301`
- **Build (pós-M6):** ~`0x136270` bytes
- **Flash:** `cd taskhog-fw && idf.py -p /dev/cu.usbmodem13301 flash monitor`

### M6-T1 + T2 (implementado)

- `power_mgr`: wake cause, boot dispatch, `enter_deep_sleep`, idle timer
- Wake sources: REC ext0, RTC ext1 (GPIO5), timer interno, USB
- **Menuconfig** (`Taskhog`):
  - `Enable deep sleep` — default **off** (`sdkconfig.defaults`)
  - `Skip auto sleep while USB` — default **on**
  - Idle 15 s · timer 60 min (quando sleep ON)

### Fix crítico — latch GPIO17 (BAT_Control)

**Problema:** deep sleep desligava a placa inteira; REC não acordava (só PWR).

**Fix:** `board_power_sleep_latch()` — `gpio_hold_en(GPIO17)` + `gpio_deep_sleep_hold_en()` antes de dormir.

**Validação pendente:** reflash com fix → sleep ON → REC deve mostrar `wake: rec_ext0`.

### Dev USB (recomendado agora)

- Deep sleep **desligado** no `sdkconfig.defaults`
- Log esperado: `power_mgr: deep sleep OFF`
- **Não** usar `-DCONFIG_...` para menuconfig — editar `sdkconfig` ou `idf.py menuconfig` (sdkconfig é gitignored)

### Se a placa não responde após sleep

1. Pressionar **PWR** uma vez (liga; não segurar)
2. Ou BOOT+RST para download mode
3. Reflash

## QA device — pendente (rodada separada)

Checklist: `docs/setup/M5-chaos-checklist.md`

| Item | Status |
|------|--------|
| D2 rede no upload | ✅ (1 tarefa Todoist, sem duplicata) |
| D1, D3, D4, D5, D6 | ⏳ |
| M5-T1 journal recovery | ⏳ |
| M5-T2 backoff logs | ⏳ |
| M4 regressão roteamento | ⏳ |

### SD — fila com lixo

Logs: `list_pending: 0 ok (2 .job, 1 read_fail, 1 wrong_state)`.

Limpar manualmente `queue/` no SD (mover `.job`/`.wav` órfãos ou para `sent/` se já processados).

## Próximos passos sugeridos

1. **Commit + push** M6-T1/T2 + fix latch
2. **Flash** firmware com deep sleep OFF (uso diário USB)
3. **Rodada QA** M5 checklist no device
4. **M6-T3** caminho rápido REC→RECORDING ≤300 ms
5. Instalar bateria antes de M6-T4/T7

## Commits recentes (`main`)

| Commit | Conteúdo |
|--------|----------|
| `3ed76b5` | M5-T3 idempotência Hub + M5-T7 testes caos |
| `b83450a` | poll Hub `done` → `sent/` |
| `724246c` | M5-T4 multi-AP Wi-Fi |
