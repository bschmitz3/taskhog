"""Pipeline por job: transcribing → structuring → creating → done (M4)."""

from __future__ import annotations

import asyncio
import logging
from collections.abc import Callable
from datetime import datetime
from typing import TYPE_CHECKING, Any

from app.models import db
from app.models.schemas import StructuredResult, StructuredTaskItem
from app.services import todoist, transcribe
from app.services.audio_retention import after_job_done
from app.services.confidence import merge_labels, route_task
from app.services.llm.base import get_llm_provider
from app.services.todoist_cache import get_cached_label_names, get_cached_project_names, get_cached_projects

if TYPE_CHECKING:
    from sqlalchemy.engine import Engine

    from app.config import HubConfig

logger = logging.getLogger(__name__)

_EMPTY_TRANSCRIPT_CONTENT = "(áudio sem fala reconhecida)"


def _reference_now(job: dict[str, Any]) -> datetime:
    use_rtc = bool(job.get("rtc_valid"))
    raw = job.get("rtc_timestamp") if use_rtc else job.get("received_at")
    if raw:
        try:
            return datetime.fromisoformat(str(raw))
        except ValueError:
            pass
    return datetime.now().astimezone()


def _empty_transcript_result(content: str) -> StructuredResult:
    return StructuredResult(
        language="pt",
        needs_review=True,
        tasks=[
            StructuredTaskItem(
                content=content,
                project_suggestion=None,
                project_confidence=0.0,
            )
        ],
    )


def _load_structured(job: dict[str, Any], transcript: str, config: HubConfig, engine: Engine) -> StructuredResult:
    raw = job.get("llm_json")
    if raw:
        return StructuredResult.model_validate_json(raw)

    if not transcript:
        return _empty_transcript_result(_EMPTY_TRANSCRIPT_CONTENT)

    db.set_status(engine, job["recording_id"], "structuring")
    llm = get_llm_provider(config)
    structured = llm.structure(
        transcript,
        now=_reference_now(job),
        projects=get_cached_project_names(engine),
        labels=get_cached_label_names(engine),
    )
    db.set_llm_json(engine, job["recording_id"], structured.model_dump_json())
    return structured


async def _create_todoist_tasks(
    config: HubConfig,
    *,
    recording_id: str,
    structured: StructuredResult,
    projects: list[dict[str, str]],
    cached_labels: list[str],
    start_idx: int = 0,
    on_task_created: Callable[[dict[str, Any]], None] | None = None,
) -> list[dict[str, Any]]:
    created: list[dict[str, Any]] = []

    for idx, task_spec in enumerate(structured.tasks):
        if idx < start_idx:
            continue

        route = route_task(
            task_spec,
            global_needs_review=structured.needs_review,
            projects=projects,
            threshold=config.confidence_threshold,
        )
        labels = merge_labels(
            task_spec,
            route,
            always_label=config.todoist_always_label,
            review_label=config.todoist_review_label,
            cached_label_names=cached_labels,
            global_needs_review=structured.needs_review,
        )

        parent = await asyncio.to_thread(
            todoist.create_task,
            config,
            content=task_spec.content,
            labels=labels,
            project_id=route.project_id,
            due_string=task_spec.due_string,
            priority=task_spec.priority,
            idempotency_key=f"{recording_id}:{idx}",
        )

        for sub_idx, sub_content in enumerate(task_spec.subtasks):
            await asyncio.to_thread(
                todoist.create_task,
                config,
                content=sub_content,
                project_id=parent.get("project_id"),
                parent_id=str(parent.get("id", "")),
                idempotency_key=f"{recording_id}:{idx}:{sub_idx}",
            )

        task_result = {
            "todoist_id": str(parent.get("id", "")),
            "content": task_spec.content,
            "project": route.project_name,
            "confidence": route.confidence,
            "routed_to": route.routed_to,
            "due": task_spec.due_string,
            "priority": task_spec.priority,
            "labels": labels,
        }
        created.append(task_result)
        if on_task_created is not None:
            on_task_created(task_result)

    return created


async def process_job(engine: Engine, config: HubConfig, job: dict[str, Any]) -> None:
    recording_id = job["recording_id"]
    try:
        job = db.get_job(engine, recording_id) or job

        transcript = job.get("transcript")
        if transcript is not None:
            transcript = transcript.strip()
        else:
            transcript = await asyncio.to_thread(
                transcribe.transcribe, job["wav_path"], config
            )
            transcript = (transcript or "").strip()
            db.set_transcript(engine, recording_id, transcript)

        structured = await asyncio.to_thread(
            _load_structured, job, transcript, config, engine
        )

        created = db.load_created_tasks(job)
        start_idx = len(created)

        db.set_status(engine, recording_id, "creating")
        projects = get_cached_projects(engine)
        cached_labels = get_cached_label_names(engine)

        if start_idx < len(structured.tasks):

            def persist_task(task: dict[str, Any]) -> None:
                db.append_created_task(engine, recording_id, task)

            new_tasks = await _create_todoist_tasks(
                config,
                recording_id=recording_id,
                structured=structured,
                projects=projects,
                cached_labels=cached_labels,
                start_idx=start_idx,
                on_task_created=persist_task,
            )
            created.extend(new_tasks)

        db.set_done(engine, recording_id, created)
        after_job_done(engine, config, recording_id, job.get("wav_path"))
        logger.info(
            "Job %s done → %d tarefa(s) no Todoist",
            recording_id,
            len(created),
        )
    except Exception as exc:  # noqa: BLE001 — registra e marca error/requeue
        job = db.get_job(engine, recording_id) or job
        partial = db.load_created_tasks(job)
        if partial or job.get("status") == "creating":
            db.set_status(engine, recording_id, "queued")
            logger.warning(
                "Job %s interrompido em creating — requeued (%d tarefa(s) já criadas): %s",
                recording_id,
                len(partial),
                exc,
            )
            return

        db.increment_attempts(engine, recording_id)
        db.set_status(engine, recording_id, "error", error=str(exc)[:500])
        logger.exception("Job %s erro no pipeline", recording_id)
