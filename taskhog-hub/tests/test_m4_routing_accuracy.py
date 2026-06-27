"""Testes M4-T8: acurácia de roteamento no conjunto de frases reais."""

from __future__ import annotations

import os

import pytest

from app.config import load_config
from app.services.llm.cloud import CloudLLMProvider
from app.services.routing_eval import (
    FIXTURE_PATH,
    load_routing_cases,
    run_live_report,
    run_offline_report,
)


def test_routing_fixture_loads_cases():
    cases = load_routing_cases()
    assert len(cases) == 31
    assert FIXTURE_PATH.is_file()


def test_routing_fixture_all_have_structured_for_offline():
    cases = load_routing_cases()
    missing = [c.id for c in cases if c.structured is None]
    assert missing == [], f"casos sem structured: {missing}"


def test_routing_offline_accuracy_meets_m4_gate():
    """Sanidade: saídas LLM ideais + confidence roteiam conforme esperado (100%)."""
    report = run_offline_report()
    assert report.total == 31
    assert report.accuracy == 1.0
    assert report.passed_gate


@pytest.mark.live
def test_routing_live_accuracy_meets_m4_gate():
    """Chama OpenAI real — requer LLM_API_KEY e HUB_CONFIG. Rodar: pytest -m live."""
    if not os.environ.get("LLM_API_KEY"):
        pytest.skip("LLM_API_KEY não definido")

    config = load_config()
    llm = CloudLLMProvider(config)
    report = run_live_report(llm)

    failures = [r for r in report.results if not r.correct]
    if failures:
        lines = [f"  {r.case_id}: esperado {r.expected_project or 'Inbox'}, got {r.actual_project or r.actual_routed_to}" for r in failures]
        pytest.fail(
            f"Acurácia live {report.accuracy:.1%} ({report.correct}/{report.total})\n"
            + "\n".join(lines)
        )

    assert report.passed_gate
