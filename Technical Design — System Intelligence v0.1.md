# System Intelligence v0.1 — Technical Design

## 1. Objective

Build the first functional version of an OS-native Windows intelligence layer capable of:

1. continuously observing the machine,
2. maintaining recent machine state,
3. establishing basic behavioral baselines,
4. detecting abnormal battery consumption,
5. launching a structured diagnostic investigation,
6. explaining the likely cause with evidence,
7. presenting the result through native Windows UI.

The first complete scenario is:

> **Detect and diagnose abnormal battery drain on a Windows laptop.**

The architecture must be reusable later for CPU, memory, storage, thermal, networking and developer-environment diagnostics.

---

# 2. Core Architectural Decision

The application will consist of **three separate processes**.

```text
┌─────────────────────────────────────────────────────────┐
│                       Windows                           │
│                                                         │
│   ┌───────────────────────────────────────────────┐     │
│   │       SystemIntelligenceService.exe           │     │
│   │                                               │     │
│   │  Windows Service                              │     │
│   │                                               │     │
│   │  • telemetry                                  │     │
│   │  • machine state                              │     │
│   │  • history                                    │     │
│   │  • anomaly detection                          │     │
│   │  • diagnostic tools                           │     │
│   └───────────────────┬───────────────────────────┘     │
│                       │                                 │
│                 Local IPC                               │
│                       │                                 │
│            ┌──────────▼────────────┐                    │
│            │ IntelligenceHost     │                    │
│            │                       │                    │
│            │ Python                │                    │
│            │                       │                    │
│            │ • reasoning agent     │                    │
│            │ • hypotheses          │                    │
│            │ • diagnosis           │                    │
│            │ • LLM integration     │                    │
│            └──────────┬────────────┘                    │
│                       │                                 │
│                 Local IPC                               │
│                       │                                 │
│            ┌──────────▼────────────┐                    │
│            │ SystemIntelligenceUI │                    │
│            │                       │                    │
│            │ C# / WinUI 3          │                    │
│            │                       │                    │
│            │ • tray                │                    │
│            │ • notifications       │                    │
│            │ • diagnosis UI        │                    │
│            │ • command overlay     │                    │
│            └───────────────────────┘                    │
└─────────────────────────────────────────────────────────┘
```

This separation is intentional.

The Windows Service owns the machine.

The Python intelligence process owns reasoning.

The WinUI application owns interaction.

---

# 3. Why Three Processes?

## System service

Needs:

- Windows APIs
- ETW
- performance counters
- power APIs
- long-running operation
- possibly elevated privileges

It should be deterministic and relatively small.

---

## Intelligence host

LLMs and experimental ML logic should **not live inside the privileged Windows service**.

If the reasoning layer crashes, leaks memory, or generates malformed output, system telemetry continues functioning.

The agent receives only structured tools.

---

## UI

The UI should run under the logged-in user's security context rather than LocalSystem.

This means:

```text
System service
≠
desktop UI
```

which is the normal safe architecture for Windows services.

---

# 4. Technology Stack

### Core service

```text
Language: C++20
Platform: Win32
Build: CMake
Compiler: MSVC
```

I would use C++ rather than Rust for v0.1 primarily because the Windows diagnostics ecosystem and Microsoft examples map extremely directly onto Win32/C++.

Rust remains a valid alternative later.

---

### Intelligence host

```text
Python 3.12+
Pydantic
SQLite access
LLM SDK
asyncio
```

---

### UI

```text
C#
.NET
WinUI 3
Windows App SDK
```

Microsoft currently recommends WinUI 3 for new native Windows desktop applications.

---

### Persistence

```text
SQLite
```

One database initially.

No vector database.

No graph database.

No Kafka.

No Redis.

We do not need any of those yet.

---

# 5. Repository Layout

