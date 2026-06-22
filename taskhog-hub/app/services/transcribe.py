"""faster-whisper — carrega modelo no startup; transcrição usada em M2."""

from __future__ import annotations

import logging
import threading
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from app.config import HubConfig

logger = logging.getLogger(__name__)

_lock = threading.Lock()
_model = None
_ready = False
_load_error: str | None = None


def whisper_status() -> str:
    if _ready:
        return "ready"
    if _load_error:
        return "error"
    return "pending"


def whisper_load_error() -> str | None:
    return _load_error


def get_model():
    with _lock:
        return _model


def warmup_whisper(config: HubConfig) -> None:
    global _model, _ready, _load_error
    with _lock:
        if _ready or _model is not None:
            return
        try:
            from faster_whisper import WhisperModel

            logger.info(
                "Carregando Whisper %s (%s/%s)...",
                config.whisper_model,
                config.whisper_device,
                config.whisper_compute_type,
            )
            _model = WhisperModel(
                config.whisper_model,
                device=config.whisper_device,
                compute_type=config.whisper_compute_type,
            )
            _ready = True
            _load_error = None
            logger.info("Whisper pronto")
        except Exception as exc:  # noqa: BLE001 — expor via health
            _load_error = str(exc)
            logger.exception("Falha ao carregar Whisper")
            raise


def warmup_whisper_background(config: HubConfig) -> None:
    def _run() -> None:
        try:
            warmup_whisper(config)
        except Exception:
            pass

    thread = threading.Thread(target=_run, name="whisper-warmup", daemon=True)
    thread.start()


def transcribe(wav_path: str, config: HubConfig) -> str:
    if not _ready or _model is None:
        raise RuntimeError("Whisper não carregado")

    segments, _info = _model.transcribe(
        wav_path,
        language=config.whisper_language,
        vad_filter=config.whisper_vad_filter,
    )
    return " ".join(s.text.strip() for s in segments).strip()
