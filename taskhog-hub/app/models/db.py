from __future__ import annotations

from pathlib import Path

from sqlalchemy import create_engine, text
from sqlalchemy.engine import Engine

SCHEMA_SQL = """
CREATE TABLE IF NOT EXISTS jobs (
  recording_id   TEXT PRIMARY KEY,
  device_id        TEXT NOT NULL,
  client_job_id    TEXT NOT NULL,
  wav_path         TEXT NOT NULL,
  rtc_timestamp    TEXT,
  rtc_valid        INTEGER DEFAULT 1,
  received_at      TEXT NOT NULL,
  duration_s       REAL,
  status           TEXT NOT NULL,
  transcript       TEXT,
  llm_json         TEXT,
  error            TEXT,
  attempts         INTEGER DEFAULT 0,
  created_tasks    TEXT,
  UNIQUE(device_id, client_job_id)
);

CREATE TABLE IF NOT EXISTS todoist_cache (
  kind   TEXT NOT NULL,
  ext_id TEXT NOT NULL,
  name   TEXT NOT NULL,
  PRIMARY KEY (kind, ext_id)
);

CREATE INDEX IF NOT EXISTS idx_jobs_status ON jobs(status);
CREATE INDEX IF NOT EXISTS idx_jobs_received ON jobs(received_at);
"""


def create_db_engine(db_path: str) -> Engine:
    path = Path(db_path)
    path.parent.mkdir(parents=True, exist_ok=True)
    engine = create_engine(
        f"sqlite:///{path}",
        connect_args={"check_same_thread": False},
    )
    return engine


def init_db(engine: Engine) -> None:
    with engine.begin() as conn:
        conn.execute(text("PRAGMA journal_mode=WAL"))
        for statement in SCHEMA_SQL.strip().split(";"):
            stmt = statement.strip()
            if stmt:
                conn.execute(text(stmt))


def count_jobs_by_status(engine: Engine, status: str) -> int:
    with engine.connect() as conn:
        row = conn.execute(
            text("SELECT COUNT(*) AS n FROM jobs WHERE status = :status"),
            {"status": status},
        ).one()
        return int(row.n)
