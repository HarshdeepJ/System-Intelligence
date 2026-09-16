# UI Handoff — Living Halo Desktop Presence

Status as of 2026-09-16: the Living Halo WPF surface is implemented, runnable,
and wired to live telemetry through a small deterministic state arbiter, and
Pixel can now also be asked free-form questions in a small chat panel,
answered by an LLM in character and grounded in a live snapshot. `core/`
gained a small, additive extension (below) to expose data the CLI was
already collecting but hadn't put in JSON form yet — no new anomaly domain,
no Action Broker changes. `intelligence/` gained one new, narrow LLM entry
point (`chat.py` / the `ask` CLI command) alongside its existing
hypothesis-selection one; nothing about the diagnostic agent itself changed.

**Design pivot approved and implemented:** the character has been replaced by
the **Living Halo**, a formless ambient light attached to the top-center screen
edge. The production WPF shell now uses the refined opalescent
white/cyan/periwinkle/lilac/blush profile, with warmer honey/coral concern
states and mint/sky recovery states. The throwaway comparison remains in
`ui/GlowPrototype/` as design history; Living Halo is the selected direction.

## Product idea — settled

The halo does not merely monitor the computer. It represents the computer.
Instead of reporting "CPU temperature is 91°C," the halo warms and says,
"Um… I’m getting a little warm."

The intended personality is innocent, observant, slightly vulnerable, and
quietly competent. The presence should feel worth looking after without becoming
needy or distracting. Target roughly 90% silent physical behavior and 10%
dialogue. Serious problems must still use direct, unambiguous language.

The halo is the product surface. Users should not need to know about the CLI,
SQLite database, Python agent, or LLM behind it.

## Decisions already made

- Use WPF for the native UI. This deliberately replaces the PRD's WinUI 3
  choice because WPF provides transparent layered windows with much less setup.
- The halo lives at the top-center screen edge.
- It starts fully tucked above the screen with no persistent glint. It appears
  only after the pointer remains inside an 84-by-6-logical-pixel activation
  strip at the top center for 180 ms.
- Once visible, the halo stays while hovered or while its card/context menu is
  open. After ten seconds away it retreats below the screen edge again.
- Use a real installed Windows Service for eventual background collection.
  Service mode and install/uninstall commands do not exist yet.
- Defer named-pipe IPC. The UI can initially poll existing CLI JSON commands
  and use the same SQLite file as the Python agent.
- Keep all actions behind explicit human approval.

## What exists now

The implementation is under `ui/PixelMini/`:

- `PixelWindow.cs` — transparent always-on-top window, top-center positioning,
  halo palettes and motion, card, bubble, context menu, edge activation,
  telemetry polling/timers, and built-in visual/layout checks.
- `Telemetry.cs` — `TelemetryClient` (locates and shells out to
  `build/sysintel.exe`'s JSON commands off the UI thread) and
  `TelemetrySnapshot` (the typed live picture of the machine it parses into).
- `PixelStateArbiter.cs` — the single state-arbiter function: live readings in,
  one dominant `ExpressionKind` + card subtitle + optional cooldown-gated
  message out. Pure and independently testable (no WPF dependency).
- `Json.cs` — a minimal hand-rolled JSON reader (mirrors `core/util/json.hpp`'s
  philosophy). Needed because the csc.exe fallback build path has no
  `System.Text.Json` available.
- `Chat.cs` — `PixelChatClient`, the bridge to `intelligence/main.py ask` for
  the chat feature (see "Chat with Pixel" below).
- `Program.cs` — app entry point and smoke-test modes.
- `Assets/pixel-mini-blank-face.png` and `Assets/pixel-mini.png` — historical
  character artwork retained as source material; the live halo does not use it.
- `app.manifest` — DPI-aware process declaration.
- `build.ps1` — fallback build for this machine, which has the WPF runtime and
  Visual Studio compiler but no `dotnet` SDK. Updated to compile the new
  files above alongside `Program.cs`/`PixelWindow.cs`.
- `PixelMini.csproj` — normal .NET 8 WPF project for machines with an SDK
  (globs `*.cs`, so the new files need no csproj edit there).

Current behavior:

- DPI-aware, code-native WPF glow; no bitmap or character is visible.
- A 260-by-72 logical-pixel transparent compact surface, visually concentrated
  into a thin top-edge halo.
- Continuous restrained breathing, core shimmer, and a slowly drifting wisp.
- Animated calm, listening, working, warm, worried, sleepy,
  happy/charging, and offline color profiles.
- Left-click toggles a 48-pixel-high translucent health strip: compact status
  at left, followed by CPU, memory, temperature, battery, and disk on one row.
  There are no health ticks, dividers, shadows, or telemetry footer.
- Right-click opens chat/voice controls, previews system states, and provides
  **Quit System Intelligence**
  (previewing a state sets a `_previewActive` flag that pauses telemetry-driven
  expression/message updates until the preview bubble's own 5s timer hides it,
  so a manual preview never gets stomped by the next 5s poll).
- The health card shows real CPU / memory / battery / disk-capacity-used
  percentages and best-effort temperature, refreshed every 5 seconds. Any
  reading the machine genuinely doesn't have (this dev machine's thermal
  zones report `unsupported`, for example) renders as `n/a`/`—`, never as a
  fabricated zero.