```text
system-intelligence/
│
├── core/
│   │
│   ├── service/
│   │   ├── main.cpp
│   │   ├── service.cpp
│   │   ├── service.hpp
│   │   └── lifecycle.cpp
│   │
│   ├── collectors/
│   │   ├── battery/
│   │   ├── cpu/
│   │   ├── memory/
│   │   ├── disk/
│   │   ├── network/
│   │   ├── process/
│   │   └── gpu/
│   │
│   ├── events/
│   │   └── etw/
│   │
│   ├── state/
│   │   ├── system_state.hpp
│   │   └── state_store.cpp
│   │
│   ├── anomalies/
│   │   ├── detector.hpp
│   │   └── battery_detector.cpp
│   │
│   ├── tools/
│   │   ├── battery_tools.cpp
│   │   ├── process_tools.cpp
│   │   ├── gpu_tools.cpp
│   │   └── power_tools.cpp
│   │
│   ├── storage/
│   │   ├── sqlite_store.cpp
│   │   └── migrations/
│   │
│   └── ipc/
│       ├── pipe_server.cpp
│       └── protocol.hpp
│
├── intelligence/
│   ├── main.py
│   ├── agent/
│   ├── diagnosis/
│   ├── hypotheses/
│   ├── evidence/
│   ├── tools/
│   └── schemas/
│
├── ui/
│   ├── App.xaml
│   ├── MainWindow.xaml
│   ├── Views/
│   ├── Notifications/
│   ├── Tray/
│   └── IPC/
│
├── shared/
│   └── schemas/
│
├── tests/
│
├── scripts/
│
└── docs/
```

---

# 6. Windows Service

The service will be installed as:

```text
SystemIntelligenceService
```

The Windows Service Control Manager will own its lifecycle.

Windows services are appropriate for long-running system daemons and can also use trigger-start behavior later if we want parts of the platform to wake only in response to system events.

For v0.1:

```text
startup type = Automatic
```

Service boot:

```text
ServiceMain()
       │
       ├── initialize logging
       ├── open SQLite
       ├── initialize collectors
       ├── start ETW consumer
       ├── start sampler
       ├── start anomaly engine
       └── start IPC server
```

---

# 7. Internal Service Threads

Do not create one giant loop.

Use approximately:

```text
Main Service Thread
│
├── Collector Scheduler
│
├── ETW Consumer Thread
│
├── State Aggregator
│
├── Persistence Worker
│
├── Anomaly Engine
│
└── IPC Server
```

Conceptually:

```text
             telemetry samples
                    │
                    ▼
Collectors ──→ Event Queue
                    │
                    ▼
              State Aggregator
                /          \
               /            \
              ▼              ▼
       Current State      SQLite
              │
              ▼
       Anomaly Engine
              │
         anomaly found
              │
              ▼
       Diagnostic Event
```

---

# 8. Unified Telemetry Representation

Do not let every collector invent its own downstream interface.

Every metric becomes:

```cpp
struct MetricSample {
    std::string metric;
    double value;
    std::string unit;

    std::chrono::system_clock::time_point timestamp;

    std::unordered_map<std::string, std::string> labels;
};
```

Example:

```json
{
  "metric": "battery.discharge_power",
  "value": 17.4,
  "unit": "W",
  "timestamp": "2026-09-15T18:24:10.312Z",
  "labels": {
    "battery": "BAT0"
  }
}
```

CPU:

```json
{
  "metric": "cpu.utilization",
  "value": 13.7,
  "unit": "percent"
}
```

GPU:

```json
{
  "metric": "gpu.utilization",
  "value": 41.0,
  "unit": "percent",
  "labels": {
    "adapter": "NVIDIA RTX 4060"
  }
}
```

This single decision will make the rest of the architecture much easier.

---

# 9. Sampling Rates

Different metrics need different sampling frequencies.

Do not poll everything every second.

Initial values:

| Signal | Sampling |
|---|---:|
| Battery state | 5 s |
| Battery power | 5 s |
| CPU utilization | 2 s |
| Memory | 5 s |
| Disk throughput | 2 s |
| Network throughput | 2 s |
| GPU utilization | 2 s |
| Process CPU | 5 s |
| Installed drivers | event/on-demand |
| Windows Update history | on-demand |
| Battery report | investigation only |
| Sleep Study | investigation only |

This keeps normal overhead small.

---

# 10. Battery Collector

Battery information should come from multiple layers.

## Basic state

Use:

