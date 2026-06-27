"""Cache de projetos/labels Todoist (M4-T1, Spec 02 §10.1)."""

from __future__ import annotations

import logging
from typing import TYPE_CHECKING, Any

import httpx

from app.models import db

if TYPE_CHECKING:
    from sqlalchemy.engine import Engine

    from app.config import HubConfig

logger = logging.getLogger(__name__)

_KIND_PROJECT = "project"
_KIND_LABEL = "label"


def _auth_headers(config: HubConfig) -> dict[str, str]:
    return {"Authorization": f"Bearer {config.todoist_token}"}


def _fetch_paginated(config: HubConfig, resource: str) -> list[dict[str, Any]]:
    """GET /projects ou /labels com paginação por cursor (unified API v1)."""
    base = config.todoist_base_url.rstrip("/")
    url = f"{base}/{resource}"
    headers = _auth_headers(config)
    results: list[dict[str, Any]] = []
    cursor: str | None = None

    while True:
        params: dict[str, str] = {}
        if cursor:
            params["cursor"] = cursor
        resp = httpx.get(url, headers=headers, params=params, timeout=30.0)
        resp.raise_for_status()
        data = resp.json()
        batch = data.get("results", [])
        if isinstance(batch, list):
            results.extend(batch)
        cursor = data.get("next_cursor")
        if not cursor:
            break

    return results


def fetch_projects(config: HubConfig) -> list[tuple[str, str]]:
    rows = _fetch_paginated(config, "projects")
    out: list[tuple[str, str]] = []
    for item in rows:
        name = (item.get("name") or "").strip()
        ext_id = item.get("id")
        if name and ext_id is not None:
            out.append((str(ext_id), name))
    return out


def fetch_labels(config: HubConfig) -> list[tuple[str, str]]:
    rows = _fetch_paginated(config, "labels")
    out: list[tuple[str, str]] = []
    for item in rows:
        name = (item.get("name") or "").strip()
        ext_id = item.get("id")
        if name and ext_id is not None:
            out.append((str(ext_id), name))
    return out


def refresh_project_label_cache(
    engine: Engine, config: HubConfig
) -> dict[str, int]:
    """Baixa projetos/labels da API e persiste em todoist_cache."""
    if not config.todoist_token:
        raise RuntimeError("TODOIST_TOKEN não configurado")

    projects = fetch_projects(config)
    labels = fetch_labels(config)
    db.replace_todoist_cache(engine, _KIND_PROJECT, projects)
    db.replace_todoist_cache(engine, _KIND_LABEL, labels)

    logger.info(
        "Todoist cache atualizado: %d projetos, %d labels",
        len(projects),
        len(labels),
    )
    return {"projects": len(projects), "labels": len(labels)}


def get_cached_project_names(engine: Engine) -> list[str]:
    return [row["name"] for row in db.list_todoist_cache(engine, _KIND_PROJECT)]


def get_cached_label_names(engine: Engine) -> list[str]:
    return [row["name"] for row in db.list_todoist_cache(engine, _KIND_LABEL)]


def get_cached_projects(engine: Engine) -> list[dict[str, str]]:
    return db.list_todoist_cache(engine, _KIND_PROJECT)


def get_cached_labels(engine: Engine) -> list[dict[str, str]]:
    return db.list_todoist_cache(engine, _KIND_LABEL)
