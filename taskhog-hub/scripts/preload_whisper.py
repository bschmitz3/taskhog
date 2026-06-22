#!/usr/bin/env python3
"""Baixa e carrega o modelo Whisper (uso: docker compose run --rm taskhog-hub python scripts/preload_whisper.py)."""

from app.config import load_config
from app.services.transcribe import warmup_whisper


def main() -> None:
    config = load_config()
    warmup_whisper(config)
    print(f"Whisper {config.whisper_model} pronto ({config.whisper_device}/{config.whisper_compute_type})")


if __name__ == "__main__":
    main()
