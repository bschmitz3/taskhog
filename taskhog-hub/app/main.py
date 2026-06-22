from __future__ import annotations

from contextlib import asynccontextmanager
from pathlib import Path

from fastapi import FastAPI

from app.api import admin, recordings, status
from app.config import load_config
from app.deps import bind_tokens
from app.models.db import create_db_engine, init_db
from app.services import transcribe
from app.worker import queue as worker_queue


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

    worker_queue.start_worker(app)

    yield

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
