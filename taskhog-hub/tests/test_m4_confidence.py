"""Testes M4-T5: roteamento por confiança."""

from __future__ import annotations

from app.models.schemas import StructuredTaskItem
from app.services.confidence import merge_labels, resolve_project, route_task, score_task

PROJECTS = [
    {"ext_id": "p1", "name": "Compras"},
    {"ext_id": "p2", "name": "Personal"},
]


def test_resolve_project_exact_match():
    ext_id, name, mult = resolve_project("Compras", PROJECTS)
    assert ext_id == "p1"
    assert name == "Compras"
    assert mult == 1.0


def test_resolve_project_case_insensitive():
    ext_id, _name, mult = resolve_project("compras", PROJECTS)
    assert ext_id == "p1"
    assert mult == 1.0


def test_resolve_project_fuzzy():
    ext_id, name, mult = resolve_project("Compra", PROJECTS)
    assert ext_id == "p1"
    assert name == "Compras"
    assert 0 < mult < 1.0


def test_resolve_project_no_match():
    ext_id, name, mult = resolve_project("Trabalho", PROJECTS)
    assert ext_id is None
    assert name is None
    assert mult == 0.0


def test_route_high_confidence_to_project():
    task = StructuredTaskItem(
        content="Comprar pão",
        project_suggestion="Compras",
        project_confidence=0.9,
    )
    route = route_task(
        task,
        global_needs_review=False,
        projects=PROJECTS,
        threshold=0.75,
    )
    assert route.routed_to == "project"
    assert route.project_id == "p1"
    assert route.add_review_label is False


def test_route_low_confidence_to_inbox():
    task = StructuredTaskItem(
        content="Comprar pão",
        project_suggestion="Compras",
        project_confidence=0.6,
    )
    route = route_task(
        task,
        global_needs_review=False,
        projects=PROJECTS,
        threshold=0.65,
    )
    assert route.routed_to == "inbox"
    assert route.project_id is None
    assert route.add_review_label is True


def test_route_moderate_confidence_to_project_at_065():
    task = StructuredTaskItem(
        content="Comprar pão",
        project_suggestion="Compras",
        project_confidence=0.7,
    )
    route = route_task(
        task,
        global_needs_review=False,
        projects=PROJECTS,
        threshold=0.65,
    )
    assert route.routed_to == "project"
    assert route.project_id == "p1"


def test_route_global_needs_review_forces_inbox():
    task = StructuredTaskItem(
        content="aquela coisa",
        project_suggestion="Compras",
        project_confidence=0.95,
    )
    route = route_task(
        task,
        global_needs_review=True,
        projects=PROJECTS,
        threshold=0.65,
    )
    assert route.routed_to == "inbox"
    assert route.add_review_label is True


def test_score_task_exact_match():
    task = StructuredTaskItem(
        content="x",
        project_suggestion="Compras",
        project_confidence=0.8,
    )
    assert score_task(task, PROJECTS) == 0.8


def test_merge_labels_adds_taskhog_and_revisar():
    task = StructuredTaskItem(
        content="x",
        project_suggestion=None,
        project_confidence=0.1,
        labels=["casa"],
    )
    route = route_task(
        task,
        global_needs_review=False,
        projects=PROJECTS,
        threshold=0.75,
    )
    labels = merge_labels(
        task,
        route,
        always_label="taskhog",
        review_label="revisar",
        cached_label_names=["casa", "urgente"],
        global_needs_review=False,
    )
    assert labels == ["casa", "revisar", "taskhog"]
