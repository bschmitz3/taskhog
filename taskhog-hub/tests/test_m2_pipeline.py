"""Teste M2 (whisper + Todoist mockados): upload → pipeline → done, idempotência.

Não baixa modelo Whisper nem chama a Todoist real. Valida o fluxo do Hub:
POST /v1/recordings → worker → GET /v1/recordings/{id} == done; reenvio do mesmo
client_job_id não duplica; /v1/status reflete a contagem.
"""

from __future__ import annotations

import os
import time
from pathlib import Path

import pytest
from fastapi.testclient import TestClient

TOKEN = "test-device-token"
DEVICE_ID = "taskhog-01"


@pytest.fixture()
def client(tmp_path: Path, monkeypatch: pytest.MonkeyPatch):
    config_path = tmp_path / "hub.yaml"
    config_path.write_text(
        f"""
server:
  bind: "0.0.0.0:8088"
  device_tokens:
    - "{TOKEN}"
whisper:
  model: "medium"
  device: "cpu"
  compute_type: "int8"
  language: "pt"
  vad_filter: true
llm:
  provider: "cloud"
  model: "x"
  endpoint: "x"
  api_key: "x"
  json_strict: true
  max_retries: 2
  confidence_threshold: 0.75
todoist:
  token: "tok"
  base_url: "https://api.todoist.com/api/v1"
  always_label: "taskhog"
  review_label: "revisar"
  inbox_fallback: true
  cache_refresh_min: 60
  due_lang: "pt"
storage:
  audio_dir: "{tmp_path / 'audio'}"
  retain_audio_days: 7
  db_path: "{tmp_path / 'taskhog.db'}"
""",
        encoding="utf-8",
    )
    monkeypatch.setenv("HUB_CONFIG", str(config_path))

    from app import main
    from app.models import db
    from app.models.schemas import StructuredResult, StructuredTaskItem
    from app.services import todoist, todoist_cache, transcribe
    from app.worker import pipeline as worker_pipeline

    created: list[dict] = []

    def fake_transcribe(wav_path, config):
        return "comprar pão e ligar pro contador"

    def fake_create_task(config, *, content, labels=None, **kwargs):
        created.append(
            {
                "content": content,
                "labels": labels,
                "project_id": kwargs.get("project_id"),
                "due_string": kwargs.get("due_string"),
                "priority": kwargs.get("priority"),
                "parent_id": kwargs.get("parent_id"),
            }
        )
        return {
            "id": f"tdid-{len(created)}",
            "content": content,
            "project_id": kwargs.get("project_id"),
        }

    class FakeLLM:
        def structure(self, transcript, *, now, projects, labels):
            return StructuredResult(
                language="pt",
                needs_review=False,
                tasks=[
                    StructuredTaskItem(
                        content="Comprar pão",
                        project_suggestion="Compras",
                        project_confidence=0.9,
                    ),
                    StructuredTaskItem(
                        content="Ligar para o contador",
                        project_suggestion="Personal",
                        project_confidence=0.8,
                        due_string="amanhã",
                    ),
                ],
            )

    monkeypatch.setattr(transcribe, "transcribe", fake_transcribe)
    monkeypatch.setattr(transcribe, "whisper_status", lambda: "ready")
    monkeypatch.setattr(todoist, "create_task", fake_create_task)
    monkeypatch.setattr(worker_pipeline, "get_llm_provider", lambda _cfg: FakeLLM())
    monkeypatch.setattr(
        todoist_cache,
        "refresh_project_label_cache",
        lambda engine, config: {"projects": 0, "labels": 0},
    )

    with TestClient(main.app) as c:
        db.replace_todoist_cache(
            c.app.state.engine,
            "project",
            [("pid-compras", "Compras"), ("pid-pessoal", "Personal")],
        )
        c.created_tasks = created  # type: ignore[attr-defined]
        yield c


def _post(client: TestClient, client_job_id: str):
    metadata = (
        '{"client_job_id": "%s", "device_id": "%s", '
        '"rtc_timestamp": "2026-06-22T15:30:12-03:00", "rtc_valid": true, '
        '"duration_s": 4.2, "battery_pct": 80, "fw_version": "1.0.0"}'
        % (client_job_id, DEVICE_ID)
    )
    return client.post(
        "/v1/recordings",
        headers={"Authorization": f"Bearer {TOKEN}"},
        files={"audio": ("rec.wav", b"RIFFfakewavdata", "audio/wav")},
        data={"metadata": metadata},
    )


def _wait_done(client: TestClient, recording_id: str, timeout: float = 10.0) -> dict:
    deadline = time.time() + timeout
    last = {}
    while time.time() < deadline:
        resp = client.get(f"/v1/recordings/{recording_id}")
        last = resp.json()
        if last["status"] in ("done", "error"):
            return last
        time.sleep(0.1)
    return last


def test_auth_required(client: TestClient):
    assert _post_no_auth(client).status_code == 401


def _post_no_auth(client: TestClient):
    return client.post(
        "/v1/recordings",
        files={"audio": ("rec.wav", b"x", "audio/wav")},
        data={"metadata": "{}"},
    )


def test_bad_token(client: TestClient):
    resp = client.post(
        "/v1/recordings",
        headers={"Authorization": "Bearer wrong"},
        files={"audio": ("rec.wav", b"x", "audio/wav")},
        data={"metadata": "{}"},
    )
    assert resp.status_code == 403


def test_pipeline_creates_task(client: TestClient):
    resp = _post(client, "20260622_153012_a1")
    assert resp.status_code == 202, resp.text
    body = resp.json()
    assert body["status"] == "queued"
    assert body["duplicate"] is False

    detail = _wait_done(client, body["recording_id"])
    assert detail["status"] == "done", detail
    assert detail["transcript"] == "comprar pão e ligar pro contador"
    assert len(detail["tasks"]) == 2
    assert detail["tasks"][0]["routed_to"] == "project"
    assert detail["tasks"][0]["project"] == "Compras"
    assert detail["tasks"][1]["routed_to"] == "project"
    assert detail["tasks"][1]["due"] == "amanhã"
    assert "taskhog" in detail["tasks"][0]["labels"]
    assert len(client.created_tasks) == 2  # type: ignore[attr-defined]


def test_idempotent_resend(client: TestClient):
    first = _post(client, "20260622_153012_b2")
    assert first.status_code == 202
    rid = first.json()["recording_id"]
    _wait_done(client, rid)

    second = _post(client, "20260622_153012_b2")
    assert second.status_code == 200
    assert second.json()["duplicate"] is True
    assert second.json()["recording_id"] == rid
    # Não criou tarefa nova no Todoist.
    assert len(client.created_tasks) == 2  # type: ignore[attr-defined]


def test_status_reflects_processed(client: TestClient):
    rid = _post(client, "20260622_153012_c3").json()["recording_id"]
    _wait_done(client, rid)

    status = client.get("/v1/status").json()
    assert status["processed_today"] >= 1
    assert status["last_task"]["routed_to"] == "project"
