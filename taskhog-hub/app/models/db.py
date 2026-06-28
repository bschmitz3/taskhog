from __future__ import annotations

import json
import secrets
from datetime import datetime
from pathlib import Path
from typing import Any

from sqlalchemy import create_engine, text
from sqlalchemy.engine import Engine

# Estados não-terminais que devem ser reprocessados após restart do Hub.
_IN_FLIGHT = ("transcribing", "structuring", "creating")

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


# --- Repositório de jobs (M2) ----------------------------------------------

_JOB_COLUMNS = (
    "recording_id, device_id, client_job_id, wav_path, rtc_timestamp, "
    "rtc_valid, received_at, duration_s, status, transcript, llm_json, "
    "error, attempts, created_tasks"
)


def now_iso() -> str:
    return datetime.now().astimezone().isoformat(timespec="seconds")


def new_recording_id() -> str:
    return "rec_" + secrets.token_hex(4)


def _row_to_dict(row: Any) -> dict[str, Any]:
    return dict(row._mapping)


def get_job(engine: Engine, recording_id: str) -> dict[str, Any] | None:
    with engine.connect() as conn:
        row = conn.execute(
            text(f"SELECT {_JOB_COLUMNS} FROM jobs WHERE recording_id = :rid"),
            {"rid": recording_id},
        ).first()
    return _row_to_dict(row) if row else None


def get_job_by_client(
    engine: Engine, device_id: str, client_job_id: str
) -> dict[str, Any] | None:
    with engine.connect() as conn:
        row = conn.execute(
            text(
                f"SELECT {_JOB_COLUMNS} FROM jobs "
                "WHERE device_id = :did AND client_job_id = :cid"
            ),
            {"did": device_id, "cid": client_job_id},
        ).first()
    return _row_to_dict(row) if row else None


def insert_job(
    engine: Engine,
    *,
    recording_id: str,
    device_id: str,
    client_job_id: str,
    wav_path: str,
    rtc_timestamp: str | None,
    rtc_valid: bool,
    duration_s: float | None,
    received_at: str,
    status: str = "queued",
) -> None:
    with engine.begin() as conn:
        conn.execute(
            text(
                "INSERT INTO jobs (recording_id, device_id, client_job_id, "
                "wav_path, rtc_timestamp, rtc_valid, received_at, duration_s, "
                "status, attempts) VALUES (:rid, :did, :cid, :wav, :ts, :valid, "
                ":recv, :dur, :status, 0)"
            ),
            {
                "rid": recording_id,
                "did": device_id,
                "cid": client_job_id,
                "wav": wav_path,
                "ts": rtc_timestamp,
                "valid": 1 if rtc_valid else 0,
                "recv": received_at,
                "dur": duration_s,
                "status": status,
            },
        )


def claim_next_queued(engine: Engine) -> dict[str, Any] | None:
    """Pega o job `queued` mais antigo e marca `transcribing` atomicamente."""
    with engine.begin() as conn:
        row = conn.execute(
            text(
                f"SELECT {_JOB_COLUMNS} FROM jobs WHERE status = 'queued' "
                "ORDER BY received_at, recording_id LIMIT 1"
            )
        ).first()
        if row is None:
            return None
        job = _row_to_dict(row)
        conn.execute(
            text(
                "UPDATE jobs SET status = 'transcribing' "
                "WHERE recording_id = :rid"
            ),
            {"rid": job["recording_id"]},
        )
        job["status"] = "transcribing"
        return job


def set_status(
    engine: Engine, recording_id: str, status: str, *, error: str | None = None
) -> None:
    with engine.begin() as conn:
        conn.execute(
            text(
                "UPDATE jobs SET status = :status, error = :error "
                "WHERE recording_id = :rid"
            ),
            {"status": status, "error": error, "rid": recording_id},
        )


def set_transcript(engine: Engine, recording_id: str, transcript: str) -> None:
    with engine.begin() as conn:
        conn.execute(
            text(
                "UPDATE jobs SET transcript = :t WHERE recording_id = :rid"
            ),
            {"t": transcript, "rid": recording_id},
        )


def set_llm_json(engine: Engine, recording_id: str, llm_json: str) -> None:
    with engine.begin() as conn:
        conn.execute(
            text(
                "UPDATE jobs SET llm_json = :j WHERE recording_id = :rid"
            ),
            {"j": llm_json, "rid": recording_id},
        )


