"""Testes M4-T2/T3/T4: LLMProvider cloud, prompt e validação JSON."""

from __future__ import annotations

from datetime import datetime, timezone
from unittest.mock import MagicMock

import httpx
import pytest

from app.config import HubConfig
from app.models.schemas import StructuredResult
from app.services.llm.base import LLMError, get_llm_provider
from app.services.llm.cloud import CloudLLMProvider
from app.services.structuring import (
    build_system_prompt,
    extract_json_object,
    parse_structured_json,
)

NOW = datetime(2026, 6, 27, 12, 0, tzinfo=timezone.utc)
PROJECTS = ["Compras", "Trabalho", "Pessoal"]
LABELS = ["urgente", "casa"]

VALID_JSON = """{
  "language": "pt",
  "needs_review": false,
  "tasks": [{
    "content": "Comprar pão",
    "project_suggestion": "Compras",
    "project_confidence": 0.7,
    "due_string": null,
    "priority": null,
    "labels": [],
    "subtasks": [],
    "notes": null
  }]
}"""


def _hub_config(**overrides) -> HubConfig:
    defaults = dict(
        bind_host="0.0.0.0",
        bind_port=8088,
        device_tokens=["tok"],
        whisper_model="medium",
        whisper_device="cpu",
        whisper_compute_type="int8",
        whisper_language="pt",
        whisper_vad_filter=True,
        llm_provider="cloud",
        llm_model="gpt-4o-mini",
        llm_endpoint="https://api.openai.com/v1",
        llm_api_key="sk-test",
        llm_json_strict=True,
        llm_max_retries=1,
        confidence_threshold=0.75,
        todoist_token="td",
        todoist_base_url="https://api.todoist.com/api/v1",
        todoist_always_label="taskhog",
        todoist_review_label="revisar",
        todoist_inbox_fallback=True,
        todoist_cache_refresh_min=60,
        todoist_due_lang="pt",
        audio_dir="/tmp/audio",
        retain_audio_days=7,
        db_path="/tmp/taskhog.db",
    )
    defaults.update(overrides)
    return HubConfig(**defaults)


def test_build_system_prompt_injects_projects_and_labels():
    prompt = build_system_prompt(now=NOW, projects=PROJECTS, labels=LABELS)
    assert "Compras" in prompt
    assert "Trabalho" in prompt
    assert "urgente" in prompt
    assert "2026-06-27" in prompt
    assert '"type": "object"' in prompt


def test_parse_structured_json_valid():
    result = parse_structured_json(VALID_JSON, strict=True)
    assert isinstance(result, StructuredResult)
    assert result.tasks[0].content == "Comprar pão"
    assert result.tasks[0].project_suggestion == "Compras"


def test_parse_rejects_extra_properties():
    bad = VALID_JSON.replace('"needs_review": false', '"needs_review": false, "extra": 1')
    with pytest.raises(ValueError, match="schema inválido"):
        parse_structured_json(bad, strict=True)


def test_extract_json_strict_rejects_markdown():
    wrapped = f"```json\n{VALID_JSON}\n```"
    with pytest.raises(ValueError, match="markdown"):
        extract_json_object(wrapped, strict=True)


def test_extract_json_non_strict_strips_fence():
    wrapped = f"```json\n{VALID_JSON}\n```"
    extracted = extract_json_object(wrapped, strict=False)
    assert extracted.startswith("{")


def test_get_llm_provider_cloud():
    provider = get_llm_provider(_hub_config())
    assert isinstance(provider, CloudLLMProvider)


def test_cloud_provider_structure_mocked(monkeypatch: pytest.MonkeyPatch):
    config = _hub_config()
    provider = CloudLLMProvider(config)

    mock_response = MagicMock()
    mock_response.raise_for_status = MagicMock()
    mock_response.json.return_value = {
        "choices": [{"message": {"content": VALID_JSON}}],
    }

    def fake_post(url, **kwargs):
        assert url == "https://api.openai.com/v1/chat/completions"
        assert kwargs["json"]["response_format"] == {"type": "json_object"}
        assert kwargs["json"]["model"] == "gpt-4o-mini"
        messages = kwargs["json"]["messages"]
        assert messages[0]["role"] == "system"
        assert "Compras" in messages[0]["content"]
        assert messages[1]["content"] == 'Transcrição: "comprar pão"'
        return mock_response

    monkeypatch.setattr(httpx, "post", fake_post)

    result = provider.structure(
        "comprar pão",
        now=NOW,
        projects=PROJECTS,
        labels=LABELS,
    )
    assert result.tasks[0].content == "Comprar pão"


def test_cloud_provider_retries_on_invalid_json(monkeypatch: pytest.MonkeyPatch):
    config = _hub_config(llm_max_retries=1)
    provider = CloudLLMProvider(config)
    calls: list[dict] = []

    def fake_post(url, **kwargs):
        calls.append(kwargs["json"])
        mock_response = MagicMock()
        mock_response.raise_for_status = MagicMock()
        if len(calls) == 1:
            mock_response.json.return_value = {
                "choices": [{"message": {"content": "não é json"}}],
            }
        else:
            mock_response.json.return_value = {
                "choices": [{"message": {"content": VALID_JSON}}],
            }
        return mock_response

    monkeypatch.setattr(httpx, "post", fake_post)

    result = provider.structure(
        "comprar pão",
        now=NOW,
        projects=PROJECTS,
        labels=LABELS,
    )
    assert result.tasks[0].content == "Comprar pão"
    assert len(calls) == 2
    assert len(calls[1]["messages"]) == 3  # system + user + correction


def test_cloud_provider_raises_after_max_retries(monkeypatch: pytest.MonkeyPatch):
    config = _hub_config(llm_max_retries=1)
    provider = CloudLLMProvider(config)

    def fake_post(url, **kwargs):
        mock_response = MagicMock()
        mock_response.raise_for_status = MagicMock()
        mock_response.json.return_value = {
            "choices": [{"message": {"content": "lixo"}}],
        }
        return mock_response

    monkeypatch.setattr(httpx, "post", fake_post)

    with pytest.raises(LLMError, match="JSON válido"):
        provider.structure("x", now=NOW, projects=[], labels=[])
