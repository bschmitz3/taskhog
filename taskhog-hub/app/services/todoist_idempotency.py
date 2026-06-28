"""Criação idempotente de tarefas no Todoist (M5-T3).

A API unificada v1 documenta `X-Request-Id` para correlação; dedupe não é
garantido. O Hub persiste `(idempotency_key → todoist_id)` em SQLite antes de
reprocessar jobs interrompidos no `creating`.
"""

from __future__ import annotations

import logging
from typing import TYPE_CHECKING, Any

from app.models import db
from app.services import todoist

if TYPE_CHECKING:
    from sqlalchemy.engine import Engine

    from app.config import HubConfig

logger = logging.getLogger(__name__)

# Limite documentado do header X-Request-Id (REST v1).
_MAX_KEY_LEN = 36


def normalize_idempotency_key(key: str) -> str:
    if len(key) <= _MAX_KEY_LEN:
        return key
    return key[:_MAX_KEY_LEN]


def task_idempotency_key(recording_id: str, task_idx: int, sub_idx: int | None = None) -> str:
    if sub_idx is None:
        raw = f"{recording_id}:{task_idx}"
    else:
        raw = f"{recording_id}:{task_idx}:{sub_idx}"
    return normalize_idempotency_key(raw)


def create_task_idempotent(
    engine: Engine,
    config: HubConfig,
    *,
    recording_id: str,
    task_idx: int,
    sub_idx: int | None = None,
    content: str,
    labels: list[str] | None = None,
    project_id: str | None = None,
    due_string: str | None = None,
    priority: int | None = None,
    parent_id: str | None = None,
) -> dict[str, Any]:
    key = task_idempotency_key(recording_id, task_idx, sub_idx)

    cached = db.get_todoist_task_by_key(engine, key)
    if cached is not None:
        logger.info("idempotência hit %s → todoist %s", key, cached.get("id"))
        if project_id and not cached.get("project_id"):
            cached["project_id"] = project_id
        return cached

    created = todoist.create_task(
        config,
        content=content,
        labels=labels,
        project_id=project_id,
        due_string=due_string,
        priority=priority,
        parent_id=parent_id,
        idempotency_key=key,
    )

    todoist_id = str(created.get("id", ""))
    if todoist_id:
        db.put_todoist_task_key(
            engine,
            idempotency_key=key,
            recording_id=recording_id,
            todoist_id=todoist_id,
            content=content,
        )
    return created
