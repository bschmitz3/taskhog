from __future__ import annotations

from fastapi import APIRouter, Request

from app.models import db
from app.models.db import count_jobs_by_status
from app.models.schemas import (
    HealthResponse,
    LastTaskSummary,
    StatusResponse,
    iso_now,
)
from app.services import transcribe

router = APIRouter(prefix="/v1", tags=["status"])


@router.get("/health", response_model=HealthResponse)
def health(request: Request) -> HealthResponse:
    config = request.app.state.config
    whisper = transcribe.whisper_status()
    todoist = "ok" if config.todoist_token else "pending"
    ok = whisper != "error" and todoist != "error"
    return HealthResponse(ok=ok, whisper=whisper, todoist=todoist)


@router.get("/status", response_model=StatusResponse)
def status(request: Request) -> StatusResponse:
    engine = request.app.state.engine
    queued = count_jobs_by_status(engine, "queued")
    processing = sum(
        count_jobs_by_status(engine, s)
        for s in ("transcribing", "structuring", "creating")
    )
    errors = count_jobs_by_status(engine, "error")

    last = db.get_last_task(engine)
    last_task = LastTaskSummary(**last) if last else None

    return StatusResponse(
        queue_pending=queued,
        processing=processing,
        processed_today=db.count_processed_today(engine),
        errors=errors,
        last_task=last_task,
        server_time=iso_now(),
    )
