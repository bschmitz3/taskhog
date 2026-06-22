# Cloudflare Tunnel — Hub Taskhog (M05-T5 / M7)

> Decisão: ADR 001 §4 — expor `https://hub.taskhog.win` → Hub `:8088` no LXC `192.168.100.227`.

**Status:** ✅ ativo (2026-06-20) — `curl https://hub.taskhog.win/v1/health` → `{"ok":true,"whisper":"ready","todoist":"ok","version":"0.1.0"}`

| Item | Valor |
|---|---|
| Domínio | `taskhog.win` |
| Hostname Hub | `hub.taskhog.win` |
| Tunnel name | `taskhog` |
| Tunnel ID | `93648b38-c30c-46e4-ac34-339a5beb38f8` |
| Config | `/etc/cloudflared/config.yml` |
| Credenciais | `/root/.cloudflared/93648b38-c30c-46e4-ac34-339a5beb38f8.json` |

## Pré-requisitos

- Conta Cloudflare (domínio adicionado ao DNS) ✅
- `cloudflared` instalado no LXC ✅ (2026.6.1)
- Hub respondendo na LAN: `http://192.168.100.227:8088/v1/health` ✅

## Passo 1 — Login Cloudflare (interativo, uma vez)

No LXC:

```bash
ssh root@192.168.100.226
pct enter 101
cloudflared tunnel login
```

Abra a URL no browser, autorize o domínio. Credenciais ficam em `~/.cloudflared/cert.pem`.

## Passo 2 — Criar tunnel

Substitua `taskhog` pelo nome desejado:

```bash
cloudflared tunnel create taskhog
cloudflared tunnel list
```

Anote o **Tunnel ID** (UUID).

## Passo 3 — Configurar rota DNS

Substitua o hostname se necessário:

```bash
cloudflared tunnel route dns taskhog hub.taskhog.win
```

## Passo 4 — Arquivo de config

Crie o diretório e o arquivo (como root no LXC):

```bash
mkdir -p /etc/cloudflared
nano /etc/cloudflared/config.yml
```

Conteúdo (substitua `<TUNNEL_ID>` pelo UUID de `cloudflared tunnel list`):

```yaml
tunnel: 93648b38-c30c-46e4-ac34-339a5beb38f8
credentials-file: /root/.cloudflared/93648b38-c30c-46e4-ac34-339a5beb38f8.json

ingress:
  - hostname: hub.taskhog.win
    service: http://127.0.0.1:8088
  - service: http_status:404
```

## Passo 5 — Serviço systemd

```bash
cloudflared service install
systemctl enable --now cloudflared
systemctl status cloudflared
```

## Passo 6 — Validar de fora da LAN

Do Mac (4G ou rede externa, **não** Wi‑Fi de casa):

```bash
curl -s https://hub.taskhog.win/v1/health | jq
```

Esperado:

```json
{
  "ok": true,
  "whisper": "ready",
  "todoist": "ok",
  "version": "0.1.0"
}
```

## Passo 7 — Registrar URL no projeto

1. Anotar URL final em `docs/decisions/003-m05-closeout.md` (seção M05-T5)
2. Usar a mesma URL no provisionamento Wi‑Fi do ESP32 (M7)

## Troubleshooting

| Sintoma | Ação |
|---|---|
| `Error writing /etc/cloudflared/config.yml: No such file or directory` | `mkdir -p /etc/cloudflared` antes de salvar |
| 502 Bad Gateway | Hub down? `docker compose ps` em `/opt/taskhog-hub` |
| Tunnel não conecta | `journalctl -u cloudflared -f` |
| DNS não resolve | Aguardar propagação; conferir CNAME no painel Cloudflare |

## LAN vs remoto

| Contexto | URL Hub |
|---|---|
| Em casa (Wi‑Fi) | `http://192.168.100.227:8088` |
| Fora de casa | `https://hub.taskhog.win` |

O ESP32 pode guardar a URL HTTPS no `device.json` após provisionamento.
