"""LLMProvider — interface plugável (Spec 02 §9)."""

from __future__ import annotations

from datetime import datetime
from typing import TYPE_CHECKING, Protocol, runtime_checkable

from app.models.schemas import StructuredResult

if TYPE_CHECKING:
    from app.config import HubConfig


class LLMError(Exception):
    """Falha irrecuperável na chamada ou parsing da LLM."""


@runtime_checkable
class LLMProvider(Protocol):
    def structure(
        self,
        transcript: str,
        *,
        now: datetime,
        projects: list[str],
        labels: list[str],
    ) -> StructuredResult:
        """Converte transcrição em tarefas estruturadas."""
        ...


def get_llm_provider(config: HubConfig) -> LLMProvider:
    provider = config.llm_provider
    if provider == "cloud":
        from app.services.llm.cloud import CloudLLMProvider

        return CloudLLMProvider(config)
    if provider == "ollama":
        from app.services.llm.ollama import OllamaLLMProvider

        return OllamaLLMProvider(config)
    raise LLMError(f"provedor LLM desconhecido: {provider!r}")
