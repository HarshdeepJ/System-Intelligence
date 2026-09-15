# Product Requirements Document

## Product Name

**System Intelligence**

Working description:

> An OS-native AI agent that continuously understands the state, configuration, history, and behavior of a computer, detects abnormal conditions, investigates their causes, explains the evidence, and safely recommends or executes corrective actions.

---

# 1. Product Vision

Modern operating systems expose an enormous amount of information through logs, processes, performance counters, drivers, configuration files, diagnostic utilities, and system APIs.

However, troubleshooting still requires the user to understand tools such as:

- Task Manager
- Event Viewer
- Device Manager
- PowerShell
- `powercfg`
- Performance Monitor
- driver utilities
- system logs
- hardware monitoring tools

The operating system possesses most of the information required to diagnose its own problems, but it lacks an intelligence layer capable of combining that information into an understandable diagnosis.

System Intelligence aims to provide that layer.

The product should behave less like a chatbot installed on the computer and more like an intelligent subsystem of the operating system.

The long-term goal is:

> **A computer that can understand its own condition, investigate its own problems, and assist the user in maintaining and operating it.**

---

# 2. Core Product Principles

System Intelligence should follow five primary principles.

## 2.1 Observe before reasoning

The agent should obtain evidence from the actual machine rather than relying primarily on generic troubleshooting knowledge.

## 2.2 Diagnose before recommending

The system should attempt to determine why a problem is occurring rather than immediately suggesting common fixes.

## 2.3 Evidence over speculation

Every diagnosis should identify the observations that led to the conclusion.

## 2.4 Safe autonomy

The system may investigate automatically, but potentially disruptive actions should require appropriate authorization.

## 2.5 Invisible until useful

The agent should run primarily as a background system capability.

Users should not need to constantly interact with a large chatbot interface.

---

# 3. Target Platform

The initial version will target:

**Microsoft Windows 11**

Windows is selected because it provides access to several useful observability and management interfaces:

- Win32 APIs
- Event Tracing for Windows
- Windows Management Instrumentation
- Windows Performance Counters
- Windows Event Logs
- Power APIs
- `powercfg`
- Service Control Manager
- Registry
- device-management APIs

The architecture should remain modular enough to support Linux and potentially macOS later.

---

# 4. Target Users

## 4.1 General users

Users experiencing issues such as:

- poor battery life
- overheating
- slow performance
- applications freezing
- high memory usage
- network instability
- storage exhaustion
- unusually noisy fans

The user should not need technical knowledge to diagnose these issues.

---

## 4.2 Developers

Developers may use the system to investigate:

- excessive CPU usage
- memory leaks
- CUDA installation problems
- GPU underutilization
- environment conflicts
- dependency problems
- disk bottlenecks
- networking issues
- background development services

---

## 4.3 Power users

Power users may use System Intelligence as a unified observability and control layer over their computer.

---

# 5. Primary User Problem

Today, a user may notice:

> "My battery suddenly started draining quickly."

Diagnosing this could require checking:

- battery health
- Task Manager
- GPU utilization
- power settings
- recent driver updates
- Windows Sleep Study
- application GPU assignment
- active services
- display settings
- peripheral activity

The user often lacks both the knowledge and context required to correlate this information.

System Intelligence should instead automatically investigate:

```text
Symptom
    ↓
Collect evidence
    ↓
Generate hypotheses
    ↓
Test hypotheses
    ↓
Identify likely root cause
    ↓
Explain evidence
    ↓
Recommend or execute fix
    ↓
Verify result
```

---

# 6. Product Experience

There should be three ways the system begins an investigation.

## 6.1 User-triggered diagnosis

Example:

> Why is my laptop battery draining so quickly?

The agent immediately begins collecting relevant system information.

---

## 6.2 Event-triggered investigation

An observable event triggers an investigation.

Example:

```text
Battery discharge normally:
7–10 W

Current discharge:
24 W
```

The anomaly detector automatically initiates diagnosis.

---

## 6.3 Proactive diagnosis

The system notices a longer-term behavioral change.

Example:

```text
Typical battery runtime:
7h 20m

Current estimated runtime:
4h 15m
```

System Intelligence generates a notification:

> Battery usage has increased significantly compared with your normal usage. I found unusual discrete GPU activity. Investigate?

