"""The LLM boundary: hypothesis selection and explanation, nothing else.

Evidence collection stays entirely deterministic (see agent.py) -- this
module's only job is, given already-gathered structured evidence, to decide
which candidate hypothesis best explains it and write the natural-language
explanation. The model never sees a live system, never calls a tool, and
never gets to invent a hypothesis outside the fixed set it's handed; its
only affordance is picking one of the given IDs and writing prose about it.
Output is constrained to JSON validated against LlmDiagnosisResult below, so
a malformed or off-script response fails loudly here rather than silently
corrupting what the CLI prints.
"""
from __future__ import annotations

import json
import os
from typing import Optional

from pydantic import BaseModel, Field, ValidationError

try:
    from dotenv import load_dotenv

    load_dotenv()
except ImportError:
    pass

DEFAULT_MODEL = "openai/gpt-oss-120b"

SYSTEM_PROMPT = """You are the hypothesis-selection component inside an OS \
battery-diagnostic agent. You do not have tools and cannot query the \
machine -- all evidence you will ever see is in this message, already \
collected deterministically by other code.

Rules:
- Only ever pick from the given hypothesis IDs. Never invent a new one.
- Base confidence strictly on how well the evidence supports the winning \
hypothesis, not on how confident the prose sounds. Thin or contradictory \
evidence must produce a low confidence.
- If no hypothesis is well supported, prefer honesty over a confident-\
sounding guess: it is correct and expected to say the cause is unclear.
- reasoning_notes must reference the evidence given, not restate the raw \
numbers verbatim.
- Respond with JSON only, matching the required schema exactly."""


class LlmDiagnosisResult(BaseModel):
    winning_hypothesis_id: str
    confidence: float = Field(ge=0.0, le=1.0)
    finding: str
    reasoning_notes: list[str] = Field(default_factory=list)
    recommended_action: str
    risk: str
    expected_result: str


class LlmUnavailableError(RuntimeError):
    """No API key configured, the call failed, or the response didn't match
    the expected schema. Callers should catch this and fall back rather than
    crash -- an LLM being unreachable shouldn't take down a diagnostic tool
    whose whole point is to still work when things are broken."""


def select_hypothesis(
    hypotheses: list[dict],
    evidence: dict,
    model: Optional[str] = None,
) -> LlmDiagnosisResult:
    api_key = os.environ.get("GROQ_API_KEY")
    if not api_key:
        raise LlmUnavailableError(
            "GROQ_API_KEY is not set -- copy intelligence/.env.example to "
            "intelligence/.env and fill in your key."
        )

    try:
        from groq import Groq
    except ImportError as exc:
        raise LlmUnavailableError(
            "the 'groq' package isn't installed -- pip install -r intelligence/requirements.txt"
        ) from exc

    model = model or os.environ.get("GROQ_MODEL", DEFAULT_MODEL)
    client = Groq(api_key=api_key)

    user_content = json.dumps({"hypotheses": hypotheses, "evidence": evidence}, indent=2)
    schema_hint = json.dumps(LlmDiagnosisResult.model_json_schema(), indent=2)

    try:
        response = client.chat.completions.create(
            model=model,
            messages=[
                {"role": "system", "content": SYSTEM_PROMPT},
                {
                    "role": "user",
                    "content": (
                        f"Hypotheses and evidence:\n{user_content}\n\n"
                        f"Respond with JSON matching this schema:\n{schema_hint}"
                    ),
                },
            ],
            response_format={"type": "json_object"},
            temperature=0.2,
        )
    except Exception as exc:  # groq's SDK raises its own exception hierarchy
        raise LlmUnavailableError(f"Groq API call failed: {exc}") from exc

    content = response.choices[0].message.content
    try:
        return LlmDiagnosisResult.model_validate_json(content)
    except (ValidationError, ValueError) as exc:
        raise LlmUnavailableError(
            f"Groq response didn't match the expected schema: {content!r}"
        ) from exc
