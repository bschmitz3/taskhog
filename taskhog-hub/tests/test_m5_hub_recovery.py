"""M5-T6: Hub reinicia no meio de `creating` — resume sem duplicar tarefas."""

from __future__ import annotations

import time
from collections.abc import Iterator
from pathlib import Path

import pytest
from fastapi.testclient import TestClient

from app.models import db
from app.models.schemas import StructuredResult, StructuredTaskItem
from app.services import todoist, todoist_cache, transcribe
from app.worker import pipeline as worker_pipeline

TOKEN = "test-device-token"
DEVICE_ID = "taskhog-01"


def _write_config(tmp_path: Path) -> Path:
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
    return config_path


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
                ),
            ],
        )


@pytest.fixture()
def client(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> Iterator[TestClient]:
    monkeypatch.setenv("HUB_CONFIG", str(_write_config(tmp_path)))

    from app import main

    created: list[dict] = []
    idem_keys: list[str | None] = []

    def fake_transcribe(wav_path, config):
        return "comprar pão e ligar pro contador"

    def fake_create_task(config, *, content, labels=None, **kwargs):
        key = kwargs.get("idempotency_key")
        idem_keys.append(key)
        created.append(
            {
                "content": content,
                "idempotency_key": key,
                "labels": labels,
            }
        )
        return {
            "id": f"tdid-{len(created)}",
            "content": content,
            "project_id": kwargs.get("project_id"),
        }

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
        c.idem_keys = idem_keys  # type: ignore[attr-defined]
        yield c


def _post(client: TestClient, client_job_id: str):
    metadata = (
        '{"client_job_id": "%s", "device_id": "%s", '
        '"rtc_timestamp": "2026-06-22T15:30:12-03:00", "rtc_valid": true, '
        '"duration_s": 4.2}'
        % (client_job_id, DEVICE_ID)
    )
    return client.post(
        "/v1/recordings",
        headers={"Authorization": f"Bearer {TOKEN}"},
        files={"audio": ("rec.wav", b"RIFFfakewavdata", "audio/wav")},
        data={"metadata": metadata},
    )


def _wait_done(client: TestClient, recording_id: str, timeout: float = 15.0) -> dict:
    deadline = time.time() + timeout
    last = {}
    while time.time() < deadline:
        resp = client.get(f"/v1/recordings/{recording_id}")
        last = resp.json()
        if last["status"] in ("done", "error"):
            return last
        time.sleep(0.05)
    return last


def test_resume_creating_after_interrupt(
    client: TestClient, monkeypatch: pytest.MonkeyPatch
):
    """Crash após 1ª tarefa → requeue → resume só cria a 2ª (M5-T6)."""
    crash_once = {"pending": True}
    idem_keys: list[str | None] = client.idem_keys  # type: ignore[attr-defined]
    created: list[dict] = client.created_tasks  # type: ignore[attr-defined]

    def flaky_create(config, *, content, labels=None, **kwargs):
        key = kwargs.get("idempotency_key")
        idem_keys.append(key)
        if key and key.endswith(":1") and crash_once["pending"]:
            crash_once["pending"] = False
            raise RuntimeError("simulated hub crash mid-creating")
        created.append(
            {"content": content, "idempotency_key": key, "labels": labels}
        )
        return {
            "id": f"tdid-{len(created)}",
            "content": content,
            "project_id": kwargs.get("project_id"),
        }

    monkeypatch.setattr(todoist, "create_task", flaky_create)
    idem_keys.clear()
    created.clear()

    resp = _post(client, "20260628_120000_a1")
    assert resp.status_code == 202
    rid = resp.json()["recording_id"]

    detail = _wait_done(client, rid)
    assert detail["status"] == "done", detail
    assert len(detail["tasks"]) == 2

    assert idem_keys == [f"{rid}:0", f"{rid}:1", f"{rid}:1"]
    assert len(created) == 2
    assert [t["content"] for t in created] == [
        "Comprar pão",
        "Ligar para o contador",
    ]


def test_reset_stuck_creating_resumes_without_duplicate(client: TestClient):
    """Simula restart: job em creating com 1 tarefa persistida → worker retoma."""
    engine = client.app.state.engine  # type: ignore[attr-defined]
    idem_keys: list[str | None] = client.idem_keys  # type: ignore[attr-defined]
    created: list[dict] = client.created_tasks  # type: ignore[attr-defined]
    idem_keys.clear()
    created.clear()

    rid = db.new_recording_id()
    structured = StructuredResult(
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
            ),
        ],
    )

    db.insert_job(
        engine,
        recording_id=rid,
        device_id=DEVICE_ID,
        client_job_id="20260628_120001_b2",
        wav_path="/tmp/fake.wav",
        rtc_timestamp="2026-06-22T15:30:12-03:00",
        rtc_valid=True,
        duration_s=1.0,
        received_at=db.now_iso(),
        status="creating",
    )
    db.set_transcript(engine, rid, "comprar pão e ligar pro contador")
    db.set_llm_json(engine, rid, structured.model_dump_json())
    db.append_created_task(
        engine,
        rid,
        {
            "todoist_id": "tdid-existing",
            "content": "Comprar pão",
            "project": "Compras",
            "confidence": 0.9,
            "routed_to": "project",
            "due": None,
            "priority": None,
            "labels": ["taskhog"],
        },
    )

    assert db.reset_stuck_jobs(engine) == 1

    job = db.get_job(engine, rid)
    assert job is not None
    assert job["status"] == "queued"
    assert len(db.load_created_tasks(job)) == 1

    client.app.state.worker.notify()  # type: ignore[attr-defined]

    detail = _wait_done(client, rid)
    assert detail["status"] == "done", detail
    assert len(detail["tasks"]) == 2
    assert idem_keys == [f"{rid}:1"]
    assert len(created) == 1
    assert created[0]["content"] == "Ligar para o contador"