---

# 7. Interaction Model

The product should avoid behaving primarily as a standalone chat application.

The system should expose several native interaction surfaces.

## 7.1 Global command interface

A keyboard shortcut opens a compact overlay.

Example:

```text
Win + Shift + Space
```

Overlay:

```text
┌─────────────────────────────────────┐
│ Ask about this PC...                │
│                                     │
│ Why is my laptop hot?               │
└─────────────────────────────────────┘
```

---

## 7.2 Notifications

Example:

```text
Battery consumption is 2.3× higher
than normal.

Likely cause:
NVIDIA GPU remains active while idle.

[Investigate]     [Ignore]
```

---

## 7.3 System tray

The tray application provides lightweight system status.

Example:

```text
System Intelligence

✓ CPU
✓ GPU
⚠ Battery
✓ Memory
✓ Network

1 issue detected
```

---

## 7.4 Diagnostic view

When more information is required, the user can open a native WinUI interface.

Example:

```text
Battery Drain Investigation

Observed power
22.8 W

Expected power
8.9 W

Likely cause
Chrome is keeping the discrete GPU active.

Confidence
93%

Evidence
• GPU package power increased to 14.2 W
• Chrome is the only process using the GPU
• Suspending Chrome reduced system power to 9.1 W

[Fix]
[More details]
```

---

# 8. System Architecture

The initial architecture will contain five major components.

```text
                 Windows
                    │
        ┌───────────▼────────────┐
        │   System Collectors    │
        │                        │
        │ ETW / WMI / Win32      │
        │ Perf Counters / Logs   │
        └───────────┬────────────┘
                    │
                    ▼
        ┌────────────────────────┐
        │   System State Layer   │
        │                        │
        │ Current state          │
        │ Historical state       │
        │ System relationships   │
        └───────────┬────────────┘
                    │
                    ▼
        ┌────────────────────────┐
        │      Fast Brain        │
        │                        │
        │ Rules                  │
        │ Baselines              │
        │ Anomaly detection      │
        └───────────┬────────────┘
                    │
               anomaly/event
                    │
                    ▼
        ┌────────────────────────┐
        │    Reasoning Agent     │
        │                        │
        │ Hypotheses             │
        │ Investigation          │
        │ Evidence reasoning     │
        └───────────┬────────────┘
                    │
                    ▼
        ┌────────────────────────┐
        │      Action Layer      │
        │                        │
        │ Safe tools             │
        │ Approval               │
        │ Rollback               │
        └────────────────────────┘
```

---

# 9. Windows Service

The core intelligence runtime should run as a Windows background service.

Working process name:

```text
SystemIntelligenceService.exe
```

Responsibilities:

- telemetry collection
- event ingestion
- state maintenance
- historical storage
- anomaly detection
- agent execution
- diagnostic tool execution
- communication with UI
- action verification

The service should continue operating even when the primary UI is closed.

---

# 10. Observability Layer

The system should collect information from several sources.

## 10.1 Event Tracing for Windows

ETW will provide event-driven telemetry.

Potential sources include:

- process creation
- process termination
- CPU scheduling
- disk I/O
- network activity
- system events
- service events
- power-related events
- application crashes

---

## 10.2 Windows Management Instrumentation

WMI can provide machine configuration and hardware information.

Examples:

```text
Win32_Processor
Win32_Battery
Win32_Process
Win32_Service
Win32_LogicalDisk
Win32_VideoController
Win32_OperatingSystem
```

---

## 10.3 Performance Counters

Used for continuously sampled metrics.

Examples:

- CPU utilization
- available memory
- page faults
- disk throughput
- disk queue
- network throughput

---

## 10.4 Native Windows APIs

Used where direct system access is required.

Examples:

- power state
- battery state
- process management
- service management
- registry queries
- device information
- security information

---

## 10.5 Existing diagnostic utilities

The system may invoke existing Windows tools through controlled wrappers.

Examples:

```text
powercfg /batteryreport
powercfg /energy
powercfg /sleepstudy
powercfg /requests
```

The resulting output should be converted into structured evidence.

---

# 11. System State Model

System Intelligence should maintain a continuously updated representation of the machine.

Example:

```text
System
│
├── Hardware
│   ├── CPU
│   ├── GPU
│   ├── Memory
│   ├── Battery
│   ├── Storage
│   └── Network
│
├── Processes
│
├── Applications
│
├── Services
│
├── Drivers
│
├── Devices
│
├── Configuration
│
├── Events
│
└── Historical State
```

---

# 12. System Knowledge Graph

The system should eventually maintain relationships between components.

Example:

```text
chrome.exe
     │
     │ uses
     ▼
NVIDIA GPU
     │
     │ causes
     ▼
high GPU power state
     │
     ▼
battery discharge
```

Another example:

```text
Driver update
     │
     ▼
Wi-Fi adapter
     │
     ▼
connection resets
```

The graph allows the reasoning agent to investigate relevant dependencies rather than blindly scanning the entire machine.

---

# 13. Temporal System History

A major feature should be maintaining a timeline of important machine changes.

Events may include:

- driver installation
- Windows updates
- application installation
- configuration changes
- service changes
- hardware state changes
- unusual resource spikes
- crashes

Example:

```text
September 11
Battery idle power: 8.1 W

September 12 18:41
NVIDIA driver updated

September 13
Battery idle power: 19.7 W
```

The reasoning agent should be able to identify correlations between system changes and emerging symptoms.

---

# 14. Fast Brain

The Fast Brain operates continuously without requiring an LLM.

Responsibilities:

- telemetry monitoring
- simple rules
- rolling statistics
- baseline generation
- anomaly detection
- event correlation

Example:

```text
Normal battery discharge:
μ = 8.2 W
σ = 1.4 W

Observed:
21.3 W
```

This may trigger:

```text
battery_power_anomaly
```

---

# 15. Reasoning Agent

The reasoning agent activates when deeper investigation is necessary.

Responsibilities:

1. interpret symptom or event
2. generate hypotheses
3. request evidence
4. eliminate hypotheses
5. identify probable root cause
6. estimate confidence
7. recommend intervention
8. verify result

Example:

```text
Problem:
Battery drain

Hypotheses:

H1 CPU workload
H2 discrete GPU active
H3 display configuration
H4 battery degradation
H5 driver regression
H6 background application
```

Evidence is collected until hypotheses can be accepted or rejected.

---

# 16. Diagnostic Tool Interface

The LLM must not directly invoke arbitrary operating-system commands.

Instead it receives a controlled set of tools.

Example tools:

```text
get_battery_status()

get_battery_health()

get_power_consumption()

get_cpu_usage()

get_gpu_usage()

get_gpu_processes()

get_running_processes()

inspect_process(pid)

get_recent_driver_changes()

get_recent_system_events()

get_power_plan()

generate_battery_report()

generate_sleep_study()

get_disk_usage()

get_memory_pressure()

get_network_usage()
```

Action tools may include:

```text
terminate_process()

restart_service()

change_power_plan()

change_application_gpu_preference()

disable_startup_application()
```

Every tool must define:

- required permissions
- expected effects
- reversibility
- risk level

---

# 17. Permission Model

Actions should be classified according to risk.

## Level 0 — Observe

Read-only telemetry.

Examples:

- CPU usage
- battery state
- process list

No user approval required.

---

## Level 1 — Diagnose

Run non-destructive diagnostics.

Examples:

- battery report
- Sleep Study
- process inspection

No approval normally required.

---

## Level 2 — Suggest

Agent provides a recommendation.

Example:

> Chrome appears responsible for the abnormal GPU usage.

No action is executed.

---

## Level 3 — Reversible action

Examples:

- temporarily suspend process
- change power mode
- disable startup entry

User approval may be required depending on policy.

---

## Level 4 — Privileged action

Examples:

- driver modification
- registry changes
- device disabling
- security configuration

Explicit user approval and privilege elevation required.

---

# 18. Action Broker

The reasoning agent should never directly perform privileged operations.

Architecture:

```text
Agent
  │
  ▼
Action Request
  │
  ▼
Action Broker
  │
  ├── permission check
  ├── risk classification
  ├── user approval
  ├── execution
  ├── verification
  └── rollback
```

Every action should produce an audit record.

Example:

```text
Action:
Changed Chrome GPU preference

Reason:
Discrete GPU remained active during battery use

Previous state:
High-performance GPU

New state:
Integrated GPU

Result:
System power decreased 21 W → 9 W

Rollback:
Available
```

