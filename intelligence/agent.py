"""The battery-drain diagnostic agent.

State machine: TRIAGE -> GENERATE HYPOTHESES -> COLLECT EVIDENCE -> EVALUATE
-> DIAGNOSIS -> EXPLAIN (see tech design's "Agent Architecture").

Deliberately deterministic, not LLM-driven: with only CPU/memory/process
visibility right now (no GPU collector, no driver-change history yet), the
hypothesis space is exactly two items -- small enough that a fixed rule set
is honest and sufficient. An LLM call would be machinery ahead of need here.
Once a GPU collector exists and there are real competing hypotheses to weigh
(CPU vs. GPU vs. a specific process), replace `_generate_hypotheses` with an
actual model call -- the rest of this pipeline (evidence collection,
evaluation, explanation) doesn't need to change.
"""
from __future__ import annotations

from dataclasses import dataclass, field

from .tools import SysIntelClient


@dataclass
class Hypothesis:
    id: str
    description: str
    confidence: float = 0.0
    supporting_evidence: list[str] = field(default_factory=list)
    contradicting_evidence: list[str] = field(default_factory=list)


@dataclass
class Diagnosis:
    finding: str
    confidence: float
    evidence: list[str]
    alternative_explanations: list[str]
    recommended_action: str
    risk: str
    expected_result: str


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
            )

        # GENERATE HYPOTHESES (rule-based -- see module docstring)
        h_cpu = Hypothesis("H1", "System-wide CPU workload is responsible")
        h_unknown = Hypothesis(
            "H2",
            "Unexplained by evidence this agent currently checks (likely GPU activity or a "
            "driver regression -- a GPU collector exists at the C++ layer as of Phase 3.5, "
            "but isn't wired into this diagnostic agent yet, and only reports real telemetry "
            "on NVIDIA hardware)",
        )
        hypotheses = [h_cpu, h_unknown]

        # COLLECT EVIDENCE
        snapshot = self.tools.get_system_snapshot()
        cpu_baseline = self.tools.get_metric_history("cpu.utilization", last_minutes=60 * 24)
        cpu_recent = self.tools.get_metric_history("cpu.utilization", last_minutes=5)

        evidence: list[str] = [
            f"Battery discharge: {check.live_mean_watts:.1f}W observed vs "
            f"{check.baseline_mean_watts:.1f}W baseline (threshold {check.threshold_watts:.1f}W)"
        ]

        # EVALUATE: does elevated CPU plausibly explain the excess power draw?
        if cpu_recent.avg is not None and cpu_baseline.avg is not None:
            evidence.append(
                f"CPU utilization: {cpu_recent.avg:.1f}% now vs "
                f"{cpu_baseline.avg:.1f}% (24h average)"
            )

            cpu_elevated = cpu_recent.avg > cpu_baseline.avg * 1.5 and cpu_recent.avg > 30
            if cpu_elevated:
                h_cpu.confidence = 0.7
                h_cpu.supporting_evidence.append(
                    f"CPU utilization ({cpu_recent.avg:.1f}%) is well above its 24h average "
                    f"({cpu_baseline.avg:.1f}%), consistent with a CPU-driven power increase"
                )
            else:
                excess_watts = check.live_mean_watts - check.baseline_mean_watts
                h_cpu.confidence = 0.1
                h_cpu.contradicting_evidence.append(
                    f"CPU utilization ({cpu_recent.avg:.1f}%) is close to its normal range -- "
                    f"unlikely to explain {excess_watts:.1f}W of excess draw"
                )
                h_unknown.confidence = 0.6
                h_unknown.supporting_evidence.append(
                    "CPU is normal, so the excess draw isn't explained by anything this "
                    "build can currently measure"
                )
        else:
            evidence.append("Not enough CPU history to evaluate CPU as a cause.")

        top_processes = snapshot.top_processes_by_memory[:3]
        if top_processes:
            evidence.append(
                "Top processes by memory: "
                + ", ".join(
                    f"{p.name} ({p.working_set_bytes / 1024 / 1024:.0f} MB)"
                    for p in top_processes
                )
            )

        # DIAGNOSIS: pick the best-supported hypothesis; be honest when it's weak
        best = max(hypotheses, key=lambda h: h.confidence)
        alternatives = [h.description for h in hypotheses if h is not best]

        if best.confidence >= 0.5:
            finding = best.description
            if best.id == "H1":
                recommended_action = (
                    "Investigate which process is driving CPU usage; consider closing it "
                    "or switching to a lower power plan."
                )
            else:
                recommended_action = (
                    "None recommended -- a GPU collector is needed before this build can "
                    "test the likeliest remaining cause."
                )
            risk = "low -- this is a diagnosis only, no action was taken"
            expected_result = (
                f"Battery discharge should return toward the "
                f"{check.baseline_mean_watts:.1f}W baseline."
            )
        else:
            finding = (
                "Battery drain is confirmed abnormal, but current evidence is insufficient "
                "to identify a specific cause."
            )
            recommended_action = "none -- reporting uncertainty rather than guessing"
            risk = "n/a"
            expected_result = "n/a"

        return Diagnosis(
            finding=finding,
            confidence=best.confidence,
            evidence=evidence,
            alternative_explanations=alternatives,
            recommended_action=recommended_action,
            risk=risk,
            expected_result=expected_result,
        )
