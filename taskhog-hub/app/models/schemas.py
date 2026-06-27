from __future__ import annotations

from datetime import datetime
from typing import Literal

from pydantic import BaseModel, ConfigDict, Field

HUB_VERSION = "0.1.0"


class HealthResponse(BaseModel):
    ok: bool
    whisper: Literal["ready", "pending", "error"]
    todoist: Literal["ok", "pending", "error"]
    version: str = HUB_VERSION


class CacheRefreshResponse(BaseModel):
    projects: int
    labels: int
    refreshed_at: str


class LastTaskSummary(BaseModel):
    content: str
    project: str | None = None
    routed_to: Literal["project", "inbox"] | None = None


class StatusResponse(BaseModel):
    queue_pending: int
    processing: int
    processed_today: int
    errors: int
    last_task: LastTaskSummary | None = None
    server_time: str


class RecordingMetadata(BaseModel):
    client_job_id: str
    device_id: str
    rtc_timestamp: str
    rtc_valid: bool
    duration_s: float
    battery_pct: int | None = None
    fw_version: str | None = None


class RecordingAccepted(BaseModel):
    recording_id: str
    client_job_id: str
    status: str
    duplicate: bool = False


class TaskResult(BaseModel):
    todoist_id: str
    content: str
    project: str | None = None
    confidence: float | None = None
    routed_to: Literal["project", "inbox"] | None = None
    due: str | None = None
    priority: int | None = None
    labels: list[str] = Field(default_factory=list)


class StructuredTaskItem(BaseModel):
    """Uma tarefa na saída estruturada da LLM (Spec 03 §5.1)."""

    model_config = ConfigDict(extra="forbid")

    content: str = Field(min_length=1)
    project_suggestion: str | None
    project_confidence: float = Field(ge=0.0, le=1.0)
    due_string: str | None = None
    priority: Literal[1, 2, 3, 4] | None = None
    labels: list[str] = Field(default_factory=list)
    subtasks: list[str] = Field(default_factory=list)
    notes: str | None = None


class StructuredResult(BaseModel):
    """Saída completa da LLM após structuring (Spec 03 §5.1)."""

    model_config = ConfigDict(extra="forbid")

    language: str
    needs_review: bool
    tasks: list[StructuredTaskItem] = Field(min_length=1)


class RecordingDetail(BaseModel):
    recording_id: str
    status: str
    transcript: str | None = None
    tasks: list[TaskResult] = Field(default_factory=list)
    error: str | None = None


def iso_now() -> str:
    return datetime.now().astimezone().isoformat(timespec="seconds")