```cpp
GetSystemPowerStatus()
```

which exposes AC state, battery percentage and estimated battery life through `SYSTEM_POWER_STATUS`.

Use it for:

```text
AC connected?
battery %
charging?
estimated remaining time
```

---

## Battery capacity/state

Use:

```text
CallNtPowerInformation(
    SystemBatteryState
)
```

to retrieve `SYSTEM_BATTERY_STATE`.

Windows exposes fields including:

```text
MaxCapacity
RemainingCapacity
Rate
EstimatedTime
```

and importantly `Rate` can represent charge or discharge rate in milliwatts.

That gives us:

```text
battery.discharge_power
```

without estimating it indirectly.

Convert:

```text
-14500 mW
```

into:

```text
14.5 W discharge
```

---

## WMI fallback/enrichment

`Win32_Battery` also exposes properties such as charge remaining, estimated runtime, design capacity and full-charge capacity where hardware/providers make them available.

We should treat some of these fields as optional because OEM support varies.

---

# 11. CPU Collector

For v0.1 use Windows Performance Counters through **PDH**.

Microsoft specifically recommends PDH for most C/C++ performance counter collection because it handles counter querying and formatting for you.

Initial counters:

```text
\Processor(_Total)\% Processor Time

\System\Processor Queue Length
```

Later:

```text
\Process(*)\% Processor Time
```

We can add process-level CPU separately.

---

# 12. Memory Collector

Collect:

```text
total physical memory
available physical memory
memory load
page activity
```

Use:

```cpp
GlobalMemoryStatusEx()
```

plus performance counters such as:

```text
\Memory\Available MBytes
\Memory\Pages/sec
```

Derived metric:

```text
memory.pressure
```

Example classification:

```text
<70% used      normal
70–85%         elevated
85–95%         high
>95%           critical
```

These are initial operational thresholds, not permanent universal truths.

---

# 13. Process Collector

We need to know:

```text
PID
name
executable path
start time
CPU usage
memory usage
parent PID
```

Initial methods:

```text
CreateToolhelp32Snapshot
Process32First
Process32Next
OpenProcess
GetProcessMemoryInfo
QueryFullProcessImageName
```

Store a lightweight snapshot.

Example:

```json
{
  "pid": 8424,
  "parent_pid": 3400,
  "name": "chrome.exe",
  "path": "C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe",
  "working_set_mb": 613.4
}
```

---

# 14. ETW Layer

Polling tells us state.

ETW tells us **what changed**.

ETW supports real-time event consumption and is specifically designed for application and OS tracing/performance analysis.

Our ETW consumer should eventually observe:

```text
process start
process stop
image/module loading
disk I/O
network activity
power events
selected kernel events
```

ETW architecture:

```text
Provider
   ↓
ETW Session
   ↓
Consumer
   ↓
Normalized SystemEvent
```

The service can create a trace session using APIs such as `StartTrace` and enable providers through the ETW controller APIs.

But:

### Important v0.1 decision

Do **not** start by integrating every ETW provider.

Initially implement only:

```text
process start
process stop
```

Then grow it.

ETW can become a project by itself if we are not disciplined.

---

# 15. Normalized Event Schema

Everything event-driven becomes:

```cpp
struct SystemEvent {
    std::string type;
    Timestamp timestamp;

    std::unordered_map<std::string, Value> data;
};
```

Example:

```json
{
  "type": "process.started",
  "timestamp": "2026-09-15T18:22:13Z",
  "data": {
    "pid": 9404,
    "name": "chrome.exe",
    "parent_pid": 1880
  }
}
```

Another:

```json
{
  "type": "power.ac_disconnected",
  "timestamp": "2026-09-15T18:25:18Z"
}
```

This becomes the vocabulary for the future system graph.

---

# 16. Current System State

The service maintains an in-memory structure:

```cpp
struct SystemState {

    BatteryState battery;

    CpuState cpu;

    MemoryState memory;

    DiskState disk;

    NetworkState network;

    std::vector<GpuState> gpus;

    std::unordered_map<uint32_t, ProcessState> processes;

    Timestamp updated_at;
};
```

