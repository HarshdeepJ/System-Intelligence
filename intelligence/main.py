from __future__ import annotations

import argparse
import sys
from pathlib import Path

from .agent import BatteryDiagnosticAgent, Diagnosis
from .tools import SysIntelClient, SysIntelToolError

# Windows' console defaults to a legacy codepage (cp1252) that can't encode
# a lot of ordinary Unicode punctuation (curly quotes, narrow no-break
# spaces, em dashes...) an LLM will happily produce. Reconfigure stdout to
# UTF-8 so a model's word choice can never crash the CLI.
if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")


def print_diagnosis(diagnosis: Diagnosis) -> None:
    # Matches the PRD's evidence model: Finding / Confidence / Evidence /
    # Alternative explanations / Recommended action / Risk / Expected result.
    reasoning_label = "LLM (Groq)" if diagnosis.reasoned_by == "llm" else "rule-based fallback"
    print(f"[reasoned by: {reasoning_label}]\n")

    print("Finding")
    print(f"  {diagnosis.finding}\n")

    print(f"Confidence: {diagnosis.confidence * 100:.0f}%\n")

    print("Evidence")
    for line in diagnosis.evidence:
        print(f"  - {line}")

    if diagnosis.alternative_explanations:
        print("\nAlternative explanations")
        for alt in diagnosis.alternative_explanations:
            print(f"  - {alt}")

    print(f"\nRecommended action\n  {diagnosis.recommended_action}")
    print(f"\nRisk\n  {diagnosis.risk}")
    print(f"\nExpected result\n  {diagnosis.expected_result}")


def default_sysintel_exe() -> str:
    return str(Path(__file__).resolve().parent.parent / "build" / "sysintel.exe")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="intelligence")
    sub = parser.add_subparsers(dest="command", required=True)

    diag = sub.add_parser("diagnose-battery", help="Investigate a battery-drain anomaly")
    diag.add_argument("--db", default="sysintel.db")
    diag.add_argument("--sysintel-exe", default=default_sysintel_exe())
    diag.add_argument("--min-history-days", type=int, default=14)

    args = parser.parse_args(argv)

    if args.command == "diagnose-battery":
        tools = SysIntelClient(args.sysintel_exe, args.db)
        agent = BatteryDiagnosticAgent(tools, min_history_days=args.min_history_days)
        try:
            diagnosis = agent.diagnose()
        except SysIntelToolError as exc:
            print(f"error: {exc}", file=sys.stderr)
            return 1
        print_diagnosis(diagnosis)
        return 0

    return 1


if __name__ == "__main__":
    sys.exit(main())
