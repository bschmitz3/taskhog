"""Provedor cloud — endpoint OpenAI-compatible com JSON mode (Spec 02 §9.2)."""

from __future__ import annotations

import logging
from datetime import datetime
from typing import TYPE_CHECKING, Any

import httpx

from app.models.schemas import StructuredResult
from app.services.llm.base import LLMError
from app.services.structuring import (
    build_messages,
    correction_message,
    parse_structured_json,
)

if TYPE_CHECKING:
    from app.config import HubConfig

logger = logging.getLogger(__name__)


class CloudLLMProvider:
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
        messages = build_messages(
            transcript,
            now=now,
            projects=projects,
            labels=labels,
        )
        attempts = self._config.llm_max_retries + 1
        last_error: Exception | None = None

        for attempt in range(attempts):
            if attempt > 0:
                messages = [*messages, correction_message()]
            raw = self._chat_completion(messages)
            try:
                return parse_structured_json(raw, strict=self._config.llm_json_strict)
            except ValueError as exc:
                last_error = exc
                logger.warning(
                    "LLM JSON inválido (tentativa %d/%d): %s",
                    attempt + 1,
                    attempts,
                    exc,
                )

        raise LLMError(
            f"LLM não retornou JSON válido após {attempts} tentativa(s)"
        ) from last_error

    def _chat_completion(self, messages: list[dict[str, str]]) -> str:
        url = f"{self._config.llm_endpoint.rstrip('/')}/chat/completions"
        payload: dict[str, Any] = {
            "model": self._config.llm_model,
            "messages": messages,
            "response_format": {"type": "json_object"},
        }
        headers = {
            "Authorization": f"Bearer {self._config.llm_api_key}",
            "Content-Type": "application/json",
        }
        try:
            resp = httpx.post(url, json=payload, headers=headers, timeout=90.0)
            resp.raise_for_status()
        except httpx.HTTPError as exc:
            raise LLMError(f"erro HTTP na LLM: {exc}") from exc

        try:
            body = resp.json()
            return body["choices"][0]["message"]["content"]
        except (KeyError, IndexError, TypeError, ValueError) as exc:
            raise LLMError(f"resposta inesperada da LLM: {resp.text[:500]!r}") from exc