The agent should never need to reconstruct current state by querying ten collectors independently.

It asks:

```text
get_system_snapshot()
```

and gets one coherent snapshot.

---

# 17. SQLite Data Model

Start with only a few tables.

```sql
CREATE TABLE metric_samples (
    id INTEGER PRIMARY KEY,
    ts INTEGER NOT NULL,
    metric TEXT NOT NULL,
    value REAL NOT NULL,
    unit TEXT
);
```

Indexes:

```sql
CREATE INDEX idx_metric_ts
ON metric_samples(metric, ts);
```

Events:

```sql
CREATE TABLE system_events (
    id INTEGER PRIMARY KEY,
    ts INTEGER NOT NULL,
    type TEXT NOT NULL,
    payload_json TEXT NOT NULL
);
```

Incidents:

```sql
CREATE TABLE incidents (
    id TEXT PRIMARY KEY,
    domain TEXT NOT NULL,
    status TEXT NOT NULL,
    severity TEXT,
    started_at INTEGER NOT NULL,
    resolved_at INTEGER,
    trigger TEXT
);
```

Evidence:

```sql
CREATE TABLE evidence (
    id INTEGER PRIMARY KEY,
    incident_id TEXT NOT NULL,
    kind TEXT NOT NULL,
    source TEXT NOT NULL,
    payload_json TEXT NOT NULL,
    created_at INTEGER NOT NULL
);
```

Diagnosis:

```sql
CREATE TABLE diagnoses (
    incident_id TEXT PRIMARY KEY,
    conclusion TEXT,
    confidence REAL,
    explanation TEXT,
    created_at INTEGER
);
```

That's enough.

---

# 18. Data Retention

Do not permanently store every 2-second metric.

Initial policy:

```text
Raw high-resolution telemetry:
24 hours

1-minute aggregates:
30 days

Incident evidence:
indefinite until user deletes

Diagnostic reports:
30 days
```

Later we can make this configurable.

---

# 19. Fast Brain

The fast brain runs inside the native service.

No LLM.

It evaluates:

```text
rules
rolling averages
variance
baseline deviations
```

For battery:

Maintain:

```text
5-minute moving average discharge
30-minute moving average discharge
contextual baseline
```

Initial anomaly logic:

```text
if:
    on_battery
    AND samples >= minimum_samples
    AND avg_power_5m > max(
        baseline_mean + 2.5 * baseline_std,
        baseline_mean * 1.5
    )

then:
    trigger battery.drain_anomaly
```

We can improve the statistical model later.

---

# 20. Contextual Baselines

Do not compare gaming against idle.

Initial context dimensions:

```text
AC/Battery
foreground application category
GPU active/inactive
CPU bucket
```

A baseline key might be:

```text
battery | light-workload | integrated-gpu
```

with:

```json
{
  "mean_watts": 8.7,
  "std_watts": 1.4,
  "samples": 2180
}
```

For v0.1, though, start even simpler:

```text
battery + low CPU usage
```

and evolve from there.

---

# 21. Incident Creation

When anomaly detection fires:

```text
battery.drain_anomaly
```

create:

```json
{
  "incident_id": "BAT-20260915-001",
  "domain": "battery",
  "trigger": "automatic",
  "observed_power": 21.4,
  "baseline_power": 8.7,
  "severity": "high"
}
```

Then notify the Intelligence Host:

```text
NEW_INCIDENT
```

This is where the AI wakes up.

---

# 22. IPC

Use **local named pipes**.

Windows named pipes support duplex communication between separate processes and can enforce security descriptors.

Pipe:

```text
\\.\pipe\SystemIntelligence.Core
```

Use message-mode duplex communication:

```text
PIPE_TYPE_MESSAGE
PIPE_READMODE_MESSAGE
PIPE_ACCESS_DUPLEX
```

Windows supports message-oriented named pipes directly.

---

# 23. IPC Security

This is important.

Do **not** use the default ACL.

The default named-pipe security descriptor can grant read access more broadly than we want.

Explicitly allow only:

```text
SYSTEM
Administrators
currently authorized interactive user
```

and explicitly deny remote/network usage.

