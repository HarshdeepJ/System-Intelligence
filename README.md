# System Intelligence

An OS-native AI agent for Windows that watches a machine's vitals (battery, CPU, memory, processes...), notices when something looks wrong, investigates the cause, and explains it with evidence — instead of a user having to manually dig through Task Manager, Event Viewer, and `powercfg`.

Full product thinking lives in:
- [`PRD — System Intelligence_ OS-Native AI Diagnostic Agent.md`](PRD%20—%20System%20Intelligence_%20OS-Native%20AI%20Diagnostic%20Agent.md) — what we're building and why
- [`Technical Design — System Intelligence v0.1.md`](Technical%20Design%20—%20System%20Intelligence%20v0.1.md) — how it's architected

## Where things stand right now

This repo now spans two runtimes:

**A C++ CLI** (`core/`, still no AI involved — everything here is plain statistics, not a model):
- `sysintel status` — one-shot snapshot (Phase 1: prove we can read real data from Windows)
- `sysintel record` — continuously samples battery/CPU/memory and stores it in SQLite (Phase 2: history)
- `sysintel history <metric> --last <minutes>` — reads back min/avg/max over a time window
- `sysintel check-battery` / `check-memory` / `check-cpu` — runs one anomaly check right now for that domain and prints the result (Phase 3 for battery; memory/cpu generalized in Phase 7)
- `sysintel watch` — `record` plus a battery + memory + cpu anomaly check every 60 seconds, in one long-running loop (Phase 3, extended in Phase 7)
- `sysintel inspect [--db path]` — one unified snapshot: hardware inventory, live GPU state, and (with `--db`) recent events (Phase 3.5)
- `sysintel events [--last minutes]` — process start/stop and AC connect/disconnect events, synthesized from the recorder without needing ETW
- `sysintel act change-power-mode --level <best_power_efficiency|best_performance> [--reason "..."] [--yes]` — the first safe, reversible action: switches Windows 11's battery Power Mode, gated behind an explicit `--yes` (Phase 5)
- `sysintel act rollback <action-id> [--yes]` — undoes a previous action, restoring its exact prior state
- `sysintel actions [--last n]` — the action audit log
- `sysintel power-schemes` — debug view of every power scheme/overlay Windows reports on this machine
- every command above also takes `--json` for machine-readable output

**A Python reasoning agent** (`intelligence/`, Phase 4 — real LLM reasoning via Groq, Phase 6 closes the loop into an actual action):
- `diagnose-battery [--auto-approve] [--no-act]` — investigates an open battery incident: collects evidence via the CLI's `--json` output, sends it to an LLM for hypothesis selection, prints an evidence-based diagnosis in the PRD's Finding/Confidence/Evidence/Alternatives/Recommendation format, and — unless `--no-act` — offers a concrete, runnable action with an interactive approval prompt (or applies it automatically with `--auto-approve`)

Everything else (the native UI, AMD/Intel GPU telemetry, network/disk-I/O collectors, and the Python agent generalizing beyond battery) is designed but not built, and will sit on top of this same collector + storage + detector + tool-client code without needing to change it.

### Phase 7: one detector, three domains — the start of "diagnose everything"

The direction from here is explicit: cover every hardware domain, not just battery. The first step toward that wasn't a new collector — it was noticing that the battery-drain detector's actual logic (trailing baseline mean/stddev vs. a short "right now" window, gated on accumulated history, hysteresis on resolve) had nothing battery-specific about it *except the metric name*. `core/anomalies/anomaly_detector.hpp/cpp` pulls that logic out into a metric-agnostic `AnomalyDetector`, and `battery_detector.cpp` is now a thin ~50-line wrapper that configures it with `battery.discharge_watts` and translates result names back to `BatteryCheckResult`'s existing enum — so the CLI's `check-battery --json` shape and Python's `schemas.py` didn't need to change at all.