def set_done(
    engine: Engine, recording_id: str, created_tasks: list[dict[str, Any]]
) -> None:
    with engine.begin() as conn:
        conn.execute(
            text(
                "UPDATE jobs SET status = 'done', error = NULL, "
                "created_tasks = :ct WHERE recording_id = :rid"
            ),
            {"ct": json.dumps(created_tasks, ensure_ascii=False), "rid": recording_id},
        )


def load_created_tasks(job: dict[str, Any]) -> list[dict[str, Any]]:
    raw = job.get("created_tasks")
    if not raw:
        return []
    try:
        tasks = json.loads(raw)
    except (json.JSONDecodeError, TypeError):
        return []
    return tasks if isinstance(tasks, list) else []


def append_created_task(
    engine: Engine, recording_id: str, task: dict[str, Any]
) -> list[dict[str, Any]]:
    """Persiste uma tarefa criada no Todoist (M5-T6 — resume após restart)."""
    with engine.begin() as conn:
        row = conn.execute(
            text("SELECT created_tasks FROM jobs WHERE recording_id = :rid"),
            {"rid": recording_id},
        ).one()
        created = load_created_tasks({"created_tasks": row.created_tasks})
        created.append(task)
        conn.execute(
            text("UPDATE jobs SET created_tasks = :ct WHERE recording_id = :rid"),
            {
                "ct": json.dumps(created, ensure_ascii=False),
                "rid": recording_id,
            },
        )
    return created


def increment_attempts(engine: Engine, recording_id: str) -> None:
    with engine.begin() as conn:
        conn.execute(
            text(
                "UPDATE jobs SET attempts = attempts + 1 WHERE recording_id = :rid"
            ),
            {"rid": recording_id},
        )


def reset_stuck_jobs(engine: Engine) -> int:
    """Reverte jobs interrompidos (in-flight) para `queued` no boot."""
    placeholders = ", ".join(f"'{s}'" for s in _IN_FLIGHT)
    with engine.begin() as conn:
        result = conn.execute(
            text(f"UPDATE jobs SET status = 'queued' WHERE status IN ({placeholders})")
        )
        return result.rowcount or 0


def count_processed_today(engine: Engine) -> int:
    today = datetime.now().astimezone().strftime("%Y-%m-%d")
    with engine.connect() as conn:
        row = conn.execute(
            text(
                "SELECT COUNT(*) AS n FROM jobs WHERE status = 'done' "
                "AND substr(received_at, 1, 10) = :today"
            ),
            {"today": today},
        ).one()
        return int(row.n)


def get_last_task(engine: Engine) -> dict[str, Any] | None:
    with engine.connect() as conn:
        row = conn.execute(
            text(
                "SELECT created_tasks FROM jobs WHERE status = 'done' "
                "AND created_tasks IS NOT NULL "
                "ORDER BY received_at DESC, recording_id DESC LIMIT 1"
            )
        ).first()
    if row is None or not row.created_tasks:
        return None
    try:
        tasks = json.loads(row.created_tasks)
    except (json.JSONDecodeError, TypeError):
        return None
    if not tasks:
        return None
    first = tasks[0]
    return {
        "content": first.get("content", ""),
        "project": first.get("project"),
        "routed_to": first.get("routed_to"),
    }


# --- Cache Todoist (M4-T1) ---------------------------------------------------

def replace_todoist_cache(
    engine: Engine, kind: str, items: list[tuple[str, str]]
) -> None:
    """Substitui todas as entradas de um kind ('project' | 'label')."""
    with engine.begin() as conn:
        conn.execute(
            text("DELETE FROM todoist_cache WHERE kind = :kind"),
            {"kind": kind},
        )
        if items:
            conn.execute(
                text(
                    "INSERT INTO todoist_cache (kind, ext_id, name) "
                    "VALUES (:kind, :ext_id, :name)"
                ),
                [
                    {"kind": kind, "ext_id": ext_id, "name": name}
                    for ext_id, name in items
                ],
            )


def list_todoist_cache(engine: Engine, kind: str) -> list[dict[str, str]]:
    with engine.connect() as conn:
        rows = conn.execute(
            text(
                "SELECT ext_id, name FROM todoist_cache "
                "WHERE kind = :kind ORDER BY name COLLATE NOCASE"
            ),
            {"kind": kind},
        ).all()
    return [{"ext_id": str(row.ext_id), "name": row.name} for row in rows]


def count_todoist_cache(engine: Engine, kind: str) -> int:
    with engine.connect() as conn:
        row = conn.execute(
            text("SELECT COUNT(*) AS n FROM todoist_cache WHERE kind = :kind"),
            {"kind": kind},
        ).one()
        return int(row.n)