Windows documentation notes that named pipes may otherwise be accessible remotely depending on configuration; for our use case this must be local-only.

---

# 24. IPC Protocol

Use JSON initially.

Request:

```json
{
  "version": 1,
  "request_id": "6e58...",
  "method": "battery.snapshot",
  "params": {}
}
```

Response:

```json
{
  "version": 1,
  "request_id": "6e58...",
  "ok": true,
  "result": {
    "charge_percent": 64,
    "discharge_watts": 18.7,
    "estimated_seconds": 9240
  }
}
```

Later we can replace JSON with protobuf if necessary.

There is absolutely no need initially.

---

# 25. Agent Tool Contract

This boundary is critical.

The Python agent cannot run:

```python
subprocess.run(...)
```

against arbitrary model-generated commands.

Instead, it receives typed tools.

For example:

```python
class BatterySnapshot(BaseModel):
    charge_percent: float
    discharge_watts: float | None
    full_capacity_wh: float | None
    estimated_runtime_seconds: int | None
    on_ac_power: bool
```

Tool:

```python
async def get_battery_snapshot() -> BatterySnapshot:
    ...
```

Another:

```python
async def get_top_processes(
    sort_by: Literal["cpu", "memory"],
    limit: int = 10
) -> list[ProcessInfo]:
    ...
```

---

# 26. Initial Tool Set

The reasoning agent gets only:

```text
get_system_snapshot

get_battery_snapshot

get_battery_history

get_cpu_history

get_memory_status

get_top_cpu_processes

get_top_memory_processes

get_process_details

get_recent_process_events

get_gpu_status

get_gpu_processes

get_power_configuration

generate_battery_report

generate_energy_report

get_recent_driver_changes
```

No mutation tools yet.

v0.1 should **diagnose only**.

---

# 27. Agent Architecture

Do not start with multiple agents.

Use one diagnostic agent with a state machine.

```text
Incident
   ↓
TRIAGE
   ↓
GENERATE HYPOTHESES
   ↓
COLLECT EVIDENCE
   ↓
EVALUATE
   ↓
Need more evidence? ─── yes ─┐
   │                         │
   no                        │
   ↓                         │
DIAGNOSIS ◄──────────────────┘
   ↓
EXPLAIN
```

---

# 28. Hypothesis Representation

Do not keep hypotheses only in free-form model text.

Represent them structurally.

```python
class Hypothesis(BaseModel):
    id: str
    description: str

    confidence: float

    supporting_evidence: list[str]
    contradicting_evidence: list[str]

    next_tests: list[str]
```

Example:

```json
{
  "id": "H2",
  "description": "Discrete GPU activity is causing elevated battery drain",
  "confidence": 0.61,
  "supporting_evidence": [
    "GPU utilization elevated"
  ],
  "contradicting_evidence": [],
  "next_tests": [
    "get_gpu_processes",
    "get_gpu_status"
  ]
}
```

This gives you inspectable reasoning without relying on hidden model reasoning.

---

# 29. Battery Diagnostic Workflow

Initial flow:

```text
Battery anomaly
       │
       ▼
get_battery_snapshot
       │
       ▼
get_system_snapshot
       │
       ├────────────┐
       ▼            ▼
 CPU analysis    GPU analysis
       │            │
       └─────┬──────┘
             ▼
      process analysis
             │
             ▼
     power configuration
             │
             ▼
      recent changes
             │
             ▼
      hypothesis ranking
             │
             ▼
         diagnosis
```

---

# 30. Example Investigation

Detected:

```text
Observed discharge:
22.1 W

Baseline:
8.9 W
```

Agent calls:

```text
get_system_snapshot()
```

Response:

```text
CPU 11%
Memory 64%
GPU 38%
```

Now GPU becomes suspicious.

Call:

```text
get_gpu_processes()
```

Returns:

```text
chrome.exe
```

Call:

```text
get_gpu_status()
```

Returns:

```text
GPU:
NVIDIA RTX 4060

Power:
13.6 W
```

Now evidence:

```text
Total excess power ≈ 13 W

GPU power ≈ 13.6 W
```

Hypothesis confidence rises substantially.

