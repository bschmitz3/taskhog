"""Montagem do prompt de structuring e validação da saída JSON (Spec 03 §5–§6)."""

from __future__ import annotations

import json
import re
from datetime import datetime
from typing import Any

from pydantic import ValidationError

from app.models.schemas import StructuredResult

_JSON_FENCE_RE = re.compile(r"```(?:json)?\s*([\s\S]*?)\s*```", re.IGNORECASE)

_JSON_SCHEMA_TEXT = """{
  "type": "object",
  "additionalProperties": false,
  "required": ["language", "tasks", "needs_review"],
  "properties": {
    "language": { "type": "string" },
    "needs_review": { "type": "boolean" },
    "tasks": {
      "type": "array", "minItems": 1,
      "items": {
        "type": "object",
        "additionalProperties": false,
        "required": ["content", "project_suggestion", "project_confidence"],
        "properties": {
          "content": { "type": "string", "minLength": 1 },
          "project_suggestion": { "type": ["string", "null"] },
          "project_confidence": { "type": "number", "minimum": 0, "maximum": 1 },
          "due_string": { "type": ["string", "null"] },
          "priority": { "type": ["integer", "null"], "enum": [1, 2, 3, 4, null] },
          "labels": { "type": "array", "items": { "type": "string" } },
          "subtasks": { "type": "array", "items": { "type": "string" } },
          "notes": { "type": ["string", "null"] }
        }
      }
    }
  }
}"""

_FEW_SHOTS = """
# Exemplo 1 — tarefa simples, sem campos extras
Transcrição: "comprar pão"
{
  "language": "pt", "needs_review": false,
  "tasks": [{ "content": "Comprar pão", "project_suggestion": "Compras",
    "project_confidence": 0.85, "due_string": null, "priority": null,
    "labels": [], "subtasks": [], "notes": null }]
}

# Exemplo 2 — tarefa rica (trabalho → Magie)
Transcrição: "criar apresentação para o banco do brasil para quinta-feira, prioridade alta"
{
  "language": "pt", "needs_review": false,
  "tasks": [{ "content": "Criar apresentação para o Banco do Brasil",
    "project_suggestion": "Magie", "project_confidence": 0.9,
    "due_string": "quinta-feira", "priority": 4,
    "labels": [], "subtasks": [], "notes": null }]
}

# Exemplo 3 — múltiplas ações
Transcrição: "comprar pão e ligar pro contador amanhã"
{
  "language": "pt", "needs_review": false,
  "tasks": [
    { "content": "Comprar pão", "project_suggestion": "Compras", "project_confidence": 0.85,
      "due_string": null, "priority": null, "labels": [], "subtasks": [], "notes": null },
    { "content": "Ligar para o contador", "project_suggestion": "Personal", "project_confidence": 0.75,
      "due_string": "amanhã", "priority": null, "labels": [], "subtasks": [], "notes": null }
  ]
}

# Exemplo 4 — ambíguo
Transcrição: "aquela coisa de antes"
{ "language": "pt", "needs_review": true,
  "tasks": [{ "content": "aquela coisa de antes", "project_suggestion": null,
    "project_confidence": 0.1, "due_string": null, "priority": null,
    "labels": [], "subtasks": [], "notes": "transcrição ambígua" }] }
""".strip()

_PROJECT_ROUTING_HINTS = """
DICAS DE ROTEAMENTO (use só nomes exatos da lista PROJETOS acima):
- Compras: supermercado, mercado, farmácia, itens de casa
- Personal: contador, médico, família, finanças pessoais
- Magie: trabalho, clientes, apresentações, reuniões profissionais
- MagieLive: lives, transmissões, conteúdo ao vivo
Não use "Inbox" em project_suggestion — se não souber o projeto, use null.
""".strip()

_CORRECTION_USER = (
    "Sua resposta anterior não é JSON válido conforme o schema. "
    "Responda SOMENTE com um objeto JSON válido, sem markdown, sem comentários, "
    "sem texto extra."
)


def json_schema_text() -> str:
    return _JSON_SCHEMA_TEXT


