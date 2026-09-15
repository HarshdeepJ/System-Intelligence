"""The battery-drain diagnostic agent.

State machine: TRIAGE -> GENERATE HYPOTHESES -> COLLECT EVIDENCE -> EVALUATE
-> DIAGNOSIS -> EXPLAIN (see tech design's "Agent Architecture").

Evidence collection is entirely deterministic -- CPU/GPU history, current
snapshot, and events are all gathered by plain function calls to
SysIntelClient, never by asking a model to go find things. Only hypothesis
*selection* (which of the fixed candidates best fits the evidence, how
confident to be, how to phrase the finding) goes to an LLM via llm.py. If
the LLM is unreachable (no API key, network failure, malformed response),
a deterministic rule-based fallback takes over so this still produces a
usable diagnosis -- degraded, not broken.
"""
from __future__ import annotations

from dataclasses import dataclass, field

from . import llm
from .tools import SysIntelClient


@dataclass
class SuggestedAction:
    """A concrete, runnable action -- computed here by plain code, never by
    the LLM. This is the one thing keeping "close the loop" from becoming
    "let the model decide what to execute": the model only ever produces
    prose (recommended_action, finding, ...); this struct is built
    separately from fixed rules and a fixed action_type/params shape that
    action_broker.cpp already recognizes."""

    action_type: str
    params: dict[str, str]
    reason: str
    description: str  # human-readable summary shown before asking for approval


@dataclass
class Diagnosis:
    finding: str
    confidence: float
    evidence: list[str]
    alternative_explanations: list[str]
    recommended_action: str
    risk: str
    expected_result: str
    reasoned_by: str = "rule-based"  # "llm" or "rule-based" -- always disclosed
    suggested_action: SuggestedAction | None = None


def _build_suggested_action(check) -> SuggestedAction:
    """The one action this project can currently take. Offered whenever
    there's an open/ongoing battery anomaly, regardless of which hypothesis
    the diagnosis settles on: Best Power Efficiency mode reduces both CPU-
    and GPU-adjacent power draw at the OS level, so it's a reasonable,
    fully-reversible thing to try even when the root cause isn't pinned
    down exactly."""
    return SuggestedAction(
        action_type="change_power_mode",
        params={"level": "best_power_efficiency"},
        reason=(
            f"battery drain anomaly: {check.live_mean_watts:.1f}W vs "
            f"{check.baseline_mean_watts:.1f}W baseline"
        ),
        description="Switch Windows' battery Power Mode to 'Best power efficiency'",
    )


_HYPOTHESES = [
    {"id": "H1", "description": "System-wide CPU workload is responsible"},
    {
        "id": "H2",
        "description": "GPU activity is responsible (only meaningful when a GPU actually "
        "reports usable telemetry -- most machines/vendors don't yet)",
    },
    {
        "id": "H3",
        "description": "Unexplained by evidence this agent currently checks (e.g. a driver "
        "regression, or a GPU/vendor this build has no telemetry for)",
    },
]


