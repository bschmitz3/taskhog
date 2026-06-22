"""Pipeline por job (M2 — caminho cru, sem LLM).

queued → transcribing (whisper) → creating (Todoist Inbox + label taskhog) → done.
A estruturação por LLM e o roteamento por projeto entram em M4.
"""

from __future__ import annotations

import asyncio
import logging
from typing import TYPE_CHECKING, Any

from app.models import db
from app.services import todoist, transcribe

if TYPE_CHECKING:
    from sqlalchemy.engine import Engine

    from app.config import HubConfig

logger = logging.getLogger(__name__)

_EMPTY_TRANSCRIPT_CONTENT = "(áudio sem fala reconhecida)"


async def process_job(engine: Engine, config: HubConfig, job: dict[str, Any]) -> None:
    recording_id = job["recording_id"]
    try:
        transcript = await asyncio.to_thread(
            transcribe.transcribe, job["wav_path"], config
        )
        transcript = (transcript or "").strip()
        db.set_transcript(engine, recording_id, transcript)

        db.set_status(engine, recording_id, "creating")

        needs_review = not transcript
        content = transcript or _EMPTY_TRANSCRIPT_CONTENT
        labels = [config.todoist_always_label]
        if needs_review:
            labels.append(config.todoist_review_label)

        task = await asyncio.to_thread(
            todoist.create_task,
            config,
            content=content,
            labels=labels,
            project_id=None,  # M2: sempre Inbox
            idempotency_key=f"{recording_id}:0",
        )

        created = [
            {
                "todoist_id": str(task.get("id", "")),
                "content": content,
                "project": None,
                "routed_to": "inbox",
                "labels": labels,
            }
        ]
        db.set_done(engine, recording_id, created)
        logger.info(
            "Job %s done → Todoist task %s", recording_id, task.get("id")
        )
    except Exception as exc:  # noqa: BLE001 — registra e marca error
        db.increment_attempts(engine, recording_id)
        db.set_status(engine, recording_id, "error", error=str(exc)[:500])
        logger.exception("Job %s erro no pipeline", recording_id)
