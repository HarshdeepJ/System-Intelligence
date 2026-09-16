from __future__ import annotations

import argparse
import sys
from pathlib import Path

from .agent import Diagnosis, DiagnosticAgent
from .schemas import ActionOutcome
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


def print_action_outcome(outcome: ActionOutcome) -> None:
    if not outcome.known_action:
        print(f"\nAction refused: {outcome.message}")
        return
    if not outcome.executed:
        print(f"\n{'No-op' if outcome.success else 'Not applied'}: {outcome.message}")
        return

    print(f"\n{'Applied' if outcome.success else 'FAILED'}: {outcome.message}")
    print(f"  Previous state   {outcome.previous_state}")
    print(f"  New state        {outcome.new_state}")
    if outcome.action_id:
        print(
            f"  Action id        {outcome.action_id}  "
            f"(undo with: sysintel act rollback {outcome.action_id} --yes)"
        )


def handle_suggested_action(
    tools: SysIntelClient, diagnosis: Diagnosis, auto_approve: bool, no_act: bool
) -> None:
    """The approval gate. `diagnosis.suggested_action` -- when present -- is
    always a fixed action_type/params pair computed by plain code in
    agent.py, never something the LLM wrote; this function's only job is to
    get a human's (or an explicit --auto-approve flag's) yes before it ever
    reaches the Action Broker.
    """
    action = diagnosis.suggested_action
    if action is None or no_act:
        return

    print("\nSuggested action")
    print(f"  {action.description}")

    if auto_approve:
        print("  (--auto-approve given, applying without an interactive prompt)")
        approved = True
    else:
        try:
            answer = input("\nApply this now? [y/N]: ").strip().lower()
        except EOFError:
            answer = "n"
        approved = answer == "y"

    if not approved:
        level = action.params.get("level", "")
        print("Not applied. Run it yourself later with:")
        print(f'  sysintel act change-power-mode --level {level} --reason "{action.reason}" --yes')
        return

    outcome = tools.apply_change_power_mode(
        level=action.params["level"], reason=action.reason, approved=True
    )
    print_action_outcome(outcome)


def default_sysintel_exe() -> str:
    return str(Path(__file__).resolve().parent.parent / "build" / "sysintel.exe")


# Every domain DiagnosticAgent knows about (see agent.py's _DOMAINS) gets the
# same diagnose-<domain> subcommand, wired up identically -- only the domain
# name and help text differ.
_DIAGNOSE_COMMANDS: dict[str, tuple[str, str]] = {
    "diagnose-battery": ("battery", "Investigate a battery-drain anomaly"),
    "diagnose-memory": ("memory", "Investigate a memory-usage anomaly"),
    "diagnose-cpu": ("cpu", "Investigate a CPU-usage anomaly"),
    "diagnose-network": ("network", "Investigate a network-throughput anomaly"),
    "diagnose-disk": ("disk", "Investigate a disk I/O anomaly"),
    "diagnose-top-cpu": ("top_cpu", "Investigate a single process monopolizing the CPU"),
    "diagnose-thermal": ("thermal", "Investigate an elevated CPU temperature"),
}


def _add_diagnose_subparser(sub: argparse._SubParsersAction, command: str, help_text: str) -> None:
    diag = sub.add_parser(command, help=help_text)
    diag.add_argument("--db", default="sysintel.db")
    diag.add_argument("--sysintel-exe", default=default_sysintel_exe())
    diag.add_argument("--min-history-days", type=int, default=14)
    diag.add_argument(
        "--auto-approve",
        action="store_true",
        help="Apply the suggested action without an interactive y/N prompt",
    )
    diag.add_argument(
        "--no-act",
        action="store_true",
        help="Never offer to apply an action -- diagnosis only",
    )


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="intelligence")
    sub = parser.add_subparsers(dest="command", required=True)

    for command, (_, help_text) in _DIAGNOSE_COMMANDS.items():
        _add_diagnose_subparser(sub, command, help_text)

    args = parser.parse_args(argv)

    if args.command in _DIAGNOSE_COMMANDS:
        domain, _ = _DIAGNOSE_COMMANDS[args.command]
        tools = SysIntelClient(args.sysintel_exe, args.db)
        agent = DiagnosticAgent(tools, domain, min_history_days=args.min_history_days)
        try:
            diagnosis = agent.diagnose()
        except SysIntelToolError as exc:
            print(f"error: {exc}", file=sys.stderr)
            return 1
        print_diagnosis(diagnosis)
        try:
            handle_suggested_action(tools, diagnosis, args.auto_approve, args.no_act)
        except SysIntelToolError as exc:
            print(f"error applying action: {exc}", file=sys.stderr)
            return 1
        return 0

    return 1


if __name__ == "__main__":
    sys.exit(main())
