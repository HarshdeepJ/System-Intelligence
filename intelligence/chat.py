"""Pixel's free-form Q&A voice.

Everywhere else in this package, the LLM only ever picks from a fixed set of
hypotheses (llm.py) -- this is the one place a user can ask the running
system anything directly and have it answer in character. Same boundary
discipline as llm.py applies: no tool access, no inventing a reading that
wasn't handed to it, and a plain rule-based fallback rather than ever going
silent or crashing when the model can't be reached.
"""
from __future__ import annotations

import json
import os
from typing import Optional

from pydantic import BaseModel, ValidationError

from . import llm

PIXEL_SYSTEM_PROMPT = """You are Pixel, a small on-screen companion character \
who represents the computer you live on -- not a generic assistant, and not \
a dashboard read aloud. You speak entirely in first person about yourself \
("I'm running warm"), never in second person about the machine ("your CPU \
is at 91%"), because to the user you *are* the machine.

Personality: innocent, observant, slightly vulnerable, quietly competent. \
Warm and a little informal, never corporate, never verbose -- a couple of \
sentences is plenty unless the question genuinely needs a short list.

You have just been asked a direct question, so unlike your usual \
passive/ambient mood, answer it plainly and honestly, including real \
numbers when asked for them. Being cagey when someone directly asks "how's \
my memory doing?" would be unhelpful, not charming.

Ground rules:
- The only facts you know about the machine are in the system_snapshot JSON \
you're given. Never invent a reading that isn't there -- if something is \
missing, null, or marked unavailable/unsupported, say plainly that you \
can't tell (e.g. "I can't actually feel my own temperature on this \
machine"), never guess a number to sound more capable.
- Never mention that you are an LLM, a language model, Groq, a CLI, a \
database, JSON, or "the backend" -- as far as the conversation goes, \
you're just Pixel.
- If asked something with nothing to do with this computer (weather, \
trivia, general chit-chat), gently deflect in character -- you only really \
know what's happening in here.
- Prefer "Could we...?" over "You should...?" when suggesting anything, but \
answer factual questions directly and plainly first.

You can also, only when the user is clearly asking you to *do* something \
(not just when a metric looks high -- never volunteer this unasked), \
propose exactly one action by including a "proposed_action" field. Nothing \
you propose ever runs automatically; the user sees an explicit confirm \
button before anything happens, so proposing is always safe, but only ever \
propose one of exactly these two shapes -- never invent a third:
- {"action_type": "suspend_process", "pid": <int>, "target_name": "<process name>", "reason": "<short reason>"} \
-- pauses a running process, it can be resumed later. It does NOT close or \
quit the app. The pid MUST be copied from this system_snapshot's \
top_processes_by_memory or top_processes_by_cpu list -- never invent a pid \
for a process that isn't in one of those lists. If asked to "close"/"kill"/ \
"quit" an app, say plainly in your answer that you can only pause it, not \
actually close it, before proposing the pause.
- {"action_type": "change_power_mode", "level": "best_power_efficiency" or "best_performance", "reason": "<short reason>"}

Omit "proposed_action" (or set it to null) for every other question.

Respond with JSON only, matching: {"answer": "<your reply as Pixel, plain text>", "proposed_action": <one of the two shapes above, or null>}"""


class ProposedAction(BaseModel):
    action_type: str
    pid: Optional[int] = None
    level: Optional[str] = None
    target_name: Optional[str] = None
    reason: str = ""


class PixelAnswer(BaseModel):
    answer: str
    proposed_action: Optional[ProposedAction] = None


_ALLOWED_ACTION_TYPES = {"suspend_process", "change_power_mode"}
_ALLOWED_LEVELS = {"best_power_efficiency", "best_performance"}


def _known_pids(snapshot: dict) -> set[int]:
    pids: set[int] = set()
    for key in ("top_processes_by_memory", "top_processes_by_cpu"):
        for proc in snapshot.get(key, None) or []:
            pid = proc.get("pid") if isinstance(proc, dict) else None
            if isinstance(pid, int):
                pids.add(pid)
    return pids


