from __future__ import annotations

from datetime import datetime
from typing import Literal

from pydantic import BaseModel, Field

HUB_VERSION = "0.1.0"


class HealthResponse(BaseModel):
    ok: bool
    whisper: Literal["ready", "pending", "error"]
    todoist: Literal["ok", "pending", "error"]
    version: str = HUB_VERSION


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


class RecordingDetail(BaseModel):
    recording_id: str
    status: str
    transcript: str | None = None
    tasks: list[TaskResult] = Field(default_factory=list)
    error: str | None = None


def iso_now() -> str:
    return datetime.now().astimezone().isoformat(timespec="seconds")