---

# 19. Diagnostic Probes

A later capability should allow controlled experiments.

Example:

```text
Hypothesis:
Chrome is causing high GPU power.
```

Probe:

```text
Temporarily suspend Chrome
        ↓
Observe power
        ↓
22 W → 9 W
        ↓
Resume Chrome
```

This allows the system to distinguish correlation from stronger causal evidence.

Every diagnostic probe must be:

- short-lived
- reversible
- logged
- safe
- clearly identified

---

# 20. Evidence Model

Every diagnosis should contain:

```text
Finding
Confidence
Evidence
Alternative explanations
Recommended action
Risk
Expected result
```

Example:

```text
Finding:
Chrome is responsible for abnormal battery drain.

Confidence:
92%

Evidence:

1. GPU power increased from 1.4 W to 14.7 W.
2. Chrome was the only active discrete-GPU process.
3. Suspending Chrome reduced total system power from 22.1 W to 9.3 W.
4. GPU returned to its low-power state immediately after suspension.
```

The system should clearly distinguish:

- observations
- hypotheses
- conclusions

---

# 21. Machine Baselines

The system should learn the normal behavior of the specific computer.

Potential baselines include:

- idle CPU usage
- idle power consumption
- battery discharge
- GPU idle power
- memory use
- disk activity
- boot time
- fan behavior
- temperatures
- network usage

Baselines should consider context where possible.

Example:

```text
Battery + browser workload:
Normal discharge = 9 W

Charger + IDE:
Normal CPU = 18%

CUDA training:
Normal GPU utilization = 90–100%
```

---

# 22. Machine Memory

System Intelligence should remember previous system incidents.

Example:

```text
August 14

Issue:
Battery drain

Cause:
Chrome woke discrete GPU

Resolution:
Application GPU preference changed
```

Future detection:

> This resembles an issue detected on August 14.

This memory belongs to the machine rather than the user's conversational profile.

---

# 23. Context Awareness

The system may infer operating context.

Examples:

```text
Battery + browser
→ mobility context

Charger + IDE + monitor
→ workstation context

CUDA process
→ ML training context

Teams + webcam
→ meeting context

Game + discrete GPU
→ gaming context
```

Context can influence:

- anomaly thresholds
- recommendations
- expected resource use
- optimization policies

---

# 24. System Goals

Later versions may allow users to declare goals.

Example:

> Maximize battery life.

Possible constraints:

```text
Do not reduce refresh rate below 90 Hz.
Do not terminate VS Code.
Bluetooth must remain enabled.
```

The system may then optimize the machine while respecting those constraints.

Other goals could include:

- maximum performance
- quiet operation
- low temperature
- meeting mode
- gaming mode
- ML training mode

---

# 25. MVP Scope

The first version should deliberately avoid attempting to understand the entire operating system.

The MVP should support five domains:

1. Battery
2. CPU
3. Memory
4. Disk
5. Network

The first deeply implemented diagnostic workflow will focus on:

> **Abnormal battery drain**

---

# 26. MVP Battery Workflow

## Trigger

One of:

```text
User:
"Why is my battery draining?"

or

Anomaly:
Battery discharge > learned threshold
```

---

## Investigation

Collect:

```text
battery discharge rate
battery capacity
battery health
CPU utilization
GPU utilization
active GPU processes
power mode
display state
high-resource processes
recent application changes
recent driver changes
sleep information
```

---

## Hypothesis Generation

Potential hypotheses:

```text
CPU workload
GPU workload
background application
display consumption
driver problem
battery degradation
sleep issue
peripheral activity
```

---

## Diagnosis

The agent ranks hypotheses according to evidence.

Example:

```text
H1 GPU activity          0.89
H2 CPU activity          0.12
H3 Battery degradation   0.07
```

---

## Intervention

The system may recommend:

```text
Move application to integrated GPU
Close process
Disable startup activity
Change power mode
Rollback recent configuration
```

---

## Verification

After an action:

```text
Before:
21.8 W

After:
9.4 W
```

The agent confirms whether the intervention was successful.

---

# 27. Native Windows Integration

The user-facing application should use **WinUI 3**.

Components:

```text
WinUI shell
System tray
Global hotkey
Notifications
Health dashboard
Diagnostic overlay
Approval dialogs
```

