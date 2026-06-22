"""Cliente Todoist mínimo (unified API v1).

A REST API v2 (`/rest/v2`) foi desligada pela Todoist em 2026-02-10; tudo passa
pela unified API v1 (`https://api.todoist.com/api/v1`). Em M2 só criamos a tarefa
crua (transcript) no Inbox + label `taskhog`. Roteamento por projeto/confiança vem
em M4.
"""

from __future__ import annotations

import logging
from typing import TYPE_CHECKING, Any

import httpx

if TYPE_CHECKING:
    from app.config import HubConfig

logger = logging.getLogger(__name__)


def create_task(
    config: HubConfig,
    *,
    content: str,
    labels: list[str] | None = None,
    project_id: str | None = None,
    due_string: str | None = None,
    priority: int | None = None,
    parent_id: str | None = None,
    idempotency_key: str | None = None,
    timeout: float = 30.0,
) -> dict[str, Any]:
    """Cria uma tarefa no Todoist e devolve o objeto retornado pela API."""
    payload: dict[str, Any] = {"content": content}
    if project_id:
        payload["project_id"] = project_id
    if parent_id:
        payload["parent_id"] = parent_id
    if due_string:
        payload["due_string"] = due_string
        payload["due_lang"] = config.todoist_due_lang
    if priority:
        payload["priority"] = priority
    if labels:
        payload["labels"] = sorted(set(labels))

    headers = {
        "Authorization": f"Bearer {config.todoist_token}",
        "Content-Type": "application/json",
    }
    # X-Request-Id: evita duplicar se o Hub reiniciar no meio do `creating`.
    if idempotency_key:
        headers["X-Request-Id"] = idempotency_key

    url = f"{config.todoist_base_url.rstrip('/')}/tasks"
    resp = httpx.post(url, json=payload, headers=headers, timeout=timeout)
    resp.raise_for_status()
    return resp.json()
