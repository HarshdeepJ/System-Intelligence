"""The agent's only way of touching the machine.

Every method here maps to exactly one sysintel.exe subcommand with a fixed,
narrow argument shape. The agent can never construct an arbitrary command
string -- arguments are always passed as a list to subprocess.run (never
through a shell), so there's no path from "the LLM said something" to "a
shell executed it." This is the same "no shell access for the LLM" boundary
the tech design calls out, just implemented over subprocess + JSON for now
instead of the eventual named-pipe IPC.
"""
from __future__ import annotations

import json
import subprocess
from pathlib import Path
from typing import Union

from .schemas import (
    ActionOutcome,
    AnomalyCheckReport,
    BatteryCheckReport,
    EventsResult,
    MetricHistory,
    ProcessCpuInfo,
    SystemSnapshot,
)


class SysIntelToolError(RuntimeError):
    """Raised when sysintel.exe fails or returns something we can't parse."""


class SysIntelClient:
    def __init__(self, sysintel_exe: Union[str, Path], db_path: Union[str, Path]):
        self._exe = str(sysintel_exe)
        self._db = str(db_path)

    def _run(self, *args: str):
        try:
            result = subprocess.run(
                [self._exe, *args],
                capture_output=True,
                text=True,
                timeout=15,
                check=True,
            )
        except subprocess.CalledProcessError as exc:
            raise SysIntelToolError(
                f"sysintel {' '.join(args)} failed (exit {exc.returncode}): {exc.stderr}"
            ) from exc
        except subprocess.TimeoutExpired as exc:
            raise SysIntelToolError(f"sysintel {' '.join(args)} timed out") from exc

        try:
            return json.loads(result.stdout)
        except json.JSONDecodeError as exc:
            raise SysIntelToolError(
                f"sysintel {' '.join(args)} did not return valid JSON: {result.stdout!r}"
            ) from exc

    def get_system_snapshot(self) -> SystemSnapshot:
        return SystemSnapshot.model_validate(self._run("status", "--json"))

    def get_metric_history(self, metric: str, last_minutes: int = 30) -> MetricHistory:
        return MetricHistory.model_validate(
            self._run(
                "history", metric, "--last", str(last_minutes), "--db", self._db, "--json"
            )
        )

    def get_battery_anomaly_status(self, min_history_days: int = 14) -> BatteryCheckReport:
        return BatteryCheckReport.model_validate(
            self._run(
                "check-battery",
                "--db",
                self._db,
                "--min-history-days",
                str(min_history_days),
                "--json",
            )
        )

    def _get_anomaly_status(self, command: str, min_history_days: int) -> AnomalyCheckReport:
        return AnomalyCheckReport.model_validate(
            self._run(
                command,
                "--db",
                self._db,
                "--min-history-days",
                str(min_history_days),
                "--json",
            )
        )

    def get_memory_anomaly_status(self, min_history_days: int = 14) -> AnomalyCheckReport:
        return self._get_anomaly_status("check-memory", min_history_days)

    def get_cpu_anomaly_status(self, min_history_days: int = 14) -> AnomalyCheckReport:
        return self._get_anomaly_status("check-cpu", min_history_days)

    def get_network_anomaly_status(self, min_history_days: int = 14) -> AnomalyCheckReport:
        return self._get_anomaly_status("check-network", min_history_days)

    def get_disk_anomaly_status(self, min_history_days: int = 14) -> AnomalyCheckReport:
        return self._get_anomaly_status("check-disk", min_history_days)

    def get_top_cpu_anomaly_status(self, min_history_days: int = 14) -> AnomalyCheckReport:
        return self._get_anomaly_status("check-top-cpu", min_history_days)

    def get_thermal_anomaly_status(self, min_history_days: int = 14) -> AnomalyCheckReport:
        return self._get_anomaly_status("check-thermal", min_history_days)

    def get_top_processes_by_cpu(self, limit: int = 8) -> list[ProcessCpuInfo]:
        """A live query, not `--db`-backed like the anomaly-status methods --
        mirrors the CLI's own `top-cpu` debug command, which takes ~1s (PDH's
        two-sample rate trick, same as the total CPU collector)."""
        return [
            ProcessCpuInfo.model_validate(p)
            for p in self._run("top-cpu", "--limit", str(limit), "--json")
        ]

    def get_recent_events(self, last_minutes: int = 60, limit: int = 50) -> EventsResult:
        return EventsResult.model_validate(
            self._run(
                "events",
                "--last",
                str(last_minutes),
                "--limit",
                str(limit),
                "--db",
                self._db,
                "--json",
            )
        )

    def apply_change_power_mode(
        self, level: str, reason: str, approved: bool = True
    ) -> ActionOutcome:
        """The one action this agent can take. `approved` defaults to True
        because by the time main.py calls this, a human has already said
        yes (interactively, or via --auto-approve) -- the actual approval
        gate is main.py's prompt, not this method. Passing approved=False
        gets a dry-run preview instead, same as the CLI's default."""
        args = [
            "act",
            "change-power-mode",
            "--level",
            level,
            "--reason",
            reason,
            "--db",
            self._db,
            "--json",
        ]
        if approved:
            args.append("--yes")
        return ActionOutcome.model_validate(self._run(*args))

    def apply_suspend_process(self, pid: str, reason: str, approved: bool = True) -> ActionOutcome:
        """The second action this agent can take -- suspends (not
        terminates) a process, so it can always be undone via
        rollback_action(). See core/actions/process_control.hpp for why
        suspend rather than terminate was the deliberate choice here."""
        args = [
            "act",
            "suspend-process",
            str(pid),
            "--reason",
            reason,
            "--db",
            self._db,
            "--json",
        ]
        if approved:
            args.append("--yes")
        return ActionOutcome.model_validate(self._run(*args))

    def rollback_action(self, action_id: str, approved: bool = True) -> ActionOutcome:
        args = ["act", "rollback", action_id, "--db", self._db, "--json"]
        if approved:
            args.append("--yes")
        return ActionOutcome.model_validate(self._run(*args))