The UI communicates with the background service through IPC.

Preferred initial mechanism:

```text
Named Pipes
```

Example:

```text
\\.\pipe\SystemIntelligence
```

---

# 28. Internal Communication

Messages should use structured serialization.

Example:

```json
{
  "type": "diagnosis_request",
  "domain": "battery",
  "source": "user"
}
```

Response:

```json
{
  "case_id": "BAT-00042",
  "status": "investigating"
}
```

---

# 29. Suggested Repository Structure

```text
system-intelligence/
│
├── service/
│   ├── core/
│   ├── telemetry/
│   │   ├── etw/
│   │   ├── wmi/
│   │   ├── perf/
│   │   ├── power/
│   │   └── events/
│   │
│   ├── state/
│   ├── tools/
│   ├── actions/
│   └── ipc/
│
├── intelligence/
│   ├── baseline/
│   ├── anomalies/
│   ├── reasoning/
│   ├── hypotheses/
│   ├── evidence/
│   └── memory/
│
├── ui/
│   ├── shell/
│   ├── overlay/
│   ├── notifications/
│   ├── diagnostics/
│   └── settings/
│
├── storage/
│   ├── schema/
│   └── migrations/
│
├── tests/
│
├── docs/
│
└── README.md
```

---

# 30. Proposed Technology Stack

## System runtime

Preferred:

```text
Rust or C++
```

Responsibilities:

- Windows APIs
- ETW
- process monitoring
- telemetry
- IPC
- privileged operations

Rust may offer stronger memory safety while maintaining native performance.

---

## Intelligence layer

Initial implementation:

```text
Python
```

Responsibilities:

- anomaly detection
- agent orchestration
- evidence reasoning
- experimentation
- model integration

The boundary between Python and the system runtime should use structured IPC rather than embedding Python deeply into the privileged service.

---

## Storage

Initial implementation:

```text
SQLite
```

Potential later additions:

```text
DuckDB
time-series storage
graph database
```

SQLite should be sufficient for the MVP.

---

## UI

```text
C#
WinUI 3
Windows App SDK
```

---

# 31. Local vs Cloud Intelligence

The product should adopt a hybrid architecture.

## Local

Keep local:

- telemetry
- system history
- anomaly detection
- sensitive device information
- common diagnostics
- basic reasoning where possible

## Cloud

Optional cloud reasoning may be used for:

- complicated diagnosis
- documentation search
- unfamiliar errors
- high-level explanation

Sensitive data should be minimized before transmission.

---

# 32. Privacy Requirements

The system should follow the principle:

> Collect system information, not user content, unless content access is necessary and explicitly permitted.

By default, the system should avoid reading:

- personal documents
- message contents
- browser history
- passwords
- private files

Process metadata may be collected where required.

Example:

```text
chrome.exe
CPU: 17%
GPU: 22%
RAM: 1.8 GB
```

The system should not inspect browser contents to diagnose power consumption.

---

# 33. Security Requirements

Because System Intelligence has deep OS access, security is critical.

Requirements:

- least-privilege execution
- separation of UI and privileged runtime
- signed binaries
- authenticated IPC
- explicit privilege escalation
- tool allowlisting
- structured agent actions
- comprehensive action logs
- no arbitrary shell access for the LLM
- rollback where possible

The agent must never be able to produce an arbitrary command string and directly send it to a privileged shell.

---

# 34. Auditability

Every autonomous investigation should be reproducible.

Example:

```text
Incident BAT-00042

18:43:02
Battery anomaly detected

18:43:03
Battery discharge = 22.4 W

18:43:03
CPU power = 4.1 W

18:43:04
GPU power = 14.8 W

18:43:04
chrome.exe identified as active GPU process

18:43:05
Hypothesis created:
Chrome keeping discrete GPU active

18:43:08
Diagnostic probe executed

18:43:18
GPU power fell to 1.3 W

18:43:19
Confidence increased to 94%
```

This should be accessible through an advanced diagnostics screen.

---

# 35. Failure Handling

The system must be comfortable saying:

> I do not have enough evidence to determine the cause.

Possible output:

```text
Battery usage is abnormally high.

I found two plausible causes:

1. Display consumption
2. background GPU activity

Current evidence is insufficient to distinguish them.
```

This is preferable to inventing a confident diagnosis.

