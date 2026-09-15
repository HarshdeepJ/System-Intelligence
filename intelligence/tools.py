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

from .schemas import BatteryCheckReport, MetricHistory, SystemSnapshot


class SysIntelToolError(RuntimeError):
    """Raised when sysintel.exe fails or returns something we can't parse."""


class SysIntelClient:
    def __init__(self, sysintel_exe: Union[str, Path], db_path: Union[str, Path]):
        self._exe = str(sysintel_exe)
        self._db = str(db_path)

    def _run(self, *args: str) -> dict:
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
