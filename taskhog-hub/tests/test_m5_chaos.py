"""M5-T7: testes de caos automatizados no Hub (complementam checklist no device)."""

from __future__ import annotations

import time
from pathlib import Path

import pytest
from fastapi.testclient import TestClient
from sqlalchemy import text

from app.models import db
from app.models.schemas import StructuredResult, StructuredTaskItem
from app.services import todoist, todoist_cache, transcribe
from app.worker import pipeline as worker_pipeline

TOKEN = "chaos-token"
DEVICE_ID = "taskhog-chaos"


@pytest.fixture()
def chaos_client(tmp_path: Path, monkeypatch: pytest.MonkeyPatch):
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
  retain_audio_days: 0
  db_path: "{tmp_path / 'taskhog.db'}"
""",
        encoding="utf-8",
    )
    monkeypatch.setenv("HUB_CONFIG", str(config_path))

    from app import main

    api_calls: list[str] = []

    def fake_transcribe(wav_path, config):
        return "comprar leite"

    def fake_create(config, *, content, labels=None, **kwargs):
        key = kwargs.get("idempotency_key", "")
        api_calls.append(key)
        return {"id": f"td-{len(api_calls)}", "content": content, "project_id": None}

    class FakeLLM:
        def structure(self, transcript, *, now, projects, labels):
            return StructuredResult(
                language="pt",
                needs_review=False,
                tasks=[
                    StructuredTaskItem(
                        content="Comprar leite",
                        project_suggestion="Compras",
                        project_confidence=0.9,
                    ),
                    StructuredTaskItem(
                        content="Ligar pro contador",
                        project_suggestion="Personal",
                        project_confidence=0.85,
                    ),
                ],
            )

    monkeypatch.setattr(transcribe, "transcribe", fake_transcribe)
    monkeypatch.setattr(transcribe, "whisper_status", lambda: "ready")
    monkeypatch.setattr(todoist, "create_task", fake_create)
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
        c.api_calls = api_calls  # type: ignore[attr-defined]
        yield c


def _post(client: TestClient, job_id: str):
    meta = (
        '{"client_job_id": "%s", "device_id": "%s", '
        '"rtc_timestamp": "2026-06-28T12:00:00-03:00", "rtc_valid": true, '
        '"duration_s": 2.0}'
        % (job_id, DEVICE_ID)
    )
    return client.post(
        "/v1/recordings",
        headers={"Authorization": f"Bearer {TOKEN}"},
        files={"audio": ("rec.wav", b"RIFFchaoswav", "audio/wav")},
        data={"metadata": meta},
    )


def _wait_done(client: TestClient, rid: str) -> dict:
    deadline = time.time() + 15.0
    while time.time() < deadline:
        body = client.get(f"/v1/recordings/{rid}").json()
        if body["status"] in ("done", "error"):
            return body
        time.sleep(0.05)
    return body


def test_chaos_duplicate_upload_is_idempotent(chaos_client: TestClient):
    """Rede caiu após upload: reenvio do mesmo client_job_id não duplica."""
    first = _post(chaos_client, "chaos_upload_01")
    assert first.status_code == 202
    rid = first.json()["recording_id"]
    _wait_done(chaos_client, rid)

    second = _post(chaos_client, "chaos_upload_01")
    assert second.status_code == 200
    assert second.json()["duplicate"] is True
    assert len(chaos_client.api_calls) == 2  # type: ignore[attr-defined]


def test_chaos_hub_restart_mid_creating_no_extra_todoist(chaos_client: TestClient):
    """Hub reinicia no meio do creating: retoma sem chamar Todoist de novo."""
    engine = chaos_client.app.state.engine  # type: ignore[attr-defined]
    rid = db.new_recording_id()
    structured = StructuredResult(
        language="pt",
        needs_review=False,
        tasks=[
            StructuredTaskItem(
                content="Comprar leite",
                project_suggestion="Compras",
                project_confidence=0.9,
            ),
            StructuredTaskItem(
                content="Ligar pro contador",
                project_suggestion="Personal",
                project_confidence=0.85,
            ),
        ],
    )

    db.insert_job(
        engine,
        recording_id=rid,
        device_id=DEVICE_ID,
        client_job_id="chaos_restart_01",
        wav_path="/tmp/x.wav",
        rtc_timestamp="2026-06-28T12:00:00-03:00",
        rtc_valid=True,
        duration_s=1.0,
        received_at=db.now_iso(),
        status="creating",
    )
    db.set_transcript(engine, rid, "comprar leite")
    db.set_llm_json(engine, rid, structured.model_dump_json())
    db.put_todoist_task_key(
        engine,
        idempotency_key=f"{rid}:0",
        recording_id=rid,
        todoist_id="td-existing",
        content="Comprar leite",
    )
    db.append_created_task(
        engine,
        rid,
        {
            "todoist_id": "td-existing",
            "content": "Comprar leite",
            "project": "Compras",
            "confidence": 0.9,
            "routed_to": "project",
            "due": None,
            "priority": None,
            "labels": ["taskhog"],
        },
    )

    chaos_client.api_calls.clear()  # type: ignore[attr-defined]
    assert db.reset_stuck_jobs(engine) == 1
    chaos_client.app.state.worker.notify()  # type: ignore[attr-defined]

    detail = _wait_done(chaos_client, rid)
    assert detail["status"] == "done"
    assert len(chaos_client.api_calls) == 1  # type: ignore[attr-defined]
    assert chaos_client.api_calls[0] == f"{rid}:1"  # type: ignore[attr-defined]
    assert db.count_todoist_task_keys(engine, rid) == 2