- If `build/sysintel.exe` can't be located (walks up from the UI's own
  directory looking for it, or honors a `SYSINTEL_EXE` env var override), the
  card footnote says so explicitly and keeps showing static preview values
  rather than silently lying about being live.

### The telemetry/state pipeline

1. **C++ side** (`core/cli/main.cpp`): `status --json` now also includes
   `"thermal"` (zones + fans, cheap — no rate-counter sampling needed) and
   `"disk_space"` (`total_bytes`/`free_bytes` for the Windows system drive,
   via `GetDiskFreeSpaceExW`, a small helper added directly in `main.cpp`
   rather than a new collector module). `thermal`, `disk`, and `network` (the
   text-mode CLI commands) each gained a `--json` flag mirroring their
   existing text output, for symmetry and because `network --json` is used
   on its own slower poll cadence (see below). None of this touches the
   anomaly detectors or the Action Broker.
2. **`TelemetryClient`** polls two things on separate `DispatcherTimer`s so
   the ~1-second-per-call cost of network sampling doesn't slow down the
   more time-sensitive readings:
   - `status --json` every 5s → CPU%, memory load%, battery, thermal,
     disk capacity.
   - `network --json` every 20s → whether any physical adapter is
     operational (used only for the "internet disappeared" state; an empty
     adapter list is treated as *unknown*, not offline, since that shape is
     more likely a VM quirk than a real outage).
   Each call runs `Process.Start` + `WaitForExit` on a background thread with
   a timeout, so a hung or missing `sysintel.exe` can never block the UI
   thread; a failed/timed-out call just skips that tick and keeps the last
   good reading on screen.
3. **`PixelStateArbiter.Evaluate(snapshot, now)`** holds nine ordered
   condition specs (critical battery/thermal/disk > network lost > warm
   thermal > low battery > charging > high CPU/memory), each with its own
   dwell time before it's allowed to win, plus a 20s "stayed healthy" dwell
   before reverting to Calm and a 10-minute per-condition message cooldown.
   Verified directly (bypassing the UI) with a standalone harness that fed it
   synthetic snapshots across simulated time and confirmed: a transient spike
   is ignored, a sustained one flips state exactly at the dwell boundary and
   speaks once, repeats stay silent under cooldown, and recovery holds the
   prior state until its own dwell clears. The same harness round-tripped a
   live `status --json`/`network --json` call through the real parser against
   this machine's actual numbers.

### Known simplifications (deliberate, worth revisiting)

- **No anomaly-detector input yet.** The arbiter is threshold-based on live
  readings only; it doesn't yet poll `check-cpu`/`check-battery`/etc. Those
  need `sysintel record`/`watch` to have been running to accumulate history,
  which nothing currently keeps running in the background (that's the
  service-mode milestone below), so they'd mostly return
  `not_enough_history` right now anyway. Worth wiring in as a severity-bump
  signal once the service exists.
- **"Charging" is a sustained mild state, not a one-shot flash.** Per the
  domain table below it reads more like "briefly happy" — Pixel stays Happy
  for the whole time it's plugged in and charging rather than a quick
  acknowledgment that settles back to Calm. A nicer version would
  edge-trigger on the false→true transition and time out after a few
  seconds; not built yet.
- **Disk I/O throughput** (`disk --json`, already exposed on the CLI side) 
  isn't polled — only disk *capacity* used% feeds the card/arbiter. Nothing
  in the domain table needed it yet.

