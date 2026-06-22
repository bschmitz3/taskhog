from __future__ import annotations

import json
from pathlib import Path

from fastapi import APIRouter, Depends, File, Form, HTTPException, Request, UploadFile
from fastapi.responses import JSONResponse
from pydantic import ValidationError
from sqlalchemy.exc import IntegrityError

from app.deps import verify_device_token
from app.models import db
from app.models.schemas import (
    RecordingAccepted,
    RecordingDetail,
    RecordingMetadata,
    TaskResult,
)
from app.worker import queue as worker_queue

router = APIRouter(prefix="/v1", tags=["recordings"])

# WAV PCM 16k/16-bit/mono a 120 s ≈ 3.84 MB; folga generosa.
MAX_AUDIO_BYTES = 25 * 1024 * 1024
_CHUNK = 1 << 16


def _accepted(recording_id: str, client_job_id: str, status: str, *, duplicate: bool, code: int):
    body = RecordingAccepted(
        recording_id=recording_id,
        client_job_id=client_job_id,
        status=status,
        duplicate=duplicate,
    )
    return JSONResponse(status_code=code, content=body.model_dump())


@router.post("/recordings")
async def create_recording(
    request: Request,
    audio: UploadFile = File(...),
    metadata: str = Form(...),
    _token: str = Depends(verify_device_token),
):
    config = request.app.state.config
    engine = request.app.state.engine

    try:
        meta = RecordingMetadata.model_validate_json(metadata)
    except ValidationError as exc:
        raise HTTPException(status_code=422, detail=f"metadata inválido: {exc.errors()}")

    # Idempotência por (device_id, client_job_id): reenvio devolve o job existente.
    existing = db.get_job_by_client(engine, meta.device_id, meta.client_job_id)
    if existing is not None:
        return _accepted(
            existing["recording_id"],
            meta.client_job_id,
            existing["status"],
            duplicate=True,
            code=200,
        )

    recording_id = db.new_recording_id()
    audio_dir = Path(config.audio_dir)
    audio_dir.mkdir(parents=True, exist_ok=True)
    wav_path = audio_dir / f"{recording_id}.wav"

    size = 0
    try:
        with wav_path.open("wb") as fh:
            while chunk := await audio.read(_CHUNK):
                size += len(chunk)
                if size > MAX_AUDIO_BYTES:
                    raise HTTPException(status_code=413, detail="áudio acima do limite")
                fh.write(chunk)
    except HTTPException:
        wav_path.unlink(missing_ok=True)
        raise

    if size == 0:
        wav_path.unlink(missing_ok=True)
        raise HTTPException(status_code=422, detail="áudio vazio")

    try:
        db.insert_job(
            engine,
            recording_id=recording_id,
            device_id=meta.device_id,
            client_job_id=meta.client_job_id,
            wav_path=str(wav_path),
            rtc_timestamp=meta.rtc_timestamp,
            rtc_valid=meta.rtc_valid,
            duration_s=meta.duration_s,
            received_at=db.now_iso(),
        )
    except IntegrityError:
        # Corrida: outro request com o mesmo client_job_id ganhou. Devolve o dele.
        wav_path.unlink(missing_ok=True)
        existing = db.get_job_by_client(engine, meta.device_id, meta.client_job_id)
        if existing is not None:
            return _accepted(
                existing["recording_id"],
                meta.client_job_id,
                existing["status"],
                duplicate=True,
                code=200,
            )
        raise HTTPException(status_code=409, detail="conflito de idempotência")

    worker_queue.notify_new_job(request.app)
    return _accepted(recording_id, meta.client_job_id, "queued", duplicate=False, code=202)


@router.get("/recordings/{recording_id}", response_model=RecordingDetail)
def get_recording(request: Request, recording_id: str) -> RecordingDetail:
    engine = request.app.state.engine
    job = db.get_job(engine, recording_id)
    if job is None:
        raise HTTPException(status_code=404, detail="recording não encontrado")

    tasks: list[TaskResult] = []
    if job.get("created_tasks"):
        try:
            for item in json.loads(job["created_tasks"]):
                tasks.append(
                    TaskResult(
                        todoist_id=str(item.get("todoist_id", "")),
                        content=item.get("content", ""),
                        project=item.get("project"),
                        confidence=item.get("confidence"),
                        routed_to=item.get("routed_to"),
                        due=item.get("due"),
                        priority=item.get("priority"),
                        labels=item.get("labels", []),
                    )
                )
        except (json.JSONDecodeError, TypeError):
            tasks = []

    return RecordingDetail(
        recording_id=job["recording_id"],
        status=job["status"],
        transcript=job.get("transcript"),
        tasks=tasks,
        error=job.get("error"),
    )
