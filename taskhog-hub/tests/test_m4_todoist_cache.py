"""M4-T1: cache de projetos/labels Todoist."""

from __future__ import annotations

from pathlib import Path

import pytest
from fastapi.testclient import TestClient

from app.models import db
from app.models.db import create_db_engine, init_db
from app.services import todoist_cache

TOKEN = "test-device-token"


@pytest.fixture()
def hub(tmp_path: Path, monkeypatch: pytest.MonkeyPatch):
    config_path = tmp_path / "hub.yaml"
    db_path = tmp_path / "taskhog.db"
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
  db_path: "{db_path}"
""",
        encoding="utf-8",
    )
    monkeypatch.setenv("HUB_CONFIG", str(config_path))

    from app.config import load_config

    config = load_config(str(config_path))
    engine = create_db_engine(str(db_path))
    init_db(engine)
    return config, engine


def test_refresh_populates_cache(hub, monkeypatch: pytest.MonkeyPatch):
    config, engine = hub

    def fake_fetch_projects(cfg):
        assert cfg.todoist_token == "tok"
        return [("p1", "Compras"), ("p2", "Personal")]

    def fake_fetch_labels(cfg):
        return [("l1", "taskhog"), ("l2", "revisar")]

    monkeypatch.setattr(todoist_cache, "fetch_projects", fake_fetch_projects)
    monkeypatch.setattr(todoist_cache, "fetch_labels", fake_fetch_labels)

    counts = todoist_cache.refresh_project_label_cache(engine, config)
    assert counts == {"projects": 2, "labels": 2}
    assert todoist_cache.get_cached_project_names(engine) == ["Compras", "Personal"]
    assert todoist_cache.get_cached_label_names(engine) == ["revisar", "taskhog"]


def test_replace_cache_is_atomic(hub):
    _, engine = hub
    db.replace_todoist_cache(engine, "project", [("a", "Alpha")])
    assert db.count_todoist_cache(engine, "project") == 1

    db.replace_todoist_cache(engine, "project", [("b", "Beta"), ("c", "Gamma")])
    names = todoist_cache.get_cached_project_names(engine)
    assert names == ["Beta", "Gamma"]
    assert db.count_todoist_cache(engine, "project") == 2


def test_admin_refresh_endpoint(hub, monkeypatch: pytest.MonkeyPatch):
    monkeypatch.setattr(
        todoist_cache,
        "refresh_project_label_cache",
        lambda engine, config: {"projects": 3, "labels": 2},
    )

    from app import main
    from app.services import transcribe

    monkeypatch.setattr(transcribe, "whisper_status", lambda: "ready")
    monkeypatch.setattr(transcribe, "warmup_whisper_background", lambda cfg: None)

    with TestClient(main.app) as client:
        resp = client.post(
            "/v1/projects/refresh",
            headers={"Authorization": f"Bearer {TOKEN}"},
        )
        assert resp.status_code == 200, resp.text
        body = resp.json()
        assert body["projects"] == 3
        assert body["labels"] == 2
        assert "refreshed_at" in body


def test_admin_refresh_requires_auth(hub, monkeypatch: pytest.MonkeyPatch):
    from app import main
    from app.services import transcribe

    monkeypatch.setattr(transcribe, "whisper_status", lambda: "ready")
    monkeypatch.setattr(transcribe, "warmup_whisper_background", lambda cfg: None)

    with TestClient(main.app) as client:
        assert client.post("/v1/projects/refresh").status_code == 401