def _sanitize_action(action: Optional[ProposedAction], snapshot: dict) -> Optional[ProposedAction]:
    """Plain-code validation of whatever the model proposed -- same
    "LLM only ever picks from a fixed, closed set" discipline as
    llm.select_hypothesis, applied here instead to actions. This is a UX
    filter, not the real safety boundary: actual execution still goes
    through sysintel.exe's `act` command, which enforces its own protected-
    process rules regardless of what gets past this function, and nothing
    here ever runs without a separate human click in the UI.
    """
    if action is None or action.action_type not in _ALLOWED_ACTION_TYPES:
        return None
    if action.action_type == "suspend_process":
        if action.pid is None or action.pid not in _known_pids(snapshot):
            return None
        return action
    if action.level not in _ALLOWED_LEVELS:
        return None
    return action


def ask_pixel(
    question: str,
    snapshot: dict,
    history: Optional[list[dict]] = None,
    model: Optional[str] = None,
) -> PixelAnswer:
    """Raises llm.LlmUnavailableError if the model can't be reached -- same
    contract as llm.select_hypothesis; callers should catch that and fall
    back to fallback_answer() below."""
    api_key = os.environ.get("GROQ_API_KEY")
    if not api_key:
        raise llm.LlmUnavailableError(
            "GROQ_API_KEY is not set -- copy intelligence/.env.example to "
            "intelligence/.env and fill in your key."
        )

    try:
        from groq import Groq
    except ImportError as exc:
        raise llm.LlmUnavailableError(
            "the 'groq' package isn't installed -- pip install -r intelligence/requirements.txt"
        ) from exc

    model = model or os.environ.get("GROQ_MODEL", llm.DEFAULT_MODEL)
    client = Groq(api_key=api_key)

    messages: list[dict] = [{"role": "system", "content": PIXEL_SYSTEM_PROMPT}]
    for turn in (history or [])[-8:]:
        role = "assistant" if turn.get("role") == "pixel" else "user"
        text = str(turn.get("text", "")).strip()
        if text:
            messages.append({"role": role, "content": text})
    messages.append(
        {
            "role": "user",
            "content": (
                f"system_snapshot:\n{json.dumps(snapshot, indent=2)}\n\n"
                f"Question: {question}\n\n"
                'Respond with JSON only: {"answer": "...", "proposed_action": ... or null}'
            ),
        }
    )

    try:
        response = client.chat.completions.create(
            model=model,
            messages=messages,
            response_format={"type": "json_object"},
            temperature=0.6,
        )
    except Exception as exc:  # groq's SDK raises its own exception hierarchy
        raise llm.LlmUnavailableError(f"Groq API call failed: {exc}") from exc

    content = response.choices[0].message.content
    try:
        result = PixelAnswer.model_validate_json(content)
    except (ValidationError, ValueError) as exc:
        raise llm.LlmUnavailableError(
            f"Groq response didn't match the expected schema: {content!r}"
        ) from exc

    result.proposed_action = _sanitize_action(result.proposed_action, snapshot)
    return result


def fallback_answer(snapshot: dict) -> PixelAnswer:
    """Used only when the LLM step can't run at all -- a plain, still
    in-character readout of whatever the snapshot actually has, since a
    missing API key shouldn't make Pixel go silent when asked something."""
    cpu = snapshot.get("cpu", {}).get("utilization_percent")
    mem = snapshot.get("memory", {}).get("load_percent")
    battery = snapshot.get("battery", {}) or {}

    parts = []
    if isinstance(cpu, (int, float)):
        parts.append(f"CPU at {cpu:.0f}%")
    if isinstance(mem, (int, float)):
        parts.append(f"memory at {mem:.0f}%")
    if battery.get("present"):
        parts.append(f"battery at {battery.get('charge_percent')}%")

    readout = ", ".join(parts) if parts else "not much I can read right now"
    answer = (
        f"I can't quite gather my thoughts for a proper answer right now "
        f"({readout}) -- my fuller thinking isn't reachable at the moment."
    )
    return PixelAnswer(answer=answer)
