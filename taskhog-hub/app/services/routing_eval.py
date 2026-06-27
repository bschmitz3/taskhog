"""Avaliação de acurácia de roteamento (M4-T8)."""

from __future__ import annotations

import json
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from app.models.schemas import StructuredResult
from app.services.confidence import RouteDecision, route_task

FIXTURE_PATH = Path(__file__).resolve().parents[1] / "fixtures" / "routing_phrases.json"

DEFAULT_PROJECTS: list[dict[str, str]] = [
    {"ext_id": "6fmQVwv3P3V2m32g", "name": "Compras"},
    {"ext_id": "6gq7xH5JgFwPHCVv", "name": "Magie"},
    {"ext_id": "6gq7xHvV6F48XH8G", "name": "Personal"},
    {"ext_id": "6gv46H6wfP77q6Fv", "name": "MagieLive"},
]

DEFAULT_THRESHOLD = 0.65


@dataclass(frozen=True)
class RoutingCase:
    id: str
    transcript: str
    expected_project: str | None
    expected_routed_to: str
    structured: dict[str, Any] | None = None
    expected_tasks: list[dict[str, str | None]] | None = None
    notes: str | None = None


@dataclass(frozen=True)
class RoutingEvalResult:
    case_id: str
    transcript: str
    expected_project: str | None
    expected_routed_to: str
    actual_project: str | None
    actual_routed_to: str
    confidence: float
    correct: bool
    detail: str | None = None


@dataclass(frozen=True)
class RoutingReport:
    total: int
    correct: int
    accuracy: float
    results: list[RoutingEvalResult]

    @property
    def passed_gate(self) -> bool:
        return self.accuracy >= 0.80


def load_routing_cases(path: Path | None = None) -> list[RoutingCase]:
    data = json.loads((path or FIXTURE_PATH).read_text(encoding="utf-8"))
    cases: list[RoutingCase] = []
    for raw in data["cases"]:
        cases.append(
            RoutingCase(
                id=raw["id"],
                transcript=raw["transcript"],
                expected_project=raw.get("expected_project"),
                expected_routed_to=raw["expected_routed_to"],
                structured=raw.get("structured"),
                expected_tasks=raw.get("expected_tasks"),
                notes=raw.get("notes"),
            )
        )
    return cases


def route_structured(
    structured: StructuredResult | dict[str, Any],
    *,
    projects: list[dict[str, str]] | None = None,
    threshold: float = DEFAULT_THRESHOLD,
) -> list[RouteDecision]:
    if isinstance(structured, dict):
        structured = StructuredResult.model_validate(structured)
    proj = projects or DEFAULT_PROJECTS
    return [
        route_task(
            task,
            global_needs_review=structured.needs_review,
            projects=proj,
            threshold=threshold,
        )
        for task in structured.tasks
    ]


def _match_route(
    expected_project: str | None,
    expected_routed_to: str,
    route: RouteDecision,
) -> bool:
    if route.routed_to != expected_routed_to:
        return False
    if expected_routed_to == "inbox":
        return True
    if expected_project is None:
        return route.project_name is not None
    if route.project_name is None:
        return False
    return route.project_name.casefold() == expected_project.casefold()


