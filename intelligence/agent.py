"""The resource-anomaly diagnostic agent, generalized across every domain
the CLI can check (battery, memory, cpu, network, disk, top_cpu, thermal).

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

This started as battery-only (Phase 6); the state machine and evidence
gathering had nothing battery-specific about them except which check-*
tool to call and how to phrase the headline evidence line, so DiagnosticAgent
below pulls that out into a per-domain _DomainConfig, the same "prove the
abstraction against a second real case" move Phase 7 made for
AnomalyDetector. BatteryDiagnosticAgent stays as a thin, name-preserving
subclass so existing imports don't need to know about the generalization.
"""
from __future__ import annotations

from dataclasses import dataclass
from typing import Optional

from . import llm
from .schemas import BatteryCheckReport, Incident
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


@dataclass
class _NormalizedCheck:
    """BatteryCheckReport and the generic AnomalyCheckReport carry the same
    information under different field names (live_mean_watts vs live_mean,
    etc.) -- a real shape difference, documented in schemas.py, not just
    inconsistent naming. This is the one place that difference gets
    flattened so the rest of the agent can be domain-agnostic."""

    result: str
    live_mean: float
    baseline_mean: float
    baseline_stddev: float
    threshold: float
    incident: Optional[Incident]


def _normalize_check(check) -> _NormalizedCheck:
    if isinstance(check, BatteryCheckReport):
        return _NormalizedCheck(
            result=check.result,
            live_mean=check.live_mean_watts,
            baseline_mean=check.baseline_mean_watts,
            baseline_stddev=check.baseline_stddev_watts,
            threshold=check.threshold_watts,
            incident=check.incident,
        )
    return _NormalizedCheck(
        result=check.result,
        live_mean=check.live_mean,
        baseline_mean=check.baseline_mean,
        baseline_stddev=check.baseline_stddev,
        threshold=check.threshold,
        incident=check.incident,
    )