The earlier HTML/Claude Artifact is historical exploration only and no longer
represents the current visual implementation:
`https://claude.ai/artifact/6JdYFn93E1WFj4hkafBGtw`

## Build, run, and verify

From the repository root:

```powershell
powershell -ExecutionPolicy Bypass -File .\ui\PixelMini\build.ps1
.\ui\PixelMini\bin\PixelMini.exe
```

On a machine with a .NET 8 SDK:

```powershell
dotnet run --project .\ui\PixelMini\PixelMini.csproj
```

Verification modes:

```powershell
.\ui\PixelMini\bin\PixelMini.exe --smoke-layout
.\ui\PixelMini\bin\PixelMini.exe --visual-check
```

`--smoke-layout` verifies top-center edge gating and the fully-hidden → reveal →
expanded-card sequence, including horizontal centering after a resize.

`--visual-check` renders a 3× preview to
`ui/PixelMini/bin/visual-check.png` and exits. The entire `bin/` directory is
ignored.

Both `--smoke-layout` and `--visual-check` skip telemetry polling entirely
(they're static/deterministic tests; live process calls would make them
flaky). `TelemetryClient` walks up from its own directory looking for
`build/sysintel.exe`; set the `SYSINTEL_EXE` environment variable to point at
a specific build if that search doesn't find the right one.

## Visual and behavior rules

- Do not give the presence a face, body, container, or other physical form.
- Do not turn the palette into saturated rainbow or gaming-RGB effects; keep
  the opalescent white/cyan/periwinkle/lilac/blush hierarchy.
- Do not add continuous rocking, bouncing, or exaggerated squash/stretch.
- Do not wake the halo merely because the pointer is vaguely nearby.
- Light, color temperature, intensity, and motion are its communication channels.
- Brief load spikes may cause a glance but no message.
- Sustained conditions change expression first, then optionally speak.
- The system speaks in first person: "I’m juggling quite a lot right now."
- Prefer "Could we…?" over "You should…".
- Critical states drop most of the cute wording and show exact technical data.
- Keep the normal footprint small and the screen mostly unobstructed.

## Live telemetry and state engine — done

The milestone that used to be described here (telemetry adapter, 5s live
poll, real card values that never fake unavailable-as-zero, a single
hysteresis-driven state arbiter, deterministic state-to-language mapping) is
implemented — see "What exists now" and "The telemetry/state pipeline"
above, plus "Known simplifications" for what's deliberately still rough
(no anomaly-check input yet, charging isn't a one-shot flash, disk I/O
throughput unused). Diagnose-on-sustained-anomaly and **Tell me more** (see
"Later milestones" below) were intentionally left for later since they
depend on the anomaly-check integration that's also still pending.

Actual domain mapping (`PixelStateArbiter.cs`'s condition specs, most severe
first: battery/thermal/disk-critical → network-lost → thermal-warm →
battery-low → charging → cpu/memory-high):

| System condition | Pixel state | Example language |
|---|---|---|
| Healthy | Calm | Usually silent |
| Sustained high CPU/GPU | Working | “I’m working pretty hard right now.” |
| High temperature | Warm | “Um… I’m getting a little warm.” |
| Very high temperature | Worried | “Could we let me cool down for a bit?” |
| Low battery | Sleepy | “I’m getting sleepy…” |
| Critical battery | Worried | “Could you plug me in?” |
| Charging | Happy | “Ahh, thank you.” |
| Heavy memory use | Working | “I’m trying to remember a lot right now.” |
| Disk nearly full | Worried | “I’m running out of room in here.” |
| Network lost | Offline | “Did the internet disappear?” |
| Network restored | Happy briefly | *not implemented* — network recovery currently just settles back to Calm via the normal 20s recovery dwell, no distinct "Found it!" flash |

## Chat with the system — done

Use **Ask the system…** in the halo's right-click menu to open a translucent,
centered command bar just below the top edge. Typed input submits with Enter or
the arrow button. During voice capture, SAPI hypothesis events stream the
current transcription into that same field, so speech visibly forms as text.
On submission the composer lifts and fades upward; once the answer is ready,
the response surface fades into view and a minimal follow-up field appears
beneath it with keyboard focus already restored. The empty field fades away
after five seconds without typing or voice input; the response remains visible.
Any entered text cancels that inactivity timer. Conversation history is kept
for the session (so "and what about the CPU?" correctly reads as a follow-up
to a prior memory question). Voice wake opens the same bar. Opening chat closes
the health card/ambient bubble and vice versa; an open chat also suppresses the
telemetry arbiter's ambient messages and the 10s peek-retreat timer so a
live conversation is never interrupted or yanked off-screen mid-reply.

