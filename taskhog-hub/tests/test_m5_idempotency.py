"""M5-T3: idempotência local por tarefa no Todoist."""

from __future__ import annotations

from pathlib import Path

import pytest
from sqlalchemy import create_engine

from app.models import db
from app.models.db import init_db
from app.services.todoist_idempotency import (
    create_task_idempotent,
    normalize_idempotency_key,
    task_idempotency_key,
)


@pytest.fixture()
def engine(tmp_path: Path):
    eng = create_engine(f"sqlite:///{tmp_path / 'test.db'}")
    init_db(eng)
    return eng


def test_task_idempotency_key_format():
    assert task_idempotency_key("rec_abcd1234", 0) == "rec_abcd1234:0"
    assert task_idempotency_key("rec_abcd1234", 1, 2) == "rec_abcd1234:1:2"


def test_normalize_truncates_long_keys():
    long_key = "x" * 50
    assert len(normalize_idempotency_key(long_key)) == 36


def test_create_task_idempotent_skips_second_api_call(engine, monkeypatch):
    from app.config import HubConfig

    calls: list[str] = []

    def fake_create(config, *, content, labels=None, idempotency_key=None, **kwargs):
        calls.append(idempotency_key or "")
        return {"id": "td-99", "content": content, "project_id": kwargs.get("project_id")}

    monkeypatch.setattr(
        "app.services.todoist_idempotency.todoist.create_task", fake_create
    )

    cfg = HubConfig(
        bind_host="0.0.0.0",
        bind_port=8088,
        device_tokens=["t"],
        whisper_model="m",
        whisper_device="cpu",
        whisper_compute_type="int8",
        whisper_language="pt",
        whisper_vad_filter=True,
        llm_provider="cloud",
        llm_model="x",
        llm_endpoint="x",
        llm_api_key="x",
        llm_json_strict=True,
        llm_max_retries=1,
        confidence_threshold=0.75,
        todoist_token="tok",
        todoist_base_url="https://api.todoist.com/api/v1",
        todoist_always_label="taskhog",
        todoist_review_label="revisar",
        todoist_inbox_fallback=True,
        todoist_cache_refresh_min=60,
        todoist_due_lang="pt",
        audio_dir="/tmp",
        retain_audio_days=7,
        db_path="/tmp/db",
    )

    rid = "rec_deadbeef"
    first = create_task_idempotent(
        engine,
        cfg,
        recording_id=rid,
        task_idx=0,
        content="Comprar pão",
        labels=["taskhog"],
    )
    second = create_task_idempotent(
        engine,
        cfg,
        recording_id=rid,
        task_idx=0,
        content="Comprar pão",
        labels=["taskhog"],
    )

    assert first["id"] == "td-99"
    assert second["id"] == "td-99"
    assert calls == [f"{rid}:0"]
    assert db.count_todoist_task_keys(engine, rid) == 1


def test_different_task_indices_are_distinct_keys(engine, monkeypatch):
    from app.config import HubConfig

    calls: list[str] = []

    def fake_create(config, *, content, idempotency_key=None, **kwargs):
        calls.append(idempotency_key or "")
        return {"id": f"td-{len(calls)}", "content": content}

    monkeypatch.setattr(
        "app.services.todoist_idempotency.todoist.create_task", fake_create
    )

    cfg = HubConfig(
        bind_host="0.0.0.0",
        bind_port=8088,
        device_tokens=["t"],
        whisper_model="m",
        whisper_device="cpu",
        whisper_compute_type="int8",
        whisper_language="pt",
        whisper_vad_filter=True,
        llm_provider="cloud",
        llm_model="x",
        llm_endpoint="x",
        llm_api_key="x",
        llm_json_strict=True,
        llm_max_retries=1,
        confidence_threshold=0.75,
        todoist_token="tok",
        todoist_base_url="https://api.todoist.com/api/v1",
        todoist_always_label="taskhog",
        todoist_review_label="revisar",
        todoist_inbox_fallback=True,
        todoist_cache_refresh_min=60,
        todoist_due_lang="pt",
        audio_dir="/tmp",
        retain_audio_days=7,
        db_path="/tmp/db",
    )

    rid = "rec_cafebabe"
    create_task_idempotent(engine, cfg, recording_id=rid, task_idx=0, content="A")
    create_task_idempotent(engine, cfg, recording_id=rid, task_idx=1, content="B")

    assert calls == [f"{rid}:0", f"{rid}:1"]
    assert db.count_todoist_task_keys(engine, rid) == 2
