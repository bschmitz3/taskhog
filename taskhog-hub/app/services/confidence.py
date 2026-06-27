"""Roteamento por confiança: projeto vs Inbox + revisar (Spec 02 §11, Spec 03 §7.2)."""

from __future__ import annotations

from dataclasses import dataclass
from difflib import SequenceMatcher
from typing import Literal

from app.models.schemas import StructuredTaskItem

_FUZZY_MIN_RATIO = 0.80
_FUZZY_PENALTY = 0.85


@dataclass(frozen=True)
class RouteDecision:
    project_id: str | None
    project_name: str | None
    routed_to: Literal["project", "inbox"]
    confidence: float
    add_review_label: bool


def _normalize(name: str) -> str:
    return name.strip().casefold()


def resolve_project(
    suggestion: str | None,
    projects: list[dict[str, str]],
) -> tuple[str | None, str | None, float]:
    """Resolve nome sugerido no cache. Retorna (ext_id, name, match_multiplier)."""
    if not suggestion or not suggestion.strip():
        return None, None, 0.0

    needle = _normalize(suggestion)
    for row in projects:
        if _normalize(row["name"]) == needle:
            return row["ext_id"], row["name"], 1.0

    best_row: dict[str, str] | None = None
    best_ratio = 0.0
    for row in projects:
        ratio = SequenceMatcher(None, needle, _normalize(row["name"])).ratio()
        if ratio > best_ratio:
            best_ratio = ratio
            best_row = row

    if best_row is not None and best_ratio >= _FUZZY_MIN_RATIO:
        return best_row["ext_id"], best_row["name"], best_ratio * _FUZZY_PENALTY

    return None, None, 0.0


def score_task(task: StructuredTaskItem, projects: list[dict[str, str]]) -> float:
    """Score final = project_confidence da LLM × qualidade do match no cache."""
    _ext_id, _name, match_mult = resolve_project(task.project_suggestion, projects)
    if match_mult == 0.0:
        return 0.0
    return task.project_confidence * match_mult


def route_task(
    task: StructuredTaskItem,
    *,
    global_needs_review: bool,
    projects: list[dict[str, str]],
    threshold: float,
) -> RouteDecision:
    ext_id, name, match_mult = resolve_project(task.project_suggestion, projects)
    confidence = task.project_confidence * match_mult if match_mult > 0 else 0.0

    if ext_id and confidence >= threshold and not global_needs_review:
        return RouteDecision(
            project_id=ext_id,
            project_name=name,
            routed_to="project",
            confidence=confidence,
            add_review_label=False,
        )

    return RouteDecision(
        project_id=None,
        project_name=None,
        routed_to="inbox",
        confidence=confidence,
        add_review_label=True,
    )


def merge_labels(
    task: StructuredTaskItem,
    route: RouteDecision,
    *,
    always_label: str,
    review_label: str,
    cached_label_names: list[str],
    global_needs_review: bool,
) -> list[str]:
    """Monta labels finais: temáticas da LLM + taskhog + revisar quando aplicável."""
    allowed = {_normalize(n): n for n in cached_label_names}
    labels: set[str] = set()
    for raw in task.labels:
        key = _normalize(raw)
        if key in allowed:
            labels.add(allowed[key])

    labels.add(always_label)
    if route.add_review_label or global_needs_review:
        labels.add(review_label)
    return sorted(labels)