Pieces:

- **`intelligence/chat.py`** (new) — `ask_pixel(question, snapshot, history)`
  calls Groq (reusing `llm.py`'s API-key/model env handling) with a
  Pixel-persona system prompt, constrained to `{"answer": "..."}` JSON, same
  discipline as `llm.py`'s hypothesis selection: never invents a reading
  outside the snapshot it was handed, admits when something (e.g. this dev
  machine's `unsupported` thermal) isn't knowable rather than guessing, and
  deflects off-topic questions in character. `fallback_answer()` produces a
  plain, still-in-character readout from the raw snapshot if the LLM step
  can't run at all (no API key, network down, bad response) — chat degrades,
  it doesn't go silent or crash.
- **`intelligence/main.py`**: new `ask` subcommand. Reads its request as one
  JSON object from stdin (`{"question": ..., "history": [{"role": "user"|
  "pixel", "text": ...}]}`) rather than argv, so arbitrarily long or
  quote-heavy chat text never has to survive Windows command-line escaping;
  always prints `{"answer": ..., "reasoned_by": "llm"|"fallback"}`, exit 0
  either way.
- **`intelligence/tools.py`**: added `get_raw_status()` (the same `status
  --json` call as `get_system_snapshot()`, minus the pydantic validation) so
  `chat.py` can hand the LLM fields like `thermal`/`disk_space` without every
  one needing a `SystemSnapshot` schema entry.
- **`ui/PixelMini/Chat.cs`** (new) — `PixelChatClient`, mirroring
  `TelemetryClient`'s shell-out-per-call pattern but talking to
  `intelligence/.venv/Scripts/python.exe -m intelligence.main ask` instead of
  `sysintel.exe`, over stdin/stdout rather than argv/status-code.
- **`PixelWindow.cs`**: the chat icon, the chat panel (message list +
  input box), and the mutual-exclusion/suppression wiring described above.

**A real gotcha worth knowing before touching this again**: writing to
`Process.StandardInput.BaseStream` directly (bypassing its `StreamWriter`,
done deliberately so chat text round-trips as UTF-8 regardless of the
system codepage) still gets a UTF-8 BOM prepended by .NET on the wire —
confirmed by a debug round-trip, not theoretical. Rather than fight that on
the .NET side, `main.py` reads stdin as `utf-8-sig` (tolerates a leading BOM,
behaves exactly like plain `utf-8` if there isn't one), which is correct
regardless of which .NET build path (`csc.exe` fallback vs. SDK) ends up
writing that stdin. If a future stdin-based bridge is added here, decode it
the same way.

**Verified**, not just built: a standalone non-WPF harness (same technique
used for the telemetry milestone) round-tripped real questions through
`PixelChatClient` -> the real Python backend -> Groq, including a multi-turn
follow-up that correctly used conversation history, a question with curly
quotes and an em dash (stresses both the hand-rolled JSON writer and the
UTF-8 fix above), an off-topic deflection, and an honest "I can't tell"
answer for this machine's unsupported thermal reading.

### Chat can also propose actions — done

The user asked "can it also do tasks for me, like close an app?" — answer:
via chat, yes, but through the same architecture as everywhere else in this
project, never by letting the LLM execute anything itself.

- **`chat.py`**: the system prompt lets the model include a
  `proposed_action` field in its JSON reply, but only ever one of exactly two
  fixed shapes (`suspend_process` with a `pid`, or `change_power_mode` with a
  `level`) — "never invent a third" is stated explicitly, same as
  `llm.py`'s "only ever pick from the given hypothesis IDs." `_sanitize_action()`
  then re-validates in plain code before the action ever leaves the process:
  `action_type` must be in a fixed allow-list, `level` must be one of the two
  real levels, and a `suspend_process` pid must actually appear in the
  snapshot's `top_processes_by_memory`/`top_processes_by_cpu` lists (both now
  included in what `cmd_ask` hands the model) — an invented pid is silently
  dropped, not passed through. If asked to "close" or "quit" something, the
  prompt requires the answer say plainly that only *pausing* (resumable) is
  possible, never actually closing it — there is still no terminate action
  anywhere in this codebase, on purpose (see `core/actions/process_control.hpp`).
- **`PixelWindow.cs`**: `IsActionWellFormed()` re-checks the same constraints
  again on the UI side (defense in depth, never trust a subprocess's JSON
  blindly) before showing anything. A well-formed proposal renders as a
  message with **Confirm**/**Dismiss** — nothing runs until Confirm is
  clicked. Confirm calls `sysintel.exe act suspend-process`/`act
  change-power-mode ... --yes --json` directly (`ExecuteChatActionAsync()`,
  via `TelemetryClient.RunJsonAsync` — the exact same binary and JSON shape
  the CLI itself uses, not a separate code path) and shows the outcome; a
  successful action also gets an **Undo** link that calls `act rollback
  <id> --yes --json`, reusing the Action Broker's existing rollback/audit
  trail. Chat's `--db` points at `<repo root>/sysintel.db` explicitly, so the
  audit log lands in the same place regardless of PixelMini's own working
  directory.
- Python never executes anything — it only ever proposes and validates.
  Execution and the human click both live entirely in the C# UI, calling the
  same `act` command path already covered by "Phase 12" (suspend, not
  terminate, the Action Broker's protected-process list still applies
  underneath this regardless of what chat validates, so this is defense in
  depth on top of an already-safe execution layer, not the only thing
  standing between a bad proposal and something happening).

**Verified**: the Python side directly (three real Groq round-trips — a
memory-pressure "pause the biggest app" request that came back with a real,
correct pid; a "switch to power saving mode" request; and a plain question
confirming no action gets proposed when nothing was asked for) and the C#
parsing directly (the same three requests through `PixelChatClient.AskAsync`,
confirming `ChatAction`'s fields round-trip correctly). The `act` JSON shape
`ExecuteChatActionAsync`/`ExecuteRollbackAsync` expect was checked against a
real (dry-run, `--yes` omitted, nothing changed) `sysintel.exe act
change-power-mode` call. The Confirm/Undo click path itself — actually
suspending a real process or changing the real power mode — was
deliberately left for the user to try themselves rather than automated,
since unlike everything else verified so far, clicking Confirm has a real,
visible effect on their machine.

**Real bug found via that first live Confirm click, now fixed**:
`change_power_mode` only ever set the DC (on-battery) Power Mode slider
(`PowerSetUserConfiguredDCPowerMode`) — Windows tracks the AC (plugged-in)
slider completely separately, and the one visible in Settings while plugged
in is the AC one. So the action was genuinely applying, just to a setting
nobody was looking at. Fixed in `core/actions/power_scheme.hpp`/`.cpp`
(added `get_ac_power_mode_raw_guid()`/`set_ac_power_mode()`/
`set_ac_power_mode_raw_guid()`, mirroring the DC versions exactly) and
`action_broker.cpp` (`handle_change_power_mode`/`rollback_change_power_mode`
now set/restore both together). `sysintel.exe power-schemes` now prints both
sliders side by side — that's what caught it. Verified end-to-end: applied
`best_performance`, confirmed both AC and DC flipped, then reasoned through
the rollback path (a genuine no-op case returns `executed:false` with no
`action_id`, which is why that specific rollback call returned "no such
action id" during verification — not a bug, `action_id` is only ever minted
for a real state change).

## Talk to Pixel — done

The user asked for "Hey Google"-style voice activation. Right-click →
**Enable voice ("Hey Pixel")** turns on a wake-word listener; say "Pixel" or
"Hey Pixel", it wakes up, opens the chat panel, listens for one question,
answers in the panel same as typed chat, and *also* speaks the answer aloud
(typed chat never gets spoken, only voice-initiated questions do). Off by
default — this opens the microphone, so it's opt-in only, never silently
running. Chosen deliberately **entirely local**: both the wake word and the
follow-up question are transcribed on-device via Windows' built-in
`System.Speech.Recognition` (SAPI) — nothing about your voice ever leaves
the machine. (The alternative — wake word local, but the actual question
sent to Groq Whisper for better accuracy — was offered and explicitly
declined in favor of nothing-leaves-the-machine, at the cost of SAPI's
free-dictation accuracy being noticeably weaker than a cloud model for
natural speech.) Replies are still spoken via Windows' own
`System.Speech.Synthesis` (also local, zero added cost).

Pieces:

- **`ui/PixelMini/VoiceAssistant.cs`** (new) — `SpeechRecognitionEngine` with
  two grammars loaded simultaneously: a small wake-word `Grammar` ("Pixel" /
  "Hey Pixel") always enabled, and a `DictationGrammar` (free speech)
  disabled until the wake word fires. On wake word (confidence ≥ 0.6): swap
  which grammar is enabled, start an 8-second hard-timeout `DispatcherTimer`
  backstop (SAPI's own end-of-speech detection doesn't always trip promptly
  for natural pauses), fire `WakeWordDetected`. On a dictation result: swap
  back, fire `QuestionCaptured(text)`. `Speak(text)` drives
  `SpeechSynthesizer.SpeakAsync` for replies. Every entry point
  (`Start()`/`Speak()`) catches broadly and reports unavailable rather than
  throwing — no microphone, no installed recognizer for this Windows locale,
  and permission-denied all degrade the same way chat/telemetry do.
- **`PixelWindow.cs`**: the context-menu toggle (`ToggleVoice`), and three
  handlers (`OnVoiceWakeWordDetected`/`OnVoiceQuestionCaptured`/
  `OnVoiceListenTimedOut`) that all `Dispatcher.BeginInvoke` back to the UI
  thread first, since `VoiceAssistant`'s events fire from the recognition
  engine's own thread. `SendChatMessageAsync` gained a `speakReply` parameter
  (`false` for typed input, `true` for voice) so the exact same chat/action-
  proposal pipeline built for typed chat serves voice too — voice is just a
  different front door into it, not a separate answer path.
- **`build.ps1`**: added the `System.Speech` GAC reference (confirmed
  present on this machine: `GAC_MSIL\System.Speech\v4.0_...`) and
  `VoiceAssistant.cs` to the compile list. **`PixelMini.csproj`**: added a
  `System.Speech` `PackageReference` for the dotnet-SDK build path, since
  unlike the GAC fallback, .NET 5+ needs it as an explicit package even on
  Windows.

**Verified, with a deliberate limit on what I verified myself**: a
standalone harness confirmed two TTS voices are installed (David, Zira) and
that `VoiceAssistant.Start()`/`Stop()` — real `SpeechRecognitionEngine`
construction, grammar loading, opening and releasing the default microphone
— succeeds cleanly on this machine. I did not trigger actual TTS playback or
simulate speaking the wake word myself: the former would have put
unexpected audio out of the user's speakers, the latter isn't something
that can be faked without an actual microphone signal. The full wake-word →
transcribe → answer → spoken-reply loop needs the user's own voice to
confirm — genuinely nothing else can verify it.

**Known limitations, worth setting expectations on**: SAPI's free-dictation
accuracy is noticeably weaker than cloud STT for natural conversational
speech (that's the tradeoff the user explicitly chose over sending audio to
Groq); the wake word can false-positive on unrelated speech/audio containing
"pixel" (inherent to any wake-word system, mitigated but not eliminated by
the 0.6 confidence floor); only one question is captured per wake word (no
continuous/multi-turn voice conversation without saying "Pixel" again).

## Later milestones

1. Implement `sysintel.exe` Windows Service mode with install/uninstall CLI
   commands and boot startup.
2. Wire anomaly-check output (`check-cpu`/`check-battery`/etc. `--json`) into
   the arbiter as a severity-bump signal, now that live thresholds alone
   drive it — see "Known simplifications" above. Depends on something
   keeping `sysintel record`/`watch` running continuously, i.e. milestone 1.
3. Add **Tell me more** and metric-specific diagnostic views (item 7 from the
   original milestone: `diagnose-<domain> --no-act` on a sustained anomaly).
4. Connect approved actions such as `change_power_mode` and
   `suspend_process`, retaining the existing confirmation and audit principles.
5. Add startup/tray lifecycle and activation/display settings.

## Scope boundary

Do not add new anomaly domains or redesign the Action Broker. The `core/cli`
JSON extensions made for the telemetry milestone (thermal/disk-space fields
on `status --json`, `--json` on `thermal`/`disk`/`network`) are the kind of
additive, read-only exception that's fine — they expose data the collectors
already gathered, nothing new is measured or decided. Keep future work
focused on telemetry consumption, state arbitration, and truthful
presentation until the items above are picked up deliberately.
