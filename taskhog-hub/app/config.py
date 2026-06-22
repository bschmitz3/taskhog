from __future__ import annotations

import os
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import yaml

_ENV_PATTERN = re.compile(r"\$\{([^}]+)\}")


def _expand_env(value: str) -> str:
    def repl(match: re.Match[str]) -> str:
        return os.environ.get(match.group(1), "")

    return _ENV_PATTERN.sub(repl, value)


def _expand_tree(node: Any) -> Any:
    if isinstance(node, dict):
        return {k: _expand_tree(v) for k, v in node.items()}
    if isinstance(node, list):
        return [_expand_tree(item) for item in node]
    if isinstance(node, str):
        return _expand_env(node)
    return node


@dataclass(frozen=True)
class HubConfig:
    bind_host: str
    bind_port: int
    device_tokens: list[str]
    whisper_model: str
    whisper_device: str
    whisper_compute_type: str
    whisper_language: str
    whisper_vad_filter: bool
    llm_provider: str
    llm_model: str
    llm_endpoint: str
    llm_api_key: str
    llm_json_strict: bool
    llm_max_retries: int
    confidence_threshold: float
    todoist_token: str
    todoist_base_url: str
    todoist_always_label: str
    todoist_review_label: str
    todoist_inbox_fallback: bool
    todoist_cache_refresh_min: int
    todoist_due_lang: str
    audio_dir: str
    retain_audio_days: int
    db_path: str


def load_config(path: str | Path | None = None) -> HubConfig:
    config_path = Path(path or os.environ.get("HUB_CONFIG", "/app/hub.yaml"))
    with config_path.open(encoding="utf-8") as fh:
        raw = yaml.safe_load(fh)

    data = _expand_tree(raw)
    bind = data["server"]["bind"]
    host, port_str = bind.rsplit(":", 1)

    tokens = [t for t in data["server"]["device_tokens"] if t]

    return HubConfig(
        bind_host=host,
        bind_port=int(port_str),
        device_tokens=tokens,
        whisper_model=data["whisper"]["model"],
        whisper_device=data["whisper"]["device"],
        whisper_compute_type=data["whisper"]["compute_type"],
        whisper_language=data["whisper"]["language"],
        whisper_vad_filter=bool(data["whisper"]["vad_filter"]),
        llm_provider=data["llm"]["provider"],
        llm_model=data["llm"]["model"],
        llm_endpoint=data["llm"]["endpoint"],
        llm_api_key=data["llm"]["api_key"],
        llm_json_strict=bool(data["llm"]["json_strict"]),
        llm_max_retries=int(data["llm"]["max_retries"]),
        confidence_threshold=float(data["llm"]["confidence_threshold"]),
        todoist_token=data["todoist"]["token"],
        todoist_base_url=data["todoist"]["base_url"],
        todoist_always_label=data["todoist"]["always_label"],
        todoist_review_label=data["todoist"]["review_label"],
        todoist_inbox_fallback=bool(data["todoist"]["inbox_fallback"]),
        todoist_cache_refresh_min=int(data["todoist"]["cache_refresh_min"]),
        todoist_due_lang=data["todoist"]["due_lang"],
        audio_dir=data["storage"]["audio_dir"],
        retain_audio_days=int(data["storage"]["retain_audio_days"]),
        db_path=data["storage"]["db_path"],
    )