Diagnosis:

```text
Likely cause:
Discrete GPU remaining active because of Chrome.
```

---

# 31. Evidence Graph

For the first version, do **not** build Neo4j.

Represent evidence relationships in memory.

```text
BATTERY_DRAIN
      │
      ├── CPU_NORMAL
      │
      ├── MEMORY_NORMAL
      │
      └── GPU_HIGH_POWER
                 │
                 └── chrome.exe
```

Internally:

```python
class EvidenceNode:
    id: str
    type: str
    data: dict

class EvidenceEdge:
    source: str
    relation: str
    target: str
```

Persist it as JSON if needed.

Later this can become the true machine knowledge graph.

---

# 32. Confidence

Do not ask the LLM to invent arbitrary confidence percentages.

Initially calculate confidence from evidence classes.

Example:

```text
Weak correlation          +0.15
Strong temporal relation  +0.20
Unique responsible proc   +0.25
Power explains anomaly    +0.25
Controlled intervention   +0.15
```

Maximum:

```text
1.0
```

Then the model can explain the confidence, but does not manufacture it.

---

# 33. WinUI Application

Main UI should initially contain three pages.

```text
Health

Incidents

Settings
```

Health:

```text
System Intelligence

Battery        Healthy
CPU            Healthy
Memory         Healthy
Disk           Healthy
Network        Healthy
```

Incident:

```text
Battery consumption unusually high

22.1 W observed
8.9 W expected

Likely cause

Chrome is keeping the
NVIDIA GPU active.

Confidence
High

Evidence

GPU power       13.6 W
CPU usage       normal
Chrome          active on dGPU
```

---

# 34. Notifications

Use the Windows App SDK notification APIs through `AppNotificationManager`, which Microsoft recommends for new WinUI 3 applications.

Example:

```text
System Intelligence

Battery usage is 2.4× higher
than normal.

Likely cause identified:
Chrome / discrete GPU

[View diagnosis]
```

The Windows Service should **not directly own the desktop notification experience**.

Send the incident to the user's WinUI process.

---

# 35. Manual Invocation

Global shortcut:

```text
Win + Shift + Space
```

Opens:

```text
┌──────────────────────────────────┐
│ Ask about your PC                │
│                                  │
│ Why is my battery draining?      │
└──────────────────────────────────┘
```

The command becomes:

```json
{
  "type": "user_diagnosis_request",
  "domain": "battery",
  "query": "Why is my battery draining?"
}
```

Same investigation engine.

The user-triggered and automatic systems should not be separate architectures.

---

# 36. Logging

Use structured logging from day one.

Example:

```json
{
  "timestamp": "...",
  "level": "info",
  "component": "battery_collector",
  "event": "sample",
  "discharge_watts": 8.4
}
```

Never rely exclusively on:

```cpp
std::cout
```

because once this is running as a service, debugging otherwise becomes painful.

---

# 37. Service Resource Budget

Set explicit targets.

Normal monitoring:

```text
CPU:
< 1%

RAM:
< 100 MB native service

Disk writes:
batched

LLM activity:
zero during normal system state
```

The UI should not need to be running continuously.

---

# 38. Privacy Boundary

The collector should gather:

```text
process name
resource usage
device information
system configuration
OS events
```

Not:

```text
browser tab contents
document contents
keyboard contents
email
messages
screen contents
```

unless a completely separate user-approved feature is built later.

System diagnosis generally does not require user-content surveillance.

---

# 39. Security Model

The architecture should eventually become:

```text
                  USER
                    │
                    ▼
              WinUI process
                    │
             authenticated IPC
                    │
                    ▼
         Intelligence Host
                    │
            structured tools
                    │
                    ▼
      System Intelligence Service
                    │
          restricted capabilities
                    │
                    ▼
                 Windows
```

The LLM never receives:

```text
cmd.exe
PowerShell
CreateProcess arbitrary string
registry write arbitrary key
filesystem arbitrary write
```

Instead:

```text
inspect_process(pid)
```

and eventually:

```text
terminate_process(pid)
```

are explicit capabilities.

---

# 40. What NOT to Build Yet

