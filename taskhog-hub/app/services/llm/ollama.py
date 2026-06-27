"""Provedor Ollama local — stub até M4-T7."""

from __future__ import annotations

from datetime import datetime
from typing import TYPE_CHECKING

from app.models.schemas import StructuredResult
from app.services.llm.base import LLMError

if TYPE_CHECKING:
    from app.config import HubConfig


class OllamaLLMProvider:
    def __init__(self, config: HubConfig) -> None:
        self._config = config

    def structure(
        self,
        transcript: str,
        *,
        now: datetime,
        projects: list[str],
        labels: list[str],
    ) -> StructuredResult:
        raise LLMError("Ollama provider não implementado — milestone M4-T7")
