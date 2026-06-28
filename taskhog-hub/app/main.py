from __future__ import annotations

import asyncio
import contextlib
import logging
from contextlib import asynccontextmanager
from pathlib import Path

from fastapi import FastAPI

from app.api import admin, recordings, status
from app.config import load_config
from app.deps import bind_tokens
from app.models.db import create_db_engine, init_db
from app.services import todoist_cache, transcribe
from app.services.audio_retention import purge_expired, purge_interval_seconds
from app.worker import queue as worker_queue

logger = logging.getLogger(__name__)


async def _todoist_cache_refresh_loop(app: FastAPI) -> None:
    config = app.state.config
    engine = app.state.engine
    interval_s = max(60, config.todoist_cache_refresh_min * 60)

    while True:
        await asyncio.sleep(interval_s)
        try:
            await asyncio.to_thread(
                todoist_cache.refresh_project_label_cache, engine, config
            )
        except Exception:
            logger.exception("Refresh periódico do cache Todoist falhou")


async def _audio_purge_loop(app: FastAPI) -> None:
    config = app.state.config
    engine = app.state.engine

    while True:
        await asyncio.sleep(purge_interval_seconds())
        try:
            await asyncio.to_thread(purge_expired, engine, config)
        except Exception:
            logger.exception("Purge periódico de áudio falhou")


@asynccontextmanager
async def lifespan(app: FastAPI):
    config = load_config()
    engine = create_db_engine(config.db_path)
    init_db(engine)

    app.state.config = config
    app.state.engine = engine
    bind_tokens(config.device_tokens)

    audio_dir = Path(config.audio_dir)
    audio_dir.mkdir(parents=True, exist_ok=True)

    transcribe.warmup_whisper_background(config)

    if config.todoist_token:
        try:
            await asyncio.to_thread(
                todoist_cache.refresh_project_label_cache, engine, config
            )
        except Exception:
            logger.warning(
                "Cache Todoist inicial falhou — LLM usará lista vazia até refresh",
                exc_info=True,
            )

    cache_task = asyncio.create_task(_todoist_cache_refresh_loop(app))
    app.state.cache_refresh_task = cache_task

    await asyncio.to_thread(purge_expired, engine, config)
    purge_task = asyncio.create_task(_audio_purge_loop(app))
    app.state.audio_purge_task = purge_task

    worker_queue.start_worker(app)

    yield

    cache_task.cancel()
    purge_task.cancel()
    with contextlib.suppress(asyncio.CancelledError):
        await cache_task
        await purge_task
    await worker_queue.stop_worker(app)
    engine.dispose()


app = FastAPI(
    title="Taskhog Hub",
    version="0.1.0",
    lifespan=lifespan,
)

app.include_router(status.router)
app.include_router(recordings.router)
app.include_router(admin.router)