Do not implement yet:

```text
multi-agent architecture

Neo4j

vector database

automatic driver rollback

registry modifications

autonomous process termination

local LLM

voice interface

screen understanding

full RAG system

Linux support

macOS support

security monitoring

malware detection

GPU tuning
```

All of those are seductive distractions.

---

# 41. Development Sequence

## Phase 1 — Native system observer

Build:

```text
Windows Service

Battery collector
CPU collector
Memory collector
Process collector

CLI debugging client
```

Expected:

```text
> sysintel status

Power
  Source          Battery
  Charge          72%
  Discharge       8.7 W

CPU
  Utilization     14.2%

Memory
  Used            10.1 GB
  Available       5.7 GB

Processes
  chrome.exe      4.1%
  Code.exe        2.6%
```

No AI.

---

## Phase 2 — History

Add:

```text
SQLite
metric samples
events
1-minute aggregation
```

Then:

```text
> sysintel battery --last 30m

Mean       8.6 W
Min        6.9 W
Max        11.2 W
```

---

## Phase 3 — Anomaly engine

Create:

```text
battery baseline
battery anomaly detector
incident creation
```

Test:

```text
Baseline:
8 W

Artificial load:
22 W

→ BATTERY_DRAIN incident
```

---

## Phase 4 — Agent tools

Create Python client.

Expose:

```text
get_system_snapshot
get_battery_history
get_top_processes
get_gpu_status
```

Then let the model conduct a diagnosis.

---

## Phase 5 — Native UX

Build:

```text
WinUI
notifications
incident page
tray
```

Now the product begins feeling like part of Windows.

---

## Phase 6 — GPU attribution

This is necessary for the killer battery demo.

Integrate vendor telemetry where appropriate, initially NVIDIA/NVML if available.

Then the agent can correlate:

```text
battery drain
       ↓
GPU power
       ↓
GPU process
```

---

# 42. First End-to-End Acceptance Test

Machine starts:

```text
8.1 W battery usage
```

System learns baseline.

Launch an application that activates the discrete GPU.

Consumption becomes:

```text
21.3 W
```

Within an appropriate observation window:

```text
System Intelligence detects anomaly
```

Incident created.

Agent receives:

```text
BAT-001
```

Agent investigates.

Finds:

```text
CPU           normal
Memory        normal

GPU           13.4 W
chrome.exe    using dGPU
```

Agent returns:

```text
Finding

The discrete NVIDIA GPU is responsible
for most of the additional power draw.

Chrome is the active application using it.

Evidence

Expected system power       8.1 W
Observed                    21.3 W
GPU power                   13.4 W
```

Windows notification appears.

User did not:

```text
open Task Manager
run powercfg
open NVIDIA Control Panel
paste logs into ChatGPT
```

The machine diagnosed itself.

That is **v0.1 success**.

---

# 43. Where the System Goes Next

Once this architecture works, the same framework generalizes naturally.

Battery:

```text
Why is my battery draining?
```

CPU:

```text
Why are my fans running?
```

Memory:

```text
Why is my computer becoming slow?
```

Disk:

```text
Why is everything freezing intermittently?
```

Network:

```text
Why does Wi-Fi keep dropping?
```

Developer:

```text
Why can't PyTorch see my GPU?
```

Each becomes another diagnostic domain built on top of:

```text
Collectors
     ↓
State
     ↓
History
     ↓
Anomalies
     ↓
Tools
     ↓
Reasoning
     ↓
Evidence
```

The architecture remains the same.

---

# 44. The Most Important Architectural Principle

The intelligence hierarchy should be:

```text
             LLM
              ▲
              │
         investigation
              │
        structured tools
              ▲
              │
      system state engine
              ▲
              │
    deterministic collectors
              ▲
              │
            Windows
```

not:

```text
LLM
 │
 └── runs random PowerShell commands
```

That distinction is what turns this from an LLM wrapper into an actual systems project.

The first component to implement should therefore be:

> **`SystemIntelligenceService.exe` + BatteryCollector + CPUCollector + MemoryCollector + a CLI client.**

Once that works reliably, every intelligent capability has something real to reason about.