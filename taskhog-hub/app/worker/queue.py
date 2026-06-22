"""Worker asyncio: consome jobs `queued` da tabela SQLite e roda o pipeline.

Um único worker processa em série (Whisper é CPU-bound). A fila vive no DB,
então o estado sobrevive a restart: no boot, jobs in-flight voltam a `queued`
(ver `db.reset_stuck_jobs`).
"""

from __future__ import annotations

import asyncio
import logging

from fastapi import FastAPI

from app.models import db
from app.worker import pipeline

logger = logging.getLogger(__name__)

_IDLE_POLL_S = 5.0


class Worker:
    def __init__(self, app: FastAPI) -> None:
        self._app = app
        self._wake = asyncio.Event()
        self._task: asyncio.Task | None = None
        self._stop = False

    def notify(self) -> None:
        self._wake.set()

    def start(self) -> None:
        self._task = asyncio.create_task(self._loop(), name="taskhog-worker")

    async def stop(self) -> None:
        self._stop = True
        self._wake.set()
        if self._task is not None:
            try:
                await asyncio.wait_for(self._task, timeout=5.0)
            except (asyncio.TimeoutError, asyncio.CancelledError):
                self._task.cancel()

    async def _loop(self) -> None:
        engine = self._app.state.engine
        config = self._app.state.config

        requeued = db.reset_stuck_jobs(engine)
        if requeued:
            logger.info("Worker: %d job(s) in-flight revertidos para queued", requeued)

        while not self._stop:
            job = db.claim_next_queued(engine)
            if job is None:
                self._wake.clear()
                try:
                    await asyncio.wait_for(self._wake.wait(), timeout=_IDLE_POLL_S)
                except asyncio.TimeoutError:
                    pass
                continue

            try:
                await pipeline.process_job(engine, config, job)
            except Exception:  # noqa: BLE001 — nunca derrubar o loop
                logger.exception("Worker: job %s falhou no pipeline", job["recording_id"])


def start_worker(app: FastAPI) -> None:
    worker = Worker(app)
    app.state.worker = worker
    worker.start()


def notify_new_job(app: FastAPI) -> None:
    worker: Worker | None = getattr(app.state, "worker", None)
    if worker is not None:
        worker.notify()


async def stop_worker(app: FastAPI) -> None:
    worker: Worker | None = getattr(app.state, "worker", None)
    if worker is not None:
        await worker.stop()