_BATTERY_HYPOTHESES = [
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

# CPU's own hypothesis set can't reuse "system-wide CPU workload is
# responsible" as a candidate explanation for a CPU anomaly -- that's
# circular, not a hypothesis. GPU-correlated load and an unexplained
# fallback both still make sense, so those carry over.
_CPU_HYPOTHESES = [
    {
        "id": "H1",
        "description": "GPU activity is correlated with the CPU load (e.g. a game or "
        "GPU-accelerated workload driving both)",
    },
    {"id": "H2", "description": "A process that started right before the anomaly is responsible"},
    {"id": "H3", "description": "Unexplained by evidence this agent currently checks"},
]

# Shared by memory, network, disk, and top_cpu: none of these have a
# domain-specific secondary signal the way battery/cpu have GPU, so the
# candidate explanations are the two general-purpose correlates this agent
# can actually check (system-wide CPU load, a recently-started process)
# plus honest uncertainty. This isn't circular for top_cpu the way it would
# be for cpu itself: process.top_cpu_percent (one process) and
# cpu.utilization (the whole system) are genuinely different metrics, so
# asking whether the total is *also* elevated is real evidence, not a
# restatement.
_RESOURCE_HYPOTHESES = [
    {"id": "H1", "description": "System-wide CPU workload is correlated with the anomaly"},
    {"id": "H2", "description": "A process that started right before the anomaly is responsible"},
    {"id": "H3", "description": "Unexplained by evidence this agent currently checks"},
]

# Thermal gets its own set: an unusual CPU temperature plausibly traces to
# either CPU or GPU load (a battery-like pairing, since heat is a shared
# byproduct of both), not to "a process started recently" the way a
# resource-usage anomaly does -- a process can run hot without anything
# having started or stopped around the incident.
_THERMAL_HYPOTHESES = [
    {"id": "H1", "description": "System-wide CPU workload is correlated with the temperature rise"},
    {
        "id": "H2",
        "description": "GPU activity is correlated with the temperature rise (only meaningful "
        "when a GPU actually reports usable telemetry)",
    },
    {"id": "H3", "description": "Unexplained by evidence this agent currently checks"},
]


@dataclass(frozen=True)
class _DomainConfig:
    check_method: str  # SysIntelClient method name for this domain's anomaly check
    # The CLI's check-<cli_name> subcommand name -- equal to the domain key for every
    # domain except top_cpu (Python-identifier-friendly "top_cpu" vs the CLI's
    # hyphenated "check-top-cpu"). Used only in prose evidence lines, never to
    # actually invoke anything.
    cli_name: str
    label: str  # human-readable headline, e.g. "battery discharge"
    unit: str  # matches the unit string main.cpp prints for this metric (" B/s" has its
    # own leading space, "%"/"W" don't)
    hypotheses: list[dict]
    # Which hypothesis ID means "system-wide CPU workload correlates" -- None where that
    # would be circular (the cpu domain itself) or where a domain doesn't offer it.
    cpu_workload_hypothesis_id: Optional[str]
    # Text used in a suggested action's `reason` (e.g. "battery drain anomaly: ..."). None
    # means this domain doesn't offer a suggested action at all: the Action Broker's only
    # capability, change_power_mode, is a genuine lever on CPU/GPU power draw (hence
    # battery and cpu), but not a real fix for a memory leak or elevated network/disk I/O --
    # offering it there would overstate what this agent can actually do.
    action_reason_label: Optional[str]


_DOMAINS: dict[str, _DomainConfig] = {
    "battery": _DomainConfig(
        check_method="get_battery_anomaly_status",
        cli_name="battery",
        label="battery discharge",
        unit="W",
        hypotheses=_BATTERY_HYPOTHESES,
        cpu_workload_hypothesis_id="H1",
        action_reason_label="battery drain",
    ),
    "cpu": _DomainConfig(
        check_method="get_cpu_anomaly_status",
        cli_name="cpu",
        label="CPU utilization",
        unit="%",
        hypotheses=_CPU_HYPOTHESES,
        cpu_workload_hypothesis_id=None,
        action_reason_label="CPU load",
    ),
    "memory": _DomainConfig(
        check_method="get_memory_anomaly_status",
        cli_name="memory",
        label="memory load",
        unit="%",
        hypotheses=_RESOURCE_HYPOTHESES,
        cpu_workload_hypothesis_id="H1",
        action_reason_label=None,
    ),
    "network": _DomainConfig(
        check_method="get_network_anomaly_status",
        cli_name="network",
        label="network throughput",
        unit=" B/s",
        hypotheses=_RESOURCE_HYPOTHESES,
        cpu_workload_hypothesis_id="H1",
        action_reason_label=None,
    ),
    "disk": _DomainConfig(
        check_method="get_disk_anomaly_status",
        cli_name="disk",
        label="disk I/O throughput",
        unit=" B/s",
        hypotheses=_RESOURCE_HYPOTHESES,
        cpu_workload_hypothesis_id="H1",
        action_reason_label=None,
    ),
    "top_cpu": _DomainConfig(
        check_method="get_top_cpu_anomaly_status",
        cli_name="top-cpu",
        label="top process's CPU usage",
        unit="%",
        hypotheses=_RESOURCE_HYPOTHESES,
        cpu_workload_hypothesis_id="H1",
        # Unlike memory/network/disk, throttling CPU/GPU power draw directly
        # addresses "one process is monopolizing the CPU" -- the same lever
        # the cpu domain uses, just triggered by a per-process signal instead
        # of the system-wide total.
        action_reason_label="runaway process CPU load",
    ),
    "thermal": _DomainConfig(
        check_method="get_thermal_anomaly_status",
        cli_name="thermal",
        label="CPU temperature",
        unit="C",
        hypotheses=_THERMAL_HYPOTHESES,
        cpu_workload_hypothesis_id="H1",
        # Best Power Efficiency mode reducing CPU/GPU power draw is arguably
        # the most direct use of this action yet: less power in, less heat
        # out.
        action_reason_label="elevated temperature",
    ),
}


def _sentence_case(label: str) -> str:
    """Upper-cases just the first character, preserving the rest -- unlike
    str.capitalize(), which would turn "CPU utilization" into "Cpu
    utilization"."""
    return label[0].upper() + label[1:] if label else label


def _build_suggested_action(reason_label: str, live_mean: float, baseline_mean: float, unit: str) -> SuggestedAction:
    """The one action this project can currently take, offered whenever
    there's an open/ongoing anomaly in a domain the Action Broker's lever
    actually addresses: Best Power Efficiency mode reduces both CPU- and
    GPU-adjacent power draw at the OS level, so it's a reasonable, fully-
    reversible thing to try even when the root cause isn't pinned down
    exactly."""
    return SuggestedAction(
        action_type="change_power_mode",
        params={"level": "best_power_efficiency"},
        reason=f"{reason_label} anomaly: {live_mean:.1f}{unit} vs {baseline_mean:.1f}{unit} baseline",
        description="Switch Windows' battery Power Mode to 'Best power efficiency'",
    )


class DiagnosticAgent:
    def __init__(self, tools: SysIntelClient, domain: str, min_history_days: int = 14):
        if domain not in _DOMAINS:
            raise ValueError(f"unknown diagnosis domain: {domain!r} (expected one of {sorted(_DOMAINS)})")
        self.tools = tools
        self.domain = domain
        self.min_history_days = min_history_days
        self._config = _DOMAINS[domain]

    def _maybe_action(self, norm: _NormalizedCheck) -> SuggestedAction | None:
        if self._config.action_reason_label is None:
            return None
        return _build_suggested_action(
            self._config.action_reason_label, norm.live_mean, norm.baseline_mean, self._config.unit
        )

    def diagnose(self) -> Diagnosis:
        # TRIAGE: is there actually something to investigate?
        check = getattr(self.tools, self._config.check_method)(self.min_history_days)
        norm = _normalize_check(check)

        if norm.result == "not_enough_history":
            return Diagnosis(
                finding=f"Not enough accumulated history to evaluate {self._config.label} anomalies yet.",
                confidence=1.0,
                evidence=[f"check-{self._config.cli_name} reports '{norm.result}'"],
                alternative_explanations=[],
                recommended_action="none -- keep recording",
                risk="n/a",
                expected_result="n/a",
                reasoned_by="rule-based",
            )

        if norm.result not in ("anomaly_opened", "anomaly_ongoing"):
            return Diagnosis(
                finding=f"No active {self._config.label} anomaly to diagnose.",
                confidence=1.0,
                evidence=[f"check-{self._config.cli_name} reports '{norm.result}'"],
                alternative_explanations=[],
                recommended_action="none",
                risk="none",
                expected_result="n/a",
                reasoned_by="rule-based",
            )

        # COLLECT EVIDENCE (entirely deterministic)
        snapshot = self.tools.get_system_snapshot()

        candidate_processes = []
        if norm.incident is not None:
            events = self.tools.get_recent_events(last_minutes=60 * 24, limit=200)
            window_start = norm.incident.started_at_ms - 15 * 60 * 1000
            window_end = norm.incident.started_at_ms + 5 * 60 * 1000
            candidate_processes = [
                e.data.get("name", "?")
                for e in events.events
                if e.type == "process.started" and window_start <= e.timestamp_ms <= window_end
            ]

        evidence: dict = {
            self.domain: {
                "live": round(norm.live_mean, 2),
                "baseline": round(norm.baseline_mean, 2),
                "threshold": round(norm.threshold, 2),
                "unit": self._config.unit.strip(),
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

        # cpu.utilization history is only a useful *correlate* for a non-CPU
        # anomaly -- for the cpu domain it would just restate evidence[self.domain]
        # under a colliding "cpu" key. (top_cpu is a different metric --
        # one process's CPU%, not the system total -- so it keeps this block.)
        if self.domain != "cpu":
            cpu_baseline = self.tools.get_metric_history("cpu.utilization", last_minutes=60 * 24)
            cpu_recent = self.tools.get_metric_history("cpu.utilization", last_minutes=5)
            evidence["cpu"] = {
                "recent_5min_avg_percent": (
                    round(cpu_recent.avg, 1) if cpu_recent.avg is not None else None
                ),
                "baseline_24h_avg_percent": (
                    round(cpu_baseline.avg, 1) if cpu_baseline.avg is not None else None
                ),
            }

        headline = (
            f"{self._config.label}: {evidence[self.domain]['live']}{self._config.unit} observed vs "
            f"{evidence[self.domain]['baseline']}{self._config.unit} baseline "
            f"(threshold {evidence[self.domain]['threshold']}{self._config.unit})"
        )

        # EVALUATE + DIAGNOSIS: LLM picks the best-supported hypothesis and
        # writes the explanation, with a deterministic fallback if it can't.
        try:
            result = llm.select_hypothesis(self._config.hypotheses, evidence)
            winning = next(
                (h["description"] for h in self._config.hypotheses if h["id"] == result.winning_hypothesis_id),
                result.winning_hypothesis_id,
            )
            alternatives = [
                h["description"]
                for h in self._config.hypotheses
                if h["id"] != result.winning_hypothesis_id
            ]
            evidence_lines = [headline]
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
                suggested_action=self._maybe_action(norm),
            )
        except llm.LlmUnavailableError as exc:
            return self._fallback_diagnose(norm, evidence, headline, reason=str(exc))

    def _fallback_diagnose(
        self, norm: _NormalizedCheck, evidence: dict, headline: str, reason: str
    ) -> Diagnosis:
        """Deterministic rule-based reasoning, used only when the LLM step
        couldn't run at all. Mirrors the pre-LLM battery logic: CPU-elevated
        wins the "system-wide CPU workload" hypothesis where the domain
        offers one, otherwise it's honestly unexplained -- this fallback
        doesn't attempt GPU or candidate-process reasoning, since evaluating
        that nuance is exactly the kind of judgment call this project chose
        to hand to the LLM rather than keep growing a rule set for.
        """
        evidence_lines = [headline, f"[LLM reasoning unavailable, used rule-based fallback: {reason}]"]

        cpu_evidence = evidence.get("cpu")
        cpu_hypothesis_id = self._config.cpu_workload_hypothesis_id
        if cpu_evidence is not None and cpu_hypothesis_id is not None:
            cpu_recent_avg = cpu_evidence["recent_5min_avg_percent"]
            cpu_baseline_avg = cpu_evidence["baseline_24h_avg_percent"]

            if cpu_recent_avg is not None and cpu_baseline_avg is not None:
                evidence_lines.append(
                    f"CPU utilization: {cpu_recent_avg}% now vs {cpu_baseline_avg}% (24h average)"
                )
                cpu_elevated = cpu_recent_avg > cpu_baseline_avg * 1.5 and cpu_recent_avg > 30
                if cpu_elevated:
                    winning = next(
                        h["description"] for h in self._config.hypotheses if h["id"] == cpu_hypothesis_id
                    )
                    alternatives = [
                        h["description"]
                        for h in self._config.hypotheses
                        if h["id"] != cpu_hypothesis_id
                    ]
                    return Diagnosis(
                        finding=winning,
                        confidence=0.7,
                        evidence=evidence_lines,
                        alternative_explanations=alternatives,
                        recommended_action=(
                            "Investigate which process is driving CPU usage; consider closing it "
                            "or switching to a lower power plan."
                        ),
                        risk="low -- this is a diagnosis only, no action was taken",
                        expected_result=(
                            f"{_sentence_case(self._config.label)} should return toward its "
                            f"{evidence[self.domain]['baseline']}{self._config.unit} baseline."
                        ),
                        reasoned_by="rule-based",
                        suggested_action=self._maybe_action(norm),
                    )
                evidence_lines.append(
                    f"CPU utilization ({cpu_recent_avg}%) is close to its normal range -- "
                    "unlikely to explain the anomaly"
                )
            else:
                evidence_lines.append("Not enough CPU history to evaluate CPU as a cause.")

        return Diagnosis(
            finding=f"{_sentence_case(self._config.label)} anomaly is confirmed abnormal, but "
            "current evidence is insufficient to identify a specific cause.",
            confidence=0.1,
            evidence=evidence_lines,
            alternative_explanations=[h["description"] for h in self._config.hypotheses],
            recommended_action="none -- reporting uncertainty rather than guessing",
            risk="n/a",
            expected_result="n/a",
            reasoned_by="rule-based",
            suggested_action=self._maybe_action(norm),
        )


class BatteryDiagnosticAgent(DiagnosticAgent):
    """Thin, name-preserving wrapper kept for existing callers/imports --
    equivalent to DiagnosticAgent(tools, "battery", min_history_days)."""

    def __init__(self, tools: SysIntelClient, min_history_days: int = 14):
        super().__init__(tools, "battery", min_history_days)
