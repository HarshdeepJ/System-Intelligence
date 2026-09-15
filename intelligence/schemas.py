"""Typed shapes for everything that crosses the C++/Python boundary.

These mirror the JSON the sysintel.exe CLI emits exactly. Pydantic validates
on the way in, so a shape mismatch (a renamed field, a missing key) fails
loudly here instead of surfacing as a confusing bug three layers up in the
agent's reasoning.
"""
from __future__ import annotations

from typing import Literal, Optional

from pydantic import BaseModel


class BatterySnapshot(BaseModel):
    present: bool
    on_ac_power: bool = False
    charging: bool = False
    charge_percent: int = -1
    rate_watts: Optional[float] = None


class CpuSnapshot(BaseModel):
    utilization_percent: float


class MemorySnapshot(BaseModel):
    total_bytes: int
    available_bytes: int
    load_percent: int


class ProcessInfo(BaseModel):
    pid: int
    name: str
    working_set_bytes: int


class SystemSnapshot(BaseModel):
    battery: BatterySnapshot
    cpu: CpuSnapshot
    memory: MemorySnapshot
    top_processes_by_memory: list[ProcessInfo]


class MetricHistory(BaseModel):
    metric: str
    last_minutes: int
    count: int
    min: Optional[float] = None
    avg: Optional[float] = None
    max: Optional[float] = None


class Incident(BaseModel):
    id: str
    domain: str
    status: str
    severity: str
    started_at_ms: int
    resolved_at_ms: int
    trigger_type: str
    observed_value: float
    baseline_mean: float


BatteryCheckResultName = Literal[
    "not_enough_history",
    "no_recent_discharge",
    "normal",
    "anomaly_opened",
    "anomaly_ongoing",
    "resolved",
]


class BatteryCheckReport(BaseModel):
    result: BatteryCheckResultName
    live_mean_watts: float
    baseline_mean_watts: float
    baseline_stddev_watts: float
    threshold_watts: float
    incident: Optional[Incident] = None