def build_system_prompt(
    *,
    now: datetime,
    projects: list[str],
    labels: list[str],
) -> str:
    now_iso = now.astimezone().isoformat(timespec="seconds")
    projects_list = ", ".join(projects) if projects else "(nenhum)"
    labels_list = ", ".join(labels) if labels else "(nenhuma)"

    return f"""Você é o motor de estruturação do Taskhog. Recebe a transcrição de uma nota de voz
em português e a converte em uma ou mais tarefas para o Todoist.

REGRAS:
1. Responda SOMENTE com um objeto JSON válido, sem markdown, sem comentários, sem texto extra.
2. O JSON deve seguir exatamente o schema fornecido.
3. Extraia apenas os campos que a fala implica. Não invente prazo, prioridade, label
   ou subtarefa que não estejam sugeridos na fala. Campos ausentes = null ou lista vazia.
4. project_suggestion DEVE ser exatamente um dos nomes da lista PROJETOS (copie
   caractere por caractere, inclusive maiúsculas). Nunca invente nomes nem use sinônimos
   (ex.: "Trabalho", "Pessoal" — use "Magie", "Personal"). Não use "Inbox".
5. project_confidence (0..1) reflete o quão certo você está do roteamento. Se a fala não
   indica claramente o projeto, use confiança baixa (ex.: 0.3) e/ou null.
6. priority usa a escala da API do Todoist: 4 = mais alta, 3 = alta, 2 = média,
   1 = normal/sem prioridade. "prioridade alta/urgente" -> 4. Sem menção -> null.
7. due_string em português natural ("amanhã", "quinta-feira", "amanhã 9h"). Sem menção -> null.
8. Se a fala contém várias ações independentes, gere várias tasks.
9. labels: apenas etiquetas temáticas sugeridas pela fala (sem "taskhog"/"revisar").
10. needs_review = true se a intenção for ambígua ou a transcrição parecer incompleta.

DATA/HORA ATUAL: {now_iso} (use para resolver datas relativas).
PROJETOS DISPONÍVEIS: {projects_list}
LABELS DISPONÍVEIS: {labels_list}

{_PROJECT_ROUTING_HINTS}

SCHEMA:
{_JSON_SCHEMA_TEXT}

EXEMPLOS:
{_FEW_SHOTS}"""


def build_user_prompt(transcript: str) -> str:
    return f'Transcrição: "{transcript}"'


def build_messages(
    transcript: str,
    *,
    now: datetime,
    projects: list[str],
    labels: list[str],
) -> list[dict[str, str]]:
    return [
        {"role": "system", "content": build_system_prompt(now=now, projects=projects, labels=labels)},
        {"role": "user", "content": build_user_prompt(transcript)},
    ]


def correction_message() -> dict[str, str]:
    return {"role": "user", "content": _CORRECTION_USER}


def extract_json_object(raw: str, *, strict: bool) -> str:
    """Extrai o objeto JSON da resposta da LLM."""
    text = raw.strip()
    if not text:
        raise ValueError("resposta vazia")

    fence = _JSON_FENCE_RE.search(text)
    if fence:
        if strict:
            raise ValueError("resposta contém markdown; json_strict exige objeto puro")
        text = fence.group(1).strip()

    if strict and not text.startswith("{"):
        raise ValueError("json_strict: resposta não começa com '{'")

    # Encontra o primeiro objeto JSON balanceado.
    start = text.find("{")
    if start < 0:
        raise ValueError("nenhum objeto JSON encontrado")

    depth = 0
    in_string = False
    escape = False
    for idx in range(start, len(text)):
        ch = text[idx]
        if in_string:
            if escape:
                escape = False
            elif ch == "\\":
                escape = True
            elif ch == '"':
                in_string = False
            continue
        if ch == '"':
            in_string = True
        elif ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                return text[start : idx + 1]

    raise ValueError("objeto JSON incompleto")


def parse_structured_json(raw: str, *, strict: bool) -> StructuredResult:
    """Valida a saída da LLM contra o schema Pydantic (Spec 03 §5.1)."""
    json_text = extract_json_object(raw, strict=strict)
    try:
        data: Any = json.loads(json_text)
    except json.JSONDecodeError as exc:
        raise ValueError(f"JSON inválido: {exc}") from exc
    try:
        return StructuredResult.model_validate(data)
    except ValidationError as exc:
        raise ValueError(f"schema inválido: {exc}") from exc
