"""Retenção e limpeza de WAV no Hub (M5-T5, Spec 02 §4)."""

from __future__ import annotations

import logging
from datetime import datetime, timedelta
from pathlib import Path
from typing import TYPE_CHECKING

from app.models import db

if TYPE_CHECKING:
    from sqlalchemy.engine import Engine

    from app.config import HubConfig

logger = logging.getLogger(__name__)

_PURGE_INTERVAL_S = 24 * 60 * 60


def delete_job_wav(wav_path: str | None) -> bool:
    if not wav_path:
        return False
    path = Path(wav_path)
    if not path.is_file():
        return False
    path.unlink()
    logger.info("WAV removido: %s", path)
    return True


def after_job_done(
    engine: Engine, config: HubConfig, recording_id: str, wav_path: str | None
) -> None:
    """`retain_audio_days=0` → apaga o WAV assim que o job termina com sucesso."""
    if config.retain_audio_days > 0:
        return
    if delete_job_wav(wav_path):
        db.clear_wav_path(engine, recording_id)


def purge_expired(engine: Engine, config: HubConfig) -> int:
    """Remove WAVs de jobs `done` mais antigos que `retain_audio_days`."""
    if config.retain_audio_days <= 0:
        return 0

    cutoff = (
        datetime.now().astimezone() - timedelta(days=config.retain_audio_days)
    ).isoformat(timespec="seconds")
    removed = 0
    for job in db.list_done_jobs_with_wav_before(engine, cutoff):
        if delete_job_wav(job.get("wav_path")):
            db.clear_wav_path(engine, job["recording_id"])
            removed += 1

    if removed:
        logger.info("purge áudio: %d WAV(s) expirado(s) removido(s)", removed)
    return removed


def purge_interval_seconds() -> int:
    return _PURGE_INTERVAL_S
