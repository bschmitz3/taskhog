#!/usr/bin/env bash
# Teste E2E do M2: envia um WAV → acompanha o pipeline → valida idempotência.
#
# Uso:
#   TASKHOG_TOKEN=<token> ./scripts/test_e2e.sh [WAV] [HUB_URL]
#
# Padrões: WAV=teste.wav  HUB_URL=http://192.168.100.227:8088
# Remoto:  TASKHOG_TOKEN=... ./scripts/test_e2e.sh teste.wav https://hub.taskhog.win
set -euo pipefail

WAV="${1:-teste.wav}"
HUB="${2:-http://192.168.100.227:8088}"
TOKEN="${TASKHOG_TOKEN:-}"

if [[ -z "$TOKEN" ]]; then
  echo "ERRO: defina TASKHOG_TOKEN (ex.: TASKHOG_TOKEN=xxxx $0 ...)" >&2
  exit 1
fi
if [[ ! -f "$WAV" ]]; then
  echo "ERRO: WAV não encontrado: $WAV" >&2
  exit 1
fi

CJID="$(date +%Y%m%d_%H%M%S)_$(printf '%02x' $((RANDOM % 256)))"
META="{\"client_job_id\":\"$CJID\",\"device_id\":\"taskhog-01\",\"rtc_timestamp\":\"$(date +%Y-%m-%dT%H:%M:%S)-03:00\",\"rtc_valid\":true,\"duration_s\":1.1}"

echo "==> Hub:    $HUB"
echo "==> WAV:    $WAV"
echo "==> job id: $CJID"

jget() { python3 -c "import sys,json;print(json.load(sys.stdin).get('$1',''))"; }

echo
echo "1) POST /v1/recordings"
RESP="$(curl -s -X POST "$HUB/v1/recordings" \
  -H "Authorization: Bearer $TOKEN" \
  -F "audio=@$WAV;type=audio/wav" \
  -F "metadata=$META")"
echo "   $RESP"
RID="$(echo "$RESP" | jget recording_id)"
[[ -n "$RID" ]] || { echo "ERRO: sem recording_id (token? 401/403?)"; exit 1; }

echo
echo "2) polling GET /v1/recordings/$RID"
for i in $(seq 1 60); do
  DET="$(curl -s -H "Authorization: Bearer $TOKEN" "$HUB/v1/recordings/$RID")"
  ST="$(echo "$DET" | jget status)"
  printf "   [%02d] status=%s\n" "$i" "$ST"
  if [[ "$ST" == "done" || "$ST" == "error" ]]; then
    echo "   $DET"
    break
  fi
  sleep 1
done

echo
echo "3) idempotência — reenvio do mesmo client_job_id"
RESP2="$(curl -s -X POST "$HUB/v1/recordings" \
  -H "Authorization: Bearer $TOKEN" \
  -F "audio=@$WAV;type=audio/wav" \
  -F "metadata=$META")"
echo "   $RESP2"
DUP="$(echo "$RESP2" | jget duplicate)"
[[ "$DUP" == "True" ]] && echo "   OK: duplicate=true (não recriou)" || echo "   ATENÇÃO: esperava duplicate=true"

echo
echo "4) GET /v1/status"
curl -s "$HUB/v1/status"
echo
echo "==> Confira a tarefa no Inbox do Todoist (label taskhog)."