This was deliberately *not* built generic from day one (see the project's running "no premature abstraction" rule) — it only became a generic class once there was a second real domain to prove the abstraction against. That second and third domain are `check-memory` and `check-cpu`, reusing history the recorder was already collecting since Phase 2 — no new collector work needed for either. `sysintel watch` now checks all three every 60 seconds.

Verified with seeded synthetic data for each new domain independently: real anomalies correctly detected with sensible baseline/threshold values (e.g. CPU baseline 17.1%, spike to 85.2%, threshold 48.2%; memory baseline 61.0%, spike to 95.2%, threshold 91.6%), no false positive on normal data, and — critically — a battery-specific regression test confirming `check-battery`'s behavior is bit-for-bit unchanged after the refactor (same incident IDs, same thresholds, same ongoing/no-duplicate behavior as the original Phase 3 test).

```text
$ sysintel check-cpu --db sysintel.db
ANOMALY DETECTED (cpu): cpu-1789476582194
  Observed  85.2%
  Baseline  17.1%
  Threshold 48.2%

$ sysintel check-memory --db sysintel.db
ANOMALY DETECTED (memory): memory-1789476582254
  Observed  95.2%
  Baseline  61.0%
  Threshold 91.6%
```

Not yet done, and the natural continuation: the Python diagnostic agent still only knows how to investigate battery incidents (`get_battery_anomaly_status()`); memory and CPU anomalies are detected but nothing diagnoses *why* yet. New collectors (network, disk I/O, per-process CPU attribution, thermal/fan) are the next layer after that.

### Phase 6: closing the loop, from diagnosis to action

Phases 4 and 5 built two halves that never talked to each other: a diagnostic agent that could only print a text recommendation, and an Action Broker that could only be invoked by hand. This phase connects them — but deliberately *not* by letting the LLM decide what to execute.

The design keeps the same boundary this whole project has held since Phase 4: the model only ever produces prose. `agent.py`'s `_build_suggested_action()` is a plain function, not an LLM call — it looks at whether there's an open/ongoing anomaly (nothing else) and always proposes the same fixed `change_power_mode` / `best_power_efficiency` action, regardless of which hypothesis won. That's a deliberate choice, not a limitation being papered over: Best Power Efficiency mode reduces both CPU- and GPU-adjacent power draw at the OS level, so it's a reasonable, fully-reversible thing to try even when the root cause isn't pinned down exactly — and because it's cheap to undo, it doesn't need to wait for perfect diagnostic certainty the way a riskier action would.

`main.py` is the actual approval gate: it prints the suggested action and asks `Apply this now? [y/N]` before anything happens, unless `--auto-approve` was passed explicitly. Only after a yes does `tools.py`'s `apply_change_power_mode()` call through to `sysintel act change-power-mode --yes` — the exact same Action Broker path a human typing the command directly would take.

Verified end-to-end on the real machine, twice: once with the interactive prompt declined (confirmed the machine's power mode was untouched, and the exact equivalent command was printed for later use), and once with `--auto-approve` after manually switching to a different mode first (confirmed the diagnosis correctly detected the mismatch, applied the real change through the broker, and independent re-reads confirmed both the change and the final restored state).

```text
$ intelligence diagnose-battery --db sysintel.db --sysintel-exe build\sysintel.exe
[reasoned by: LLM (Groq)]
...
Suggested action
  Switch Windows' battery Power Mode to 'Best power efficiency'

Apply this now? [y/N]: n
Not applied. Run it yourself later with:
  sysintel act change-power-mode --level best_power_efficiency --reason "battery drain anomaly: 21.9W vs 8.4W baseline" --yes
```

```text
$ intelligence diagnose-battery --db sysintel.db --sysintel-exe build\sysintel.exe --auto-approve
[reasoned by: LLM (Groq)]
...
Suggested action
  Switch Windows' battery Power Mode to 'Best power efficiency'
  (--auto-approve given, applying without an interactive prompt)

Applied: changed and verified
  Previous state   DED574B5-45A0-4F42-8737-46345C09C238
  New state        961CC777-2547-4F9D-8174-7D86181B8A7A
  Action id        change_power_mode-1789475982288  (undo with: sysintel act rollback change_power_mode-1789475982288 --yes)
```

### Phase 5: the Action Broker, and the first safe action

Up to now this project only observes and explains. `sysintel act` is the first thing that can actually change the machine — and it's built around the PRD's Action Broker: permission check → (only if approved) execute → verify → audit record. There is exactly one path from a recommendation to a real change, and it isn't the Python agent or the LLM — it's this broker, invoked explicitly by a human.

The action itself: **change the Windows 11 "Power Mode" slider** (Settings → System → Power), specifically the battery (DC) side of it — directly addressing the battery-drain scenario this whole project is built around. Two real, hardware-verified discoveries shaped this:

1. **Classic multi-plan switching (`powercfg /list`-style) doesn't work on this dev machine at all** — it only has a single "Balanced" scheme registered, no "Power Saver"/"High Performance" to switch to. `enumerate_power_schemes()` and `powercfg /list` agree on this. So the *first* safe-action candidate (switch between classic power plans) was discarded before being built, in favor of the Power Mode slider, which works regardless of how many classic schemes exist.
2. **The Power Mode overlay GUIDs are not the classic `GUID_MAX_POWER_SAVINGS`-style constants** — an earlier version of this code assumed they were reused; live testing on this machine proved that wrong (the API call succeeded but returned a GUID matching neither constant). The actual GUIDs were found by asking Windows directly — enumerate with `ACCESS_OVERLAY_SCHEME`, read each one's friendly name back — and cross-checked against what `PowerGetUserConfiguredDCPowerMode()` genuinely returned live. Only `best_power_efficiency`'s mapping has that live cross-check; `best_performance` is inferred from its reported name ("Max Performance Overlay") and flagged lower-confidence in the code.

Verified end-to-end on the real machine: dry-run preview (no side effects, confirmed by independent re-read), real execution with independent verification, full audit trail, rollback restoring the *exact* prior state, and refusal paths (unknown level, double rollback, rollback of a nonexistent action id) all behaving correctly. The machine's power mode is back to exactly what it was before any of this testing started.

```text
$ sysintel act change-power-mode --level best_performance --reason "testing" --db sysintel.db
Preview (not applied): dry run: would change DC power mode to 'best_performance'. Pass approval to apply.
  Current state    961CC777-2547-4F9D-8174-7D86181B8A7A
  Would become     DED574B5-45A0-4F42-8737-46345C09C238

$ sysintel act change-power-mode --level best_performance --reason "testing" --db sysintel.db --yes
Applied: changed and verified
  Previous state   961CC777-2547-4F9D-8174-7D86181B8A7A
  New state        DED574B5-45A0-4F42-8737-46345C09C238
  Action id        change_power_mode-1789475388157  (use 'sysintel act rollback change_power_mode-1789475388157' to undo)

$ sysintel act rollback change_power_mode-1789475388157 --db sysintel.db --yes
Applied: rolled back and verified
  Previous state   DED574B5-45A0-4F42-8737-46345C09C238
  New state        961CC777-2547-4F9D-8174-7D86181B8A7A
```

### Phase 3.5: a Unified System Model

Rather than the agent eventually needing dozens of scattered tools (`get_cpu()`, `get_gpu()`, `get_fans()`, ...), this phase reorganizes what we collect into four named pillars:

| Pillar | What it is | Where it lives |
|---|---|---|
| **Inventory** | Things that rarely change: CPU/GPU/disk model, OS version, battery chemistry | `core/inventory/`, refreshed on demand |
| **State** | Things that change constantly: current battery/CPU/memory/GPU readings | `core/collectors/`, already existed since Phase 1 |
| **History** | State over time | SQLite, already existed since Phase 2 |
| **Events** | Discrete "something changed" facts: a process started, AC got unplugged | `core/events/`, new this phase |

The other new piece is an **availability-aware value type**. A bare `0` or `null` is ambiguous — does a fan reading of `0` mean the fan is off, or that this laptop just doesn't expose fan telemetry at all? Every new reading in this phase is a `Reading<T>`, which carries both a value *and* one of `ok` / `unsupported` / `unavailable` / `error`, so the difference is never lost.

`sysintel status` gives you something like:

```text
System Intelligence -- status

Power
  Source          AC
  Charge          88%
  Charge rate     20.7 W

CPU
  Utilization     16.4%

Memory
  Used            11.3 / 15.6 GB  (72%)

Top Processes (by memory)
  Code.exe                464.4 MB
  explorer.exe            456.4 MB
  ...
```

`sysintel record` runs until Ctrl+C, and `sysintel history cpu.utilization --last 5` then gives you:

```text
cpu.utilization -- last 5 minutes (21 samples)

  Min   7.18
  Avg   13.69
  Max   26.68
```

And `sysintel check-battery` — once there's at least 14 days of accumulated battery history (configurable with `--min-history-days`, mainly for testing) — gives you one of:

```text
Not enough history yet to evaluate battery anomalies.
```
```text
Battery normal. Baseline 8.4W (+/-1.5W)
```
```text
ANOMALY DETECTED: battery-1789471660271
  Observed  21.9 W
  Baseline  8.4 W
  Threshold 15.4 W
```

And once there's an open incident, `python -m intelligence.main diagnose-battery` investigates it — hypothesis selection now goes to a real LLM call (Groq), with evidence collection staying entirely deterministic:

```text
[reasoned by: LLM (Groq)]

Finding
  System-wide CPU workload is responsible -- Battery power consumption (21.88 W) exceeds
  the threshold (15.36 W), coinciding with a sharp rise in CPU utilization (75.8% recent
  average vs 23.6% baseline).

Confidence: 75%

Evidence
  - Battery discharge: 21.88W observed vs 8.39W baseline (threshold 15.36W)
  - Live battery watts (21.88) are well above both baseline and the defined threshold.
  - CPU recent 5-minute average (75.8%) is far higher than its 24-hour baseline (23.6%),
    matching the timing of the power spike.
  - GPU telemetry is marked as unsupported, so no evidence can confirm or refute GPU
    involvement.

Alternative explanations
  - GPU activity is responsible (only meaningful when a GPU actually reports usable
    telemetry -- most machines/vendors don't yet)
  - Unexplained by evidence this agent currently checks

Recommended action
  Identify and limit the high-CPU processes, reduce background work, and consider
  power-saving settings for the CPU.

Risk
  Reducing or terminating active processes may interrupt work or degrade user experience.

Expected result
  CPU utilization should drop toward baseline, bringing battery power draw back below
  the threshold.
```

When CPU is normal too, the same model correctly drops confidence rather than forcing an answer — verified side by side against the CPU-elevated case above:

```text
[reasoned by: LLM (Groq)]

Finding
  Unexplained by evidence this agent currently checks -- Battery power draw (21.95 W) is
  well above the threshold (15.36 W), but CPU usage is only marginally higher than its
  24h baseline and GPU telemetry is unavailable, leaving no concrete evidence for a
  CPU- or GPU-driven cause.

Confidence: 30%
```

If the LLM call fails for any reason (no API key, network error, malformed response), a deterministic rule-based fallback takes over automatically and says so explicitly (`[reasoned by: rule-based fallback]`) rather than crashing — verified by running with `GROQ_API_KEY` unset.

`sysintel inspect` pulls inventory + live GPU state (and, with `--db`, recent events) into one screen:

```text
====== SYSTEM INTELLIGENCE: inspect ======

SYSTEM
  Manufacturer    Dell Inc.
  Model           XPS 9315
  OS              Microsoft Windows 11 Home Single Language

CPU
  Model           12th Gen Intel(R) Core(TM) i7-1250U
  Cores           10
  Threads         12

GPU
  [0] Intel -- Intel(R) Iris(R) Xe Graphics
      VRAM            2048 MB

GPU STATE (live)
  [0] Intel -- Intel(R) Iris(R) Xe Graphics
      Utilization unsupported
      VRAM            unsupported
      Temperature unsupported
      Power (W)   unsupported
      P-State     unsupported

MEMORY
  DIMMs           8
  Total           16 GB

BATTERY
  Chemistry       Unknown
  Design cap.     unavailable
  Full charge     unavailable

RECENT EVENTS
  1789473073420  process.started  pid=16948  name=conhost.exe
  1789473062157  process.stopped  pid=3080  name=Notepad.exe
  1789473053951  power.ac_disconnected

===========================================
```

Every `unsupported` above is real, verified behavior on this dev machine (an Intel-only laptop) — not a placeholder. That's the whole point of the availability-aware `Reading<T>` type: this machine's GPU genuinely doesn't expose utilization/temperature/power through any provider this project has (yet), and the output says so explicitly instead of printing a misleading `0`.

When CPU is normal too, it says exactly that instead of guessing:

```text
Finding
  Unexplained by anything this build can currently measure (likely GPU activity or a driver regression -- no collector for either yet)

Confidence: 60%
```

## How the pieces connect

```mermaid
flowchart TD
    subgraph CLI["core/cli — the dashboard"]
        MAIN["main.cpp<br/>status | record | history | watch | check-battery"]
    end

    subgraph COLLECTORS["core/collectors — one sensor each"]
        BAT["battery.hpp / battery.cpp<br/>returns: BatterySnapshot"]
        CPU["cpu.hpp / cpu.cpp<br/>returns: CpuSnapshot"]
        MEM["memory.hpp / memory.cpp<br/>returns: MemorySnapshot"]
        PROC["process.hpp / process.cpp<br/>returns: list of ProcessInfo"]
    end

    subgraph WINAPI["Windows itself"]
        W1["GetSystemPowerStatus()<br/>CallNtPowerInformation()"]
        W2["PDH performance counters"]
        W3["GlobalMemoryStatusEx()"]
        W4["CreateToolhelp32Snapshot()<br/>GetProcessMemoryInfo()"]
    end

    subgraph SAMPLER["core/sampler — the recording loop"]
        SAMP["sampler.hpp / sampler.cpp<br/>ticks, buffers, flushes"]
    end

    subgraph STORAGE["core/storage — the history layer"]
        SAMPLE["metric_sample.hpp<br/>the shared MetricSample shape"]
        STORE["sqlite_store.hpp / sqlite_store.cpp<br/>insert_batch() / query_range()<br/>query_stats() / incidents CRUD"]
    end

    subgraph ANOMALY["core/anomalies — the fast brain"]
        DETECT["battery_detector.hpp / .cpp<br/>check(): baseline vs right-now"]
    end

    SQLITE["third_party/sqlite<br/>vendored sqlite3.c/.h"]

    MAIN -->|"status: one-shot calls"| BAT
    MAIN --> CPU
    MAIN --> MEM
    MAIN --> PROC

    MAIN -->|"record"| SAMP
    MAIN -->|"history"| STORE
    MAIN -->|"check-battery"| DETECT
    MAIN -->|"watch: record +"| SAMP

    SAMP -->|"loops, calling"| BAT
    SAMP --> CPU
    SAMP --> MEM
    SAMP -->|"insert_batch()"| STORE
    SAMP -.->|"periodic hook, every 60s"| DETECT

    BAT --> W1
    CPU --> W2
    MEM --> W3
    PROC --> W4

    DETECT -->|"query_stats() / query_earliest_timestamp()<br/>open_incident() / resolve_incident()"| STORE

    STORE --> SQLITE
    STORE -.-> SAMPLE

    BUILD["CMakeLists.txt<br/>(build recipe — compiles sqlite3.c, links pdh/powrprof/psapi)"] -.->|compiles + links everything| MAIN

    subgraph PYPROC["A SEPARATE PROCESS: python -m intelligence.main"]
        TOOLS["tools.py: SysIntelClient<br/>the only way the agent touches the machine"]
        SCHEMAS["schemas.py<br/>Pydantic models incl. Reading[T], ActionOutcome"]
        AGENT["agent.py: BatteryDiagnosticAgent<br/>TRIAGE -&gt; HYPOTHESES -&gt; EVIDENCE -&gt; (LLM or fallback) -&gt; EXPLAIN<br/>+ _build_suggested_action() (plain code, not the LLM)"]
        LLM["llm.py: select_hypothesis()<br/>the ONLY thing that touches an LLM"]
        PYMAIN["main.py<br/>diagnose-battery: prints reasoned_by,<br/>asks 'Apply this now? [y/N]'"]
    end

    GROQ[["Groq API<br/>(external service)"]]

    PYMAIN --> AGENT
    AGENT -->|"get_battery_anomaly_status()<br/>get_system_snapshot()<br/>get_metric_history()<br/>get_recent_events()"| TOOLS
    TOOLS -.->|validates response into| SCHEMAS
    TOOLS ==>|"subprocess: sysintel.exe status/history/check-battery/events --json<br/>(fixed subcommands + typed args, never a shell string)"| MAIN

    AGENT -->|"fixed hypothesis list + deterministically-gathered evidence only"| LLM
    LLM -->|"HTTPS, structured JSON response required"| GROQ
    LLM -.->|"LlmUnavailableError on failure"| AGENT

    PYMAIN -->|"only after y/N or --auto-approve"| TOOLS
    TOOLS ==>|"subprocess: sysintel act change-power-mode --yes<br/>(same fixed action_type/params shape as the CLI)"| AB2["core/actions/action_broker.cpp<br/>(same broker as Phase 5's diagram)"]
```

Nothing here talks to Windows directly except the four `collectors/` files — everything else (`main.cpp`, `sampler.cpp`) just asks a collector for its snapshot. That separation is deliberate: later, the background service and the AI reasoning layer will call these exact same collector functions, and none of the collector code will need to change.

Notice the `python -m intelligence.main` box is a **separate process**, connected to everything above it by exactly one edge: `tools.py` calling `sysintel.exe` as a subprocess with fixed subcommands (`status`, `history`, `check-battery`) and typed arguments — never a free-form string handed to a shell. That's the whole point of the boundary: the reasoning layer can be wrong, slow, or (eventually) an actual LLM making mistakes, and none of that can turn into an arbitrary command against the machine. The real product will eventually replace this subprocess+JSON transport with the named-pipe IPC from the tech design, but the *tools* the agent calls won't need to change — only what's underneath them.

The new `PYMAIN -> TOOLS -> action_broker.cpp` edge at the bottom is Phase 6's addition, and it's worth tracing carefully: it starts at `PYMAIN`, not `AGENT` or `LLM` — the approval prompt lives in `main.py`, gating the call before `tools.py` is ever invoked for it. `AGENT` never calls `TOOLS`' action methods itself; it only *returns* a `suggested_action` value for `PYMAIN` to look at. That's what keeps the LLM's blast radius exactly where it was in Phase 4: it can influence what gets *suggested* in prose, but the actual decision to invoke the broker, and the fixed shape of what gets sent to it, never passes through the model at all.

### Phase 3.5's additions: Inventory, Events, and GPU dispatch

This is a separate diagram rather than folding into the one above, since cramming both in would make neither legible.

```mermaid
flowchart TD
    subgraph MODEL["core/model — shared types, no logic"]
        AVAIL["availability.hpp<br/>Reading&lt;T&gt; { value, availability }"]
        INVM["system_inventory.hpp"]
        GPUM["gpu_state.hpp"]
        EVTM["system_event.hpp"]
    end

    INSPECT["sysintel inspect [--db]"]

    subgraph INVENTORY["core/inventory"]
        INV["system_inventory.cpp<br/>collect_system_inventory()"]
    end

    subgraph GPUCOL["core/collectors/gpu.cpp"]
        DISPATCH["collect_gpu_state()<br/>detect vendor -&gt; pick provider"]
    end

    subgraph PROVIDERS["core/providers"]
        WMI["windows/wmi_client.cpp<br/>generic WQL query client"]
        NVML["nvidia/nvml_provider.cpp<br/>dynamically loads nvml.dll<br/>(UNTESTED -- no NVIDIA hw here)"]
    end

    subgraph EVENTS["core/events"]
        DETECTOR["event_detector.cpp<br/>SystemEventDetector::detect()<br/>diffs consecutive snapshots"]
    end

    SAMP3["Sampler (Phase 2, unchanged)"]
    STORE3["SqliteStore<br/>insert_events() / query_recent_events()"]

    INSPECT --> INV
    INSPECT --> DISPATCH
    INSPECT -.->|"--db only"| STORE3

    INV --> WMI
    DISPATCH --> WMI
    DISPATCH --> NVML

    SAMP3 -->|"every tick: full process list"| DETECTOR
    DETECTOR -->|"insert_events()"| STORE3

    INV -.-> INVM
    DISPATCH -.-> GPUM
    DETECTOR -.-> EVTM
    INVM -.-> AVAIL
    GPUM -.-> AVAIL
```

### Phase 5's addition: the Action Broker

```mermaid
flowchart TD
    CLI4["sysintel act change-power-mode / rollback"]

    subgraph BROKER["core/actions — the only path to a mutation"]
        AB["action_broker.cpp: ActionBroker<br/>permission check -&gt; execute -&gt; verify -&gt; audit"]
        PS["power_scheme.cpp<br/>enumerate/get/set power schemes + Power Mode overlay"]
    end

    WINPOWER[["Windows PowrProf API<br/>PowerGetUserConfiguredDCPowerMode()<br/>PowerSetUserConfiguredDCPowerMode()"]]

    STORE4["SqliteStore<br/>record_action() / get_action()<br/>query_recent_actions() / mark_action_rolled_back()"]

    CLI4 -->|"approved: bool (--yes)"| AB
    AB -->|"only recognized action_type: change_power_mode"| PS
    PS --> WINPOWER
    AB -->|"always logs, approved or not, success or not"| STORE4
    AB -.->|"rollback restores the EXACT previous_state,<br/>not a re-derived 'opposite' value"| PS
```

Notice the broker is the only node with an edge into `power_scheme.cpp`'s write functions (`set_dc_power_mode*`) — nothing else in this codebase, including the Python agent, can reach them. And notice `CLI4 -> AB` carries `approved` as an explicit boolean the *caller* controls (today, a `--yes` flag; later, a UI approval dialog) — the broker itself never decides to skip approval.

## What each file does, in easy words

### [`core/collectors/battery.hpp`](core/collectors/battery.hpp)
Just a label — defines a small box called `BatterySnapshot` to carry battery facts around: is there a battery, is it plugged in, what % charge, and how many watts it's charging/draining at. No logic here, just "here's the shape of the data."

### [`core/collectors/battery.cpp`](core/collectors/battery.cpp)
Does the actual work. Calls two Windows functions:
- `GetSystemPowerStatus()` — the simple one: charge %, plugged in or not.
- `CallNtPowerInformation(SystemBatteryState, ...)` — the detailed one: exact wattage.

Gotcha we hit and fixed: Windows stores that wattage in a slot that's technically "can't be negative," but secretly does go negative (for draining) by wrapping around like an odometer rolling backwards. We had to tell the code "treat this as a number that can be negative," otherwise a normal 14-watt drain showed up as "4 million watts."

### [`core/collectors/cpu.hpp`](core/collectors/cpu.hpp) / [`cpu.cpp`](core/collectors/cpu.cpp)
Windows doesn't have a single "CPU % right now" button — it only tracks total time spent working. So this file measures twice, one second apart, and the *difference* between the two readings is the utilization percentage. That's why the tool pauses for about a second while running — it's genuinely waiting to take that second measurement. The tool doing the measuring is called **PDH** (Performance Data Helper), Microsoft's built-in library for exactly this.

### [`core/collectors/memory.hpp`](core/collectors/memory.hpp) / [`memory.cpp`](core/collectors/memory.cpp)
The easy one. `GlobalMemoryStatusEx()` is a single function call that hands back total RAM, available RAM, and a ready-made "% used" number — no waiting or math required.

### [`core/collectors/process.hpp`](core/collectors/process.hpp) / [`process.cpp`](core/collectors/process.cpp)
Three steps:
1. Take a snapshot of every running process (like Task Manager's list) via `CreateToolhelp32Snapshot`.
2. For each one, ask how much memory it's using via `GetProcessMemoryInfo` (some processes refuse this — those get skipped, that's fine).
3. Sort biggest-to-smallest and keep the top 8.

Known simplification: this sorts by **memory**, not CPU. Per-process CPU needs the same "measure twice, one second apart" trick as the CPU collector above, just done individually for every process — left for a follow-up rather than built into this first pass.

It also exposes `get_all_process_identities()` — every PID+name with *no* memory query, cheap enough to call every ~1 second purely so the event detector below can diff it against the last tick.

### [`core/model/availability.hpp`](core/model/availability.hpp)
The foundational addition this phase: `Reading<T>` pairs a value with one of four states — `ok`, `unsupported` (this hardware/vendor genuinely doesn't expose it), `unavailable` (should be readable, but this particular query failed), or `error`. Every new collector from this phase on returns `Reading<T>` fields instead of a bare number, so "the fan is off" and "we have no idea if there's even a fan" are never confused with each other.

### [`core/model/system_inventory.hpp`](core/model/system_inventory.hpp) / [`core/inventory/system_inventory.cpp`](core/inventory/system_inventory.cpp)
The "things that don't change often" pillar — CPU/GPU/disk model, OS version, memory DIMM count, battery chemistry. Implemented via WMI (`Win32_Processor`, `Win32_VideoController`, `Win32_OperatingSystem`, `Win32_PhysicalMemory`, `Win32_DiskDrive`, `Win32_Battery`) — the first time this codebase queries WMI rather than raw Win32/PDH. `Win32_Battery`'s `DesignCapacity`/`FullChargeCapacity` come back genuinely empty on this dev machine's OEM controller, which is exactly the real-world case the availability type exists for: the battery *is* there (so it's not "unsupported"), the fields are just unpopulated (so it's "unavailable").

### [`core/providers/windows/wmi_client.hpp`](core/providers/windows/wmi_client.hpp) / [`wmi_client.cpp`](core/providers/windows/wmi_client.cpp)
A small reusable wrapper around WMI's COM API (`IWbemLocator`, `IWbemServices`, SAFEARRAYs of property names, VARIANTs...) so every inventory field doesn't have to repeat that boilerplate. Give it a WQL string, get back plain string-keyed rows. If WMI itself is unreachable (e.g. the service is disabled), `ok()` is false and every query just returns empty rather than crashing — callers treat that the same way they treat a missing field.

### [`core/model/gpu_state.hpp`](core/model/gpu_state.hpp) / [`core/collectors/gpu.cpp`](core/collectors/gpu.cpp)
The vendor-dispatch pattern: detect each GPU's vendor via WMI (same source the inventory uses), then hand NVIDIA adapters to the NVML provider and mark AMD/Intel adapters `unsupported` (no provider built for either yet). If WMI says there's an NVIDIA GPU but NVML couldn't be loaded, that's reported as `unavailable`, not `unsupported` — the hardware path exists, the telemetry provider just isn't reachable right now. This distinction is only meaningful because of the availability type above.

### [`core/providers/nvidia/nvml_min.hpp`](core/providers/nvidia/nvml_min.hpp) / [`nvml_provider.hpp`](core/providers/nvidia/nvml_provider.hpp) / [`nvml_provider.cpp`](core/providers/nvidia/nvml_provider.cpp)
GPU utilization/VRAM/temperature/power/performance-state for NVIDIA GPUs, via NVIDIA's NVML. Rather than linking a static import lib (which requires the CUDA Toolkit as a build dependency — not something an end user's machine would have), this hand-declares the small stable subset of NVML's C ABI it needs and loads `nvml.dll` dynamically with `LoadLibrary`/`GetProcAddress` at runtime. That's also exactly why the project still compiles and runs cleanly here: this dev machine has only an Intel iGPU, `nvml.dll` doesn't exist on it, `LoadLibraryA` returns null, and every GPU reading correctly falls back to `unavailable`/`unsupported` instead of failing to build.

**Caveat, stated plainly:** this was written without access to NVIDIA hardware. The dynamic-loading mechanism and struct layouts follow NVIDIA's publicly documented, stable NVML ABI, and the negative path (no `nvml.dll` present) is verified working — but the actual "read real GPU telemetry" path has not been exercised against a real device or driver. Verify on NVIDIA hardware before relying on it.

### [`core/model/system_event.hpp`](core/model/system_event.hpp) / [`core/events/event_detector.hpp`](core/events/event_detector.hpp) / [`event_detector.cpp`](core/events/event_detector.cpp)
The "what changed" pillar — without needing ETW yet. `SystemEventDetector` keeps the previous tick's full PID set and AC-power state; each call diffs the new snapshot against it and returns `process.started`/`process.stopped`/`power.ac_connected`/`power.ac_disconnected` events for whatever changed. True ETW-based tracking (lower latency, catches processes that start and exit *between* poll ticks) is a real upgrade for later, not needed to get real event data today — verified by launching and closing Notepad mid-recording and seeing both events land in SQLite.

### [`core/storage/metric_sample.hpp`](core/storage/metric_sample.hpp)
Another label file. Defines `MetricSample` — one universal shape (`metric` name, `value`, `unit`, `timestamp`) that *every* reading gets converted into before it's stored, regardless of which collector produced it. This is a "narrow table" design: one row per reading, so adding a brand-new metric later (GPU, disk, network) never requires changing the database schema — it's just new rows with a new metric name.

### [`core/storage/sqlite_store.hpp`](core/storage/sqlite_store.hpp) / [`sqlite_store.cpp`](core/storage/sqlite_store.cpp)
The history layer. Jobs:
- `insert_batch(samples)` — writes a whole batch of samples in **one transaction** instead of one write per sample. Every database commit is a disk flush, so batching turns "hundreds of disk flushes" into "one every few seconds" — a big real-world speed difference for basically free.
- `query_range(metric, since, until)` — reads back min/avg/max for one metric over a time window.
- `query_stats(metric, since, until)` — reads back mean/standard-deviation instead, which is what the anomaly detector actually needs. It computes this with one `AVG`/`AVG(x*x)` SQL query rather than pulling every row back and computing it in C++.
- `query_earliest_timestamp(metric)` — how far back a metric's history actually goes, used to gate anomaly evaluation until there's enough of it.
- `open_incident()` / `get_open_incident()` / `resolve_incident()` — a tiny incident lifecycle: at most one *open* incident per domain at a time, so a persistent anomaly doesn't spam a new incident every check.
- `insert_events(events)` / `query_recent_events(since, limit)` — the events pillar's storage, batched into the same transaction pattern as metric samples since they're produced by the same tick.
- `record_action()` / `get_action()` / `query_recent_actions()` / `mark_action_rolled_back()` — the Action Broker's audit trail. Every attempted action is logged, success or failure, and rollback marks the original record rather than deleting it, so the history stays complete.

The database also has exactly one index on `metric_samples`, on `(metric, timestamp)`; one on `incidents`, on `(domain, status)`; one on `system_events`, on `(timestamp)`; and one on `actions`, on `(created_at)` — all four built from the actual questions this project asks, not defensively.

Runs in "WAL mode," a SQLite setting that lets it survive a crash without corrupting data — worst case, we lose the last few unflushed seconds of samples, never the whole database.

### [`core/sampler/sampler.hpp`](core/sampler/sampler.hpp) / [`sampler.cpp`](core/sampler/sampler.cpp)
The recording loop behind `sysintel record`. Each tick, it checks each metric's own clock — CPU every ~1 second, battery and memory every 5 — collects the due ones, and buffers them in memory. Every 5 seconds it hands the whole buffer to `sqlite_store` in one batch. Ctrl+C sets a stop flag the loop checks each tick, so it exits cleanly and flushes whatever's left in the buffer first.

Known simplification: this is one loop checking everything in turn (round-robin), not one thread per metric. That's fine for a recorder you run from a terminal; the real background service will eventually give each collector its own thread, but that's more machinery than a first storage layer needs.

It also supports one optional extra: `set_periodic_hook(interval, callback)` lets a caller run something else on a schedule (right now, an anomaly check) without `Sampler` needing to know or care what that something is — it just calls whatever function it was handed, on schedule. That's how `watch` reuses the exact same recording loop as `record`, just with one more thing plugged in.

Since this phase, it also calls `get_all_process_identities()` every tick and hands the result to a `SystemEventDetector`, buffering and flushing whatever events come back the same way it does metric samples.

### [`core/util/json.hpp`](core/util/json.hpp)
The `json_escape`/`json_num` helpers, factored out once both `main.cpp`'s `--json` output and `sqlite_store.cpp`'s event-payload serialization needed the same small bit of string-escaping logic — better shared once than copied twice.

### [`core/util/strings.hpp`](core/util/strings.hpp)
Just `to_lower()`. Same story as `json.hpp` above, but for a helper that had quietly been copy-pasted into three different files (`system_inventory.cpp`, `gpu.cpp`, and now `power_scheme.cpp`) before finally being worth sharing.

### [`core/anomalies/anomaly_detector.hpp`](core/anomalies/anomaly_detector.hpp) / [`anomaly_detector.cpp`](core/anomalies/anomaly_detector.cpp)
The "fast brain," generalized (Phase 7) — no AI, just arithmetic on stored history, and no longer tied to any one metric. Each `check()` call, parameterized by a `metric` (e.g. `cpu.utilization`) and a `domain` (e.g. `"cpu"`, matching the incidents table):
1. **Gate:** refuses to evaluate anything until history for that metric goes back at least `min_history_days` (14 by default) *and* has at least `min_baseline_samples` actual readings in that window — a technically-old-enough database that's mostly empty still won't trigger.
2. **Baseline:** mean + standard deviation over that same trailing window.
3. **Right now:** mean over the last 5 minutes.
4. **Rule:** flag an anomaly if "right now" exceeds `max(baseline_mean + 2.5·stddev, baseline_mean·1.5)` — whichever is the more forgiving of the two, so a very quiet, low-variance baseline doesn't get flagged by trivially small bumps.
5. **Incident lifecycle:** opens one incident on first detection, recognizes it's already open on every subsequent check (no duplicate spam), and resolves it once the metric drops comfortably back near baseline (a deliberate hysteresis gap, so a reading right at the edge doesn't flip open/resolved on every check).

This class didn't exist until there were two real domains to prove it against (memory, cpu) — the original battery-only version (Phase 3) is what's described above; generalizing it a phase later, once the abstraction had something real to justify it, was a deliberate sequencing choice, not an oversight.

### [`core/anomalies/battery_detector.hpp`](core/anomalies/battery_detector.hpp) / [`battery_detector.cpp`](core/anomalies/battery_detector.cpp)
Now a thin wrapper (Phase 7) around `AnomalyDetector`, configured with `battery.discharge_watts`/`"battery"` and translating `AnomalyCheckResult` back to `BatteryCheckResult`'s own enum names (`kNoRecentDischarge` instead of the generic `kNoRecentSamples`, etc.) — so the CLI's `check-battery --json` shape and Python's `schemas.py` didn't need to change when this refactor happened. Regression-tested against the exact seeded scenario from Phase 3 to confirm identical behavior (same incident ID format, thresholds, and ongoing/no-duplicate logic).

Because there are no recent `battery.discharge_watts` rows at all while a laptop is plugged in (the sampler only emits that metric while actually discharging — see `sampler.cpp` above), "no recent discharge samples" doubles as "we're on AC right now," with no separate flag needed — this is specific to battery, which is why the generic detector's equivalent result is named the more neutral `kNoRecentSamples`.

### [`core/actions/power_scheme.hpp`](core/actions/power_scheme.hpp) / [`power_scheme.cpp`](core/actions/power_scheme.cpp)
Everything to do with Windows power schemes and the Power Mode slider. `enumerate_power_schemes()`/`get_active_power_scheme()`/`set_active_power_scheme()` wrap the *classic* multi-scheme API (`PowerEnumerate`, `PowerGetActiveScheme`, `PowerSetActiveScheme`) — built first, then discovered to be a dead end for this actual machine (only one classic scheme, "Balanced," is registered here; `powercfg /list` agrees). The functions this project's action actually uses are `get_dc_power_mode_raw_guid()`/`set_dc_power_mode()`/`set_dc_power_mode_raw_guid()`, wrapping the Windows 11 "Power Mode" slider API instead (`PowerGetUserConfiguredDCPowerMode`/`PowerSetUserConfiguredDCPowerMode`) — this works regardless of how many classic schemes exist.

The two named `PowerModeLevel` GUIDs (`kOverlayBestPowerEfficiency`, `kOverlayBestPerformance`) are **not** the classic `GUID_MAX_POWER_SAVINGS`-style constants from `winnt.h`, even though they serve an analogous role — that assumption was made once, and disproven by testing live on this machine (the API call succeeded but returned a third, different GUID). The real values were found by enumerating with `ACCESS_OVERLAY_SCHEME` and reading each GUID's friendly name back from Windows itself, then cross-checking the "Better Battery-life Overlay" one against what `PowerGetUserConfiguredDCPowerMode()` actually returned live. Rollback deliberately works on the *raw GUID string*, not a named `PowerModeLevel` — it restores the exact prior value, including a "Balanced"/no-overlay state this code has no name for, rather than an approximation of it.

### [`core/actions/action_broker.hpp`](core/actions/action_broker.hpp) / [`action_broker.cpp`](core/actions/action_broker.cpp)
The PRD's Action Broker, and the only path from a recommendation to an actual change anywhere in this codebase — not the Python agent, not the LLM, nothing else. `execute()`:
1. **Permission check:** an explicit allowlist (`action_type == "change_power_mode"`, currently the only recognized action). Anything else is refused before any state is read.
2. **Dry run by default:** `approved=false` computes and returns the exact previous/new state so a caller can see precisely what would happen, but changes nothing and logs nothing.
3. **Execute + verify:** `approved=true` applies the change, then independently re-reads the state to confirm it actually took — never trusting the Windows API's return code alone.
4. **Always audit:** every approved attempt is logged via `SqliteStore::record_action()`, success or failure — matching the PRD's "every autonomous investigation should be reproducible" requirement.

`rollback()` looks up a prior action's audit record and restores its exact `previous_state`, then logs the rollback itself as its own audit entry (so the trail stays complete) without offering a rollback-of-a-rollback, which isn't a concept this project needs.

### [`third_party/sqlite/`](third_party/sqlite)
The SQLite database engine itself — `sqlite3.c` and `sqlite3.h`, downloaded directly from sqlite.org and compiled straight into our program. This is the normal way to use SQLite in a C/C++ project: it's public domain, this is officially how the SQLite team recommends including it, and it avoids needing a package manager just for one dependency.

### [`core/cli/main.cpp`](core/cli/main.cpp)
The dashboard, now with thirteen modes (`status`, `history`, `check-battery`/`check-memory`/`check-cpu`, `watch`, `events` also accept `--json`):
- `status [--json]` — the original one-shot report (unchanged behavior).
- `record [--db path]` — starts the sampling loop against a SQLite file, runs until Ctrl+C.
- `history <metric> [--last minutes] [--db path]` — reads back stored history and prints min/avg/max.
- `check-battery` / `check-memory` / `check-cpu` `[--db path] [--min-history-days n]` — runs one anomaly check right now for that domain, backed by the same generic `AnomalyDetector`.
- `watch [--db path] [--min-history-days n]` — `record`, plus battery + memory + cpu anomaly checks every 60 seconds, in one loop.
- `inspect [--db path]` — inventory + live GPU state + (with `--db`) recent events, all on one screen.
- `events [--last minutes] [--limit n] [--db path]` — recent process/power events.
- `act change-power-mode --level <...> [--reason text] [--db path] [--yes]` — the Action Broker's one safe action; without `--yes`, prints a dry-run preview and changes nothing.
- `act rollback <action-id> [--db path] [--yes]` — undoes a previous action.
- `actions [--last n] [--db path]` — the action audit log.
- `power-schemes` — debug view of every classic scheme and Power Mode overlay Windows reports, plus which one is currently active.

The `--json` output uses the hand-written helpers in `core/util/json.hpp`, not a JSON library — the object shapes here are small and fixed, so a real library would be machinery this project doesn't need yet, the same call made about not vendoring a package manager just for SQLite. This JSON is the entire contract the Python agent depends on.

### [`intelligence/schemas.py`](intelligence/schemas.py)
Pydantic models that mirror the CLI's JSON output exactly — `BatterySnapshot`, `SystemSnapshot`, `MetricHistory`, `Incident`, `BatteryCheckReport`, `SystemEvent`/`EventsResult`, `ActionOutcome`, and a generic `Reading[T]` mirroring `core/model/availability.hpp`'s `Reading<T>` field-for-field (`value` + `ok`/`unsupported`/`unavailable`/`error`). `GpuState` uses `Reading[T]` for every metric, so the availability distinction survives the C++ → JSON → Python round trip intact. Pydantic validates the shape on the way in, so if the C++ side's JSON ever drifts, it fails loudly right here instead of as a confusing bug three layers into the agent's reasoning.

### [`intelligence/tools.py`](intelligence/tools.py)
The agent's *only* way of touching the machine — this is literally the PRD's "Diagnostic Tool Interface." `SysIntelClient` exposes `get_system_snapshot` (now includes `gpu`), `get_metric_history`, `get_battery_anomaly_status`, `get_recent_events`, and — new this phase — `apply_change_power_mode()`/`rollback_action()`, each calling one fixed `sysintel.exe` subcommand via `subprocess.run` with a list of arguments, never a shell string. There is no method on this class that could execute an arbitrary command; the agent literally cannot construct one. `apply_change_power_mode()` defaults `approved=True` deliberately: by the time anything calls it, a human has already said yes (interactively in `main.py`, or via `--auto-approve`) — the real approval gate lives one layer up, not here.

### [`intelligence/llm.py`](intelligence/llm.py)
The entire LLM boundary, and nothing more than that. `select_hypothesis()` takes the fixed hypothesis list and whatever evidence `agent.py` already gathered deterministically, and asks Groq to (a) pick a winner from the *given* IDs — never invent a new one — and (b) write the confidence/finding/recommendation. The model has no tools and never touches the machine; every fact it reasons over was collected by plain function calls before it was ever consulted. Output is constrained to JSON validated against `LlmDiagnosisResult`, so a malformed or off-script response raises `LlmUnavailableError` instead of silently corrupting what gets printed. Reads `GROQ_API_KEY`/`GROQ_MODEL` from `intelligence/.env` (via `python-dotenv`), which is gitignored — the repo only ships `.env.example` as a template.

**Note on the project's own confidence-scoring principle:** the tech design explicitly says not to let an LLM invent confidence numbers, preferring a fixed evidence-based rubric. This module deliberately does let the model set confidence, because that's specifically what was asked for in this round — verified in testing that it behaves reasonably (75% when CPU evidence strongly supports the winning hypothesis, 30% when it doesn't), but this is a conscious deviation from that earlier principle, not an oversight.

### [`intelligence/agent.py`](intelligence/agent.py)
The reasoning loop: TRIAGE → GENERATE HYPOTHESES → COLLECT EVIDENCE → EVALUATE/DIAGNOSIS (now delegated to `llm.py`) → EXPLAIN. `BatteryDiagnosticAgent.diagnose()`:
1. Checks whether there's actually an open battery incident (TRIAGE) — bails out honestly if not, or if there isn't enough history yet.
2. Holds three fixed hypotheses: CPU workload (H1), GPU activity (H2), or unexplained (H3).
3. Deterministically gathers CPU history (24h baseline vs last 5 minutes), current GPU state (including its availability), the process list, and — new this round — recent events filtered to a window around the incident's start time, giving a list of processes that started right before the anomaly began.
4. Hands hypotheses + evidence to `llm.select_hypothesis()`.
5. If that raises `LlmUnavailableError` (no key, network failure, bad response), `_fallback_diagnose()` takes over: the same CPU-threshold logic the rule-based version always used, explicitly labeled as a fallback in its own evidence line rather than silently pretending to be the LLM path.

Every `Diagnosis` carries a `reasoned_by` field (`"llm"` or `"rule-based"`), and `main.py` always prints which one actually ran — never letting a degraded response masquerade as a full one.

**Phase 6 addition:** `_build_suggested_action(check)` is a plain function, not an LLM call — given an open/ongoing anomaly, it always proposes the same `change_power_mode`/`best_power_efficiency` action, wrapped in a `SuggestedAction` (`action_type`, `params`, `reason`, `description`) and attached to every `Diagnosis` regardless of which path produced it (LLM or fallback) or which hypothesis won. The LLM's `recommended_action` text and this `suggested_action` value are two separate things: one is prose the model wrote, the other is a fixed, code-computed action_type/params pair the Action Broker already recognizes. Only the second one can ever actually execute.

### [`intelligence/main.py`](intelligence/main.py)
The Python entry point — `python -m intelligence.main diagnose-battery [--db path] [--sysintel-exe path] [--min-history-days n] [--auto-approve] [--no-act]`. Wires a `SysIntelClient` to a `BatteryDiagnosticAgent`, prints which reasoning path ran, then the diagnosis. Also reconfigures stdout to UTF-8 on the way in — Windows' console defaults to a legacy codepage that can't encode a lot of ordinary Unicode punctuation an LLM will happily produce (hit this for real: a narrow no-break space in one response crashed the print before this fix).

`handle_suggested_action()` is the actual approval gate for Phase 6's closed loop: if `diagnosis.suggested_action` is set and `--no-act` wasn't passed, it prints the action's description and asks `Apply this now? [y/N]` — unless `--auto-approve` skips the prompt. Only on a yes does it call `tools.apply_change_power_mode()`, then prints the resulting `ActionOutcome` the same way the CLI's own `sysintel act` would. Declining prints the exact equivalent `sysintel act` command instead, so nothing is lost by saying no.

### [`intelligence/requirements.txt`](intelligence/requirements.txt)
`pydantic`, `groq`, and `python-dotenv`. Installed into its own virtual environment (`intelligence/.venv`, gitignored) rather than system-wide — keeps this project's dependencies from colliding with anything else on the machine.

### [`intelligence/.env.example`](intelligence/.env.example)
Template for `intelligence/.env` (gitignored, never committed): `GROQ_API_KEY` and `GROQ_MODEL`. Copy it, fill in a real key.

### [`CMakeLists.txt`](CMakeLists.txt)
The build recipe. Tells the compiler:
- which `.cpp` files to compile (collectors, storage, sampler, anomaly detector, inventory, events, GPU dispatch, WMI/NVML providers, power scheme + action broker, CLI) plus `sqlite3.c` as a C file,
- to use C++20 for our own code,
- where to find `sqlite3.h` (`third_party/sqlite`),
- and to link against `pdh`, `powrprof`, `psapi`, `wbemuuid` (the last for WMI's COM API) — Windows' own pre-built libraries containing the real implementations of the functions we called. Without naming these, the compiler wouldn't know where those functions actually live. NVML needs no new link library at all, since it's loaded dynamically at runtime with `LoadLibrary` rather than linked at build time.

## Building and running it

Requires MSVC (Visual Studio Build Tools with the "Desktop development with C++" workload) and CMake — both come bundled together if you install Build Tools via `winget install Microsoft.VisualStudio.2022.BuildTools --override "--add Microsoft.VisualStudio.Workload.VCTools"`.

```powershell
# from a "Developer Command Prompt" or after running vcvars64.bat
cmake -S . -B build -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build

.\build\sysintel.exe status
.\build\sysintel.exe record --db sysintel.db          # Ctrl+C to stop
.\build\sysintel.exe history cpu.utilization --last 30 --db sysintel.db
.\build\sysintel.exe watch --db sysintel.db           # records + checks battery every 60s, Ctrl+C to stop
.\build\sysintel.exe check-battery --db sysintel.db   # one-shot anomaly check
.\build\sysintel.exe inspect --db sysintel.db         # inventory + live GPU state + recent events
.\build\sysintel.exe events --last 30 --db sysintel.db
.\build\sysintel.exe power-schemes                    # debug view of schemes/Power Mode overlays

.\build\sysintel.exe act change-power-mode --level best_power_efficiency --reason "battery drain" --db sysintel.db
# ^ prints a dry-run preview only -- add --yes to actually apply it
.\build\sysintel.exe act rollback <action-id> --db sysintel.db --yes
.\build\sysintel.exe actions --db sysintel.db          # audit log of every action taken
```

Battery anomaly checks require at least 14 days of accumulated `record`/`watch` history by default before they'll evaluate anything — pass `--min-history-days <n>` to override this for local testing against a shorter or synthetic dataset.

The Python agent needs its own one-time setup, including a Groq API key for the LLM reasoning step (get one at console.groq.com — free tier is enough for this):

```powershell
python -m venv intelligence\.venv
intelligence\.venv\Scripts\pip install -r intelligence\requirements.txt

copy intelligence\.env.example intelligence\.env
# now edit intelligence\.env and paste your real GROQ_API_KEY

# once there's an open incident in the database (see `check-battery` above):
intelligence\.venv\Scripts\python -m intelligence.main diagnose-battery --db sysintel.db --sysintel-exe build\sysintel.exe

# add --auto-approve to apply the suggested action without an interactive prompt,
# or --no-act to never even offer one (diagnosis only)
```

No key configured, or the API call fails for any reason? The agent automatically falls back to deterministic rule-based reasoning and says so explicitly (`[reasoned by: rule-based fallback]`) — it never silently produces a degraded result while claiming full reasoning.

## What's next

The stated direction now is comprehensive coverage: "all the information, diagnose everything," not just battery. Phase 7 generalized *detection* to three domains; what it deliberately didn't touch:

- **The Python agent still only diagnoses battery incidents.** `check-memory`/`check-cpu` will happily open incidents, but nothing investigates *why* yet — `BatteryDiagnosticAgent` needs to become domain-aware (or a `MemoryDiagnosticAgent`/`CpuDiagnosticAgent` need to exist alongside it), with per-domain hypothesis sets. This is the natural next slice.
- **New collectors**, still needed for real domain coverage: network (a whole original MVP domain with zero coverage so far), disk I/O throughput/IOPS (currently only static disk *inventory* exists, no live activity), per-process CPU attribution (a known gap since Phase 1), and thermal/fan sensors (likely to surface mostly `unsupported` per the PRD's own warning about OEM fragmentation, but worth trying with the `Reading<T>` type already built for exactly this).
- **A second safe action**, something CPU-drain-specific (e.g. disabling a startup application) rather than only battery-mode — once it exists, `_build_suggested_action()` needs real logic for *which* action fits *which* winning hypothesis/domain, instead of always proposing the same one regardless.

Still separately outstanding: the NVML path needs verification on real NVIDIA hardware (everything checkable without it has been); `best_performance`'s GUID mapping wants the same live cross-check `best_power_efficiency` already got; hypothesis-selection confidence is LLM-set (a conscious, documented departure from the tech design's §32 "don't let the LLM invent confidence" principle, which still holds for every deterministic part of this system); and AMD/Intel GPU providers, true ETW-based event tracking, retention/rollup, and the eventual named-pipe IPC all remain deliberately deferred until their absence actually starts costing something.