def evaluate_case_routes(
    case: RoutingCase,
    routes: list[RouteDecision],
    *,
    threshold: float = DEFAULT_THRESHOLD,
) -> RoutingEvalResult:
    if case.expected_tasks:
        for idx, exp in enumerate(case.expected_tasks):
            if idx >= len(routes):
                return RoutingEvalResult(
                    case_id=case.id,
                    transcript=case.transcript,
                    expected_project=exp.get("project"),  # type: ignore[arg-type]
                    expected_routed_to=str(exp["routed_to"]),
                    actual_project=None,
                    actual_routed_to="missing",
                    confidence=0.0,
                    correct=False,
                    detail=f"tarefa {idx + 1} ausente na saída",
                )
            route = routes[idx]
            if not _match_route(
                exp.get("project"),  # type: ignore[arg-type]
                str(exp["routed_to"]),
                route,
            ):
                return RoutingEvalResult(
                    case_id=case.id,
                    transcript=case.transcript,
                    expected_project=exp.get("project"),  # type: ignore[arg-type]
                    expected_routed_to=str(exp["routed_to"]),
                    actual_project=route.project_name,
                    actual_routed_to=route.routed_to,
                    confidence=route.confidence,
                    correct=False,
                    detail=f"tarefa {idx + 1} incorreta",
                )
        first = routes[0]
        return RoutingEvalResult(
            case_id=case.id,
            transcript=case.transcript,
            expected_project=case.expected_project,
            expected_routed_to=case.expected_routed_to,
            actual_project=first.project_name,
            actual_routed_to=first.routed_to,
            confidence=first.confidence,
            correct=True,
        )

    route = routes[0] if routes else None
    if route is None:
        return RoutingEvalResult(
            case_id=case.id,
            transcript=case.transcript,
            expected_project=case.expected_project,
            expected_routed_to=case.expected_routed_to,
            actual_project=None,
            actual_routed_to="missing",
            confidence=0.0,
            correct=False,
            detail="nenhuma tarefa roteada",
        )

    correct = _match_route(
        case.expected_project, case.expected_routed_to, route
    )
    return RoutingEvalResult(
        case_id=case.id,
        transcript=case.transcript,
        expected_project=case.expected_project,
        expected_routed_to=case.expected_routed_to,
        actual_project=route.project_name,
        actual_routed_to=route.routed_to,
        confidence=route.confidence,
        correct=correct,
        detail=None if correct else "roteamento não bateu",
    )


def evaluate_offline_case(
    case: RoutingCase,
    *,
    projects: list[dict[str, str]] | None = None,
    threshold: float = DEFAULT_THRESHOLD,
) -> RoutingEvalResult:
    if case.structured is None:
        raise ValueError(f"caso {case.id} sem structured para avaliação offline")
    routes = route_structured(
        case.structured, projects=projects, threshold=threshold
    )
    return evaluate_case_routes(case, routes, threshold=threshold)


def evaluate_live_case(
    case: RoutingCase,
    llm: Any,
    *,
    projects: list[dict[str, str]] | None = None,
    labels: list[str] | None = None,
    threshold: float = DEFAULT_THRESHOLD,
    now: datetime | None = None,
) -> RoutingEvalResult:
    proj = projects or DEFAULT_PROJECTS
    project_names = [p["name"] for p in proj]
    structured = llm.structure(
        case.transcript,
        now=now or datetime(2026, 6, 27, 12, 0, tzinfo=timezone.utc),
        projects=project_names,
        labels=labels or [],
    )
    routes = route_structured(structured, projects=proj, threshold=threshold)
    return evaluate_case_routes(case, routes, threshold=threshold)


def run_offline_report(
    cases: list[RoutingCase] | None = None,
    *,
    threshold: float = DEFAULT_THRESHOLD,
) -> RoutingReport:
    items = cases or load_routing_cases()
    results = [
        evaluate_offline_case(case, threshold=threshold)
        for case in items
        if case.structured is not None
    ]
    correct = sum(1 for r in results if r.correct)
    total = len(results)
    return RoutingReport(
        total=total,
        correct=correct,
        accuracy=correct / total if total else 0.0,
        results=results,
    )


def run_live_report(
    llm: Any,
    cases: list[RoutingCase] | None = None,
    *,
    threshold: float = DEFAULT_THRESHOLD,
) -> RoutingReport:
    items = cases or load_routing_cases()
    results = [
        evaluate_live_case(case, llm, threshold=threshold) for case in items
    ]
    correct = sum(1 for r in results if r.correct)
    total = len(results)
    return RoutingReport(
        total=total,
        correct=correct,
        accuracy=correct / total if total else 0.0,
        results=results,
    )


def format_report(report: RoutingReport) -> str:
    lines = [
        f"Acurácia: {report.correct}/{report.total} ({report.accuracy:.1%})",
        f"Gate M4 (≥80%): {'PASS' if report.passed_gate else 'FAIL'}",
        "",
    ]
    for r in report.results:
        mark = "✓" if r.correct else "✗"
        exp = r.expected_project or "Inbox"
        act = r.actual_project or r.actual_routed_to
        lines.append(
            f"  {mark} {r.case_id}: {exp} → {act} (conf={r.confidence:.2f})"
        )
        if r.detail and not r.correct:
            lines.append(f"      {r.detail}")
    return "\n".join(lines)
