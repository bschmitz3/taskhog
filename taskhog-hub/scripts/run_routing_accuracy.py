#!/usr/bin/env python3
"""Roda avaliação de roteamento M4-T8 contra OpenAI real.

Uso:
  cd taskhog-hub
  export HUB_CONFIG=hub.yaml   # ou path no servidor
  export LLM_API_KEY=sk-...
  python scripts/run_routing_accuracy.py
  python scripts/run_routing_accuracy.py --offline   # só fixtures, sem API
"""

from __future__ import annotations

import argparse
import os
import sys

# Permite executar como script sem instalar o pacote.
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from app.config import load_config
from app.services.llm.cloud import CloudLLMProvider
from app.services.routing_eval import format_report, run_live_report, run_offline_report


def main() -> int:
    parser = argparse.ArgumentParser(description="Avalia acurácia de roteamento M4-T8")
    parser.add_argument(
        "--offline",
        action="store_true",
        help="Avalia só fixtures structured (sem chamar OpenAI)",
    )
    args = parser.parse_args()

    if args.offline:
        report = run_offline_report()
    else:
        if not os.environ.get("LLM_API_KEY"):
            print("ERRO: defina LLM_API_KEY para modo live", file=sys.stderr)
            return 1
        config = load_config()
        llm = CloudLLMProvider(config)
        print("Chamando OpenAI para 31 frases (pode levar ~1 min)...\n")
        report = run_live_report(llm)

    print(format_report(report))
    return 0 if report.passed_gate else 1


if __name__ == "__main__":
    raise SystemExit(main())
