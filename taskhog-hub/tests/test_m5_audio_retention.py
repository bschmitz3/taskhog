"""M5-T5: retenção e limpeza de WAV após job `done`."""

from __future__ import annotations

import time
from datetime import datetime, timedelta
from pathlib import Path

import pytest
from fastapi.testclient import TestClient
from sqlalchemy import text

from app.models import db
from app.models.schemas import StructuredResult, StructuredTaskItem
from app.services import audio_retention, todoist, todoist_cache, transcribe
from app.worker import pipeline as worker_pipeline

TOKEN = "test-device-token"
DEVICE_ID = "taskhog-01"


def _hub_config(tmp_path: Path, retain_days: int) -> Path:
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
  retain_audio_days: {retain_days}
  db_path: "{tmp_path / 'taskhog.db'}"
""",
        encoding="utf-8",
    )
    return config_path


@pytest.fixture()
def client_retain_zero(tmp_path: Path, monkeypatch: pytest.MonkeyPatch):
    monkeypatch.setenv("HUB_CONFIG", str(_hub_config(tmp_path, 0)))
    yield from _make_client(monkeypatch)


@pytest.fixture()
def client_retain_week(tmp_path: Path, monkeypatch: pytest.MonkeyPatch):
    monkeypatch.setenv("HUB_CONFIG", str(_hub_config(tmp_path, 7)))
    yield from _make_client(monkeypatch)


def _make_client(monkeypatch: pytest.MonkeyPatch):
    from app import main

    def fake_transcribe(wav_path, config):
        return "comprar pão"

    def fake_create_task(config, *, content, labels=None, **kwargs):
        return {"id": "tdid-1", "content": content, "project_id": None}

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
                    )
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
            [("pid-compras", "Compras")],
        )
        yield c


def _post(client: TestClient, client_job_id: str):
    metadata = (
        '{"client_job_id": "%s", "device_id": "%s", '
        '"rtc_timestamp": "2026-06-22T15:30:12-03:00", "rtc_valid": true, '
        '"duration_s": 1.0}'
        % (client_job_id, DEVICE_ID)
    )
    return client.post(
        "/v1/recordings",
        headers={"Authorization": f"Bearer {TOKEN}"},
        files={"audio": ("rec.wav", b"RIFFfakewavdata", "audio/wav")},
        data={"metadata": metadata},
    )


def _wait_done(client: TestClient, recording_id: str) -> dict:
    deadline = time.time() + 10.0
    while time.time() < deadline:
        detail = client.get(f"/v1/recordings/{recording_id}").json()
        if detail["status"] in ("done", "error"):
            return detail
        time.sleep(0.05)
    return detail


def test_delete_wav_immediately_when_retain_zero(client_retain_zero: TestClient):
    resp = _post(client_retain_zero, "20260628_130000_a1")
    rid = resp.json()["recording_id"]
    wav = Path(client_retain_zero.app.state.config.audio_dir) / f"{rid}.wav"  # type: ignore[attr-defined]

    assert _wait_done(client_retain_zero, rid)["status"] == "done"
    assert not wav.is_file()

    job = db.get_job(client_retain_zero.app.state.engine, rid)  # type: ignore[attr-defined]
    assert job is not None
    assert job["wav_path"] == ""


def test_keep_wav_when_retain_positive(client_retain_week: TestClient):
    resp = _post(client_retain_week, "20260628_130001_b1")
    rid = resp.json()["recording_id"]
    wav = Path(client_retain_week.app.state.config.audio_dir) / f"{rid}.wav"  # type: ignore[attr-defined]

    assert _wait_done(client_retain_week, rid)["status"] == "done"
    assert wav.is_file()


def test_purge_expired_done_jobs(client_retain_week: TestClient):
    engine = client_retain_week.app.state.engine  # type: ignore[attr-defined]
    config = client_retain_week.app.state.config  # type: ignore[attr-defined]
    audio_dir = Path(config.audio_dir)
    audio_dir.mkdir(parents=True, exist_ok=True)

    rid = db.new_recording_id()
    wav = audio_dir / f"{rid}.wav"
    wav.write_bytes(b"RIFFfake")

    old = (datetime.now().astimezone() - timedelta(days=10)).isoformat(timespec="seconds")
    db.insert_job(
        engine,
        recording_id=rid,
        device_id=DEVICE_ID,
        client_job_id="20260628_130002_c2",
        wav_path=str(wav),
        rtc_timestamp=None,
        rtc_valid=True,
        duration_s=1.0,
        received_at=old,
        status="done",
    )
    db.set_done(engine, rid, [{"todoist_id": "x", "content": "x"}])

    with engine.begin() as conn:
        conn.execute(
            text("UPDATE jobs SET received_at = :old WHERE recording_id = :rid"),
            {"old": old, "rid": rid},
        )

    removed = audio_retention.purge_expired(engine, config)
    assert removed == 1
    assert not wav.is_file()
    job = db.get_job(engine, rid)
    assert job is not None
    assert job["wav_path"] == ""