---

# 36. Success Metrics

## Technical metrics

- telemetry collection overhead < 2% CPU under normal operation
- minimal battery overhead from monitoring
- agent does not continuously invoke large models
- anomaly detection false-positive rate remains low
- diagnostic actions remain reversible where possible

---

## Product metrics

- percentage of detected issues correctly attributed
- percentage of diagnoses that provide useful evidence
- successful remediation rate
- reduction in troubleshooting steps
- average time from anomaly to diagnosis
- percentage of recommendations accepted by users
- number of incidents resolved without external troubleshooting

---

# 37. MVP Non-Goals

Version 0.1 will not attempt to provide:

- antivirus replacement
- arbitrary system administration
- automatic driver installation
- BIOS configuration
- malware removal
- unrestricted registry modification
- complete Windows optimization
- autonomous modification of security configuration
- support for every hardware vendor
- full Linux/macOS compatibility

---

# 38. Development Milestones

## Milestone 1 — System Observer

Build a Windows service capable of reporting:

```text
Battery
CPU
Memory
Disk
Network
Processes
```

Target output:

```text
System Intelligence

Battery
Charge              72%
Health              92%
Discharge           8.7 W

CPU
Utilization         14%

Memory
Used                9.8 / 16 GB

GPU
Utilization         3%

Top Processes
chrome.exe
Code.exe
python.exe
```

No LLM required.

---

## Milestone 2 — Historical State

Add:

- SQLite storage
- metric history
- system events
- process history
- machine baselines

---

## Milestone 3 — Battery Anomaly Detection

Learn normal battery behavior.

Trigger:

```text
battery consumption > expected range
```

Generate incident.

---

## Milestone 4 — Diagnostic Agent

Expose structured tools to the reasoning agent.

Implement:

```text
inspect_battery
inspect_cpu
inspect_gpu
inspect_process
inspect_power_configuration
inspect_recent_changes
```

---

## Milestone 5 — Evidence-Based Diagnosis

Require the agent to produce:

```text
Finding
Evidence
Confidence
Recommendation
```

---

## Milestone 6 — WinUI Integration

Implement:

- tray icon
- diagnostic window
- system notifications
- global invocation overlay

---

## Milestone 7 — Safe Actions

Implement the Action Broker.

Initially support:

```text
terminate process
change power mode
disable startup application
```

---

## Milestone 8 — Diagnostic Probes

Introduce temporary, reversible interventions.

---

# 39. First Demonstration

The first complete demonstration should show the following scenario.

### Normal state

```text
Battery discharge:
8.4 W
```

### Problem introduced

A process causes the discrete GPU to remain active.

```text
Battery discharge:
21.7 W
```

### Automatic detection

System Intelligence detects abnormal energy consumption.

### Investigation

The agent determines:

```text
Discrete GPU power: 14 W

Active GPU process:
chrome.exe
```

### Evidence collection

A temporary diagnostic probe confirms that stopping Chrome GPU activity reduces consumption.

### Notification

```text
High battery consumption detected.

Chrome is keeping your NVIDIA GPU active.

Current consumption:
21.7 W

Expected:
8–10 W

[Fix]
[Details]
```

### Fix

The system applies a safe configuration change.

### Verification

```text
Before:
21.7 W

After:
9.1 W
```

The system reports:

> Battery consumption has returned to your normal range.

---

# 40. Long-Term Vision

The system should eventually expand from diagnostics into adaptive computer management.

Potential capabilities include:

```text
"Why is my computer slow?"

"Why did my Wi-Fi disconnect?"

"Why did my application crash?"

"Why can't PyTorch detect CUDA?"

"Where did my disk space go?"

"Keep my laptop quiet during this meeting."

"Optimize this machine for maximum battery life."

"Prepare this machine for ML training."
```

Ultimately the architecture evolves from:

```text
AI troubleshooting assistant
```

into:

```text
AI system intelligence runtime
```

where the operating loop becomes:

```text
PERCEIVE
   ↓
UNDERSTAND
   ↓
ACT
   ↓
VERIFY
   ↓
LEARN
   └──────────→ PERCEIVE
```

The objective is not merely to place an AI assistant inside Windows.

The objective is to give the computer a continuously maintained understanding of its own state and the ability to safely use that understanding to assist the person operating it.