class BatteryDiagnosticAgent:
    def __init__(self, tools: SysIntelClient, min_history_days: int = 14):
        self.tools = tools
        self.min_history_days = min_history_days

    def diagnose(self) -> Diagnosis:
        # TRIAGE: is there actually something to investigate?
        check = self.tools.get_battery_anomaly_status(self.min_history_days)

        if check.result == "not_enough_history":
            return Diagnosis(
                finding="Not enough accumulated history to evaluate battery anomalies yet.",
                confidence=1.0,
                evidence=[f"check-battery reports '{check.result}'"],
                alternative_explanations=[],
                recommended_action="none -- keep recording",
                risk="n/a",
                expected_result="n/a",
                reasoned_by="rule-based",
            )

        if check.result not in ("anomaly_opened", "anomaly_ongoing"):
            return Diagnosis(
                finding="No active battery anomaly to diagnose.",
                confidence=1.0,
                evidence=[f"check-battery reports '{check.result}'"],
                alternative_explanations=[],
                recommended_action="none",
                risk="none",
                expected_result="n/a",
                reasoned_by="rule-based",
            )

        # COLLECT EVIDENCE (entirely deterministic)
        snapshot = self.tools.get_system_snapshot()
        cpu_baseline = self.tools.get_metric_history("cpu.utilization", last_minutes=60 * 24)
        cpu_recent = self.tools.get_metric_history("cpu.utilization", last_minutes=5)

        candidate_processes = []
        if check.incident is not None:
            events = self.tools.get_recent_events(last_minutes=60 * 24, limit=200)
            window_start = check.incident.started_at_ms - 15 * 60 * 1000
            window_end = check.incident.started_at_ms + 5 * 60 * 1000
            candidate_processes = [
                e.data.get("name", "?")
                for e in events.events
                if e.type == "process.started" and window_start <= e.timestamp_ms <= window_end
            ]

        evidence = {
            "battery": {
                "live_watts": round(check.live_mean_watts, 2),
                "baseline_watts": round(check.baseline_mean_watts, 2),
                "threshold_watts": round(check.threshold_watts, 2),
            },
            "cpu": {
                "recent_5min_avg_percent": (
                    round(cpu_recent.avg, 1) if cpu_recent.avg is not None else None
                ),
                "baseline_24h_avg_percent": (
                    round(cpu_baseline.avg, 1) if cpu_baseline.avg is not None else None
                ),
            },
            "gpu": [
                {
                    "vendor": g.vendor,
                    "model": g.model,
                    "utilization_percent": g.utilization_percent.model_dump(),
                    "power_watts": g.power_watts.model_dump(),
                }
                for g in snapshot.gpu
            ],
            "top_processes_by_memory": [
                f"{p.name} ({p.working_set_bytes / 1024 / 1024:.0f} MB)"
                for p in snapshot.top_processes_by_memory[:3]
            ],
            "processes_started_near_incident_onset": candidate_processes or None,
        }

        # EVALUATE + DIAGNOSIS: LLM picks the best-supported hypothesis and
        # writes the explanation, with a deterministic fallback if it can't.
        try:
            result = llm.select_hypothesis(_HYPOTHESES, evidence)
            winning = next(
                (h["description"] for h in _HYPOTHESES if h["id"] == result.winning_hypothesis_id),
                result.winning_hypothesis_id,
            )
            alternatives = [
                h["description"] for h in _HYPOTHESES if h["id"] != result.winning_hypothesis_id
            ]
            evidence_lines = [
                f"Battery discharge: {evidence['battery']['live_watts']}W observed vs "
                f"{evidence['battery']['baseline_watts']}W baseline "
                f"(threshold {evidence['battery']['threshold_watts']}W)"
            ]
            evidence_lines.extend(result.reasoning_notes)
            return Diagnosis(
                finding=f"{winning} -- {result.finding}",
                confidence=result.confidence,
                evidence=evidence_lines,
                alternative_explanations=alternatives,
                recommended_action=result.recommended_action,
                risk=result.risk,
                expected_result=result.expected_result,
                reasoned_by="llm",
                suggested_action=_build_suggested_action(check),
            )
        except llm.LlmUnavailableError as exc:
            return self._fallback_diagnose(check, evidence, reason=str(exc))

    def _fallback_diagnose(self, check, evidence: dict, reason: str) -> Diagnosis:
        """Deterministic rule-based reasoning, used only when the LLM step
        couldn't run at all. Mirrors the pre-LLM logic: CPU-elevated wins H1,
        otherwise it's honestly unexplained (H3) -- this fallback doesn't
        attempt GPU reasoning, since evaluating GPU evidence nuance is
        exactly the kind of judgment call this project chose to hand to the
        LLM rather than keep growing a rule set for.
        """
        cpu_recent_avg = evidence["cpu"]["recent_5min_avg_percent"]
        cpu_baseline_avg = evidence["cpu"]["baseline_24h_avg_percent"]

        evidence_lines = [
            f"Battery discharge: {evidence['battery']['live_watts']}W observed vs "
            f"{evidence['battery']['baseline_watts']}W baseline "
            f"(threshold {evidence['battery']['threshold_watts']}W)",
            f"[LLM reasoning unavailable, used rule-based fallback: {reason}]",
        ]

        if cpu_recent_avg is not None and cpu_baseline_avg is not None:
            evidence_lines.append(
                f"CPU utilization: {cpu_recent_avg}% now vs {cpu_baseline_avg}% (24h average)"
            )
            cpu_elevated = cpu_recent_avg > cpu_baseline_avg * 1.5 and cpu_recent_avg > 30
            if cpu_elevated:
                return Diagnosis(
                    finding="System-wide CPU workload is responsible",
                    confidence=0.7,
                    evidence=evidence_lines,
                    alternative_explanations=[h["description"] for h in _HYPOTHESES[1:]],
                    recommended_action=(
                        "Investigate which process is driving CPU usage; consider closing it "
                        "or switching to a lower power plan."
                    ),
                    risk="low -- this is a diagnosis only, no action was taken",
                    expected_result=(
                        f"Battery discharge should return toward the "
                        f"{evidence['battery']['baseline_watts']}W baseline."
                    ),
                    reasoned_by="rule-based",
                    suggested_action=_build_suggested_action(check),
                )
            evidence_lines.append(
                f"CPU utilization ({cpu_recent_avg}%) is close to its normal range -- "
                "unlikely to explain the excess draw"
            )
        else:
            evidence_lines.append("Not enough CPU history to evaluate CPU as a cause.")

        return Diagnosis(
            finding="Battery drain is confirmed abnormal, but current evidence is insufficient "
            "to identify a specific cause.",
            confidence=0.1,
            evidence=evidence_lines,
            alternative_explanations=[h["description"] for h in _HYPOTHESES],
            recommended_action="none -- reporting uncertainty rather than guessing",
            risk="n/a",
            expected_result="n/a",
            reasoned_by="rule-based",
            suggested_action=_build_suggested_action(check),
        )
