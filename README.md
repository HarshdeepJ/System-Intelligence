<p align="center">
  <img src="docs/assets/living-halo.png" alt="The Living Halo, a soft ambient light resting at the top edge of the screen" width="640">
</p>

<h1 align="center">System Intelligence</h1>

<p align="center">
  Your computer, watching its own back.
</p>

---

Most of what's wrong with a computer is invisible until it's bad enough to
notice — a fan spinning up, a battery draining twice as fast as usual, a
browser tab quietly eating your memory. Finding out why means Task Manager,
Event Viewer, `powercfg`, and a fair amount of guessing.

**System Intelligence watches instead.** It's a small ambient presence — the
**Living Halo** — that lives at the top edge of your screen, stays out of
the way, and only speaks up when something's actually worth mentioning. When
it does, it doesn't say "CPU temperature is 91°C." It says:

> "Um… I'm getting a little warm."

Underneath that one line sits a full diagnostic agent: real hardware
telemetry, statistical anomaly detection against your machine's own
baseline, and an LLM that investigates *why* before it ever recommends
anything.

## What it actually does

- **Notices, quietly.** A soft breathing light at the top of your screen
  reflects your machine's condition — calm, working hard, warm, low on
  battery, offline — without a dashboard, a tray icon full of numbers, or a
  notification storm. It appears when you reach for it and tucks itself away
  again.
- **Explains itself in plain language.** Ask it anything — "why is my
  battery draining so fast?", "what's using all my memory?" — by typing or
  by voice ("Hey Pixel…"), and it answers in character, grounded in what
  your machine is actually doing right now, not a generic troubleshooting
  script.
- **Diagnoses with evidence, not guesses.** Every anomaly is checked against
  *your* machine's own recent history — battery, CPU, memory, network, disk,
  thermal, fan, the works — not a fixed threshold that's wrong for half the
  laptops on earth. When something's off, the underlying agent gathers real
  evidence (correlated CPU load, recently-started processes, GPU state) and
  hands it to an LLM to reason over before it says anything.
- **Can act, but only with your say-so.** If a fix is genuinely safe and
  reversible — switching your power mode, pausing (never closing) the
  process hogging your CPU or memory — it can offer to do it. Nothing
  happens without a click. Every action can be undone with one more.
- **Keeps your voice on your machine.** Voice wake-word and dictation run
  entirely on-device via Windows' own speech engine. Nothing about how you
  sound leaves your computer.

## Why it's built this way

Three principles shape everything here, and they're enforced in code, not
just in spirit:

1. **Observe before reasoning.** Every diagnosis starts from real
   measurements taken off your actual hardware — never from generic
   knowledge about what "usually" causes a symptom.
2. **Evidence over speculation.** The reasoning model never touches your
   machine. It only ever reasons over evidence that was already collected
   deterministically, and it's not allowed to invent a fact that wasn't
   handed to it.
3. **Safe autonomy.** The model can suggest and even draft an action, but
   there is exactly one path from a suggestion to an actual change on your
   machine, and it runs through your explicit approval every time. The
   riskiest thing this system will ever do to a process is pause it —
   suspending is reversible, so it's the ceiling. Terminating is not on the
   table.

## Try it

```powershell
# build the core agent (needs Visual Studio Build Tools + CMake)
cmake -S . -B build -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build

# run the Living Halo
powershell -ExecutionPolicy Bypass -File .\ui\PixelMini\build.ps1
.\ui\PixelMini\bin\PixelMini.exe
```

Move your mouse to the top-center edge of the screen to summon it. Left-click
for a quick health readout; right-click to chat, enable voice, or preview
how it looks under different conditions.

The chat and diagnosis features need a free [Groq](https://console.groq.com)
API key — see [Building and running it](SYSTEM_DOCS.md#building-and-running-it)
for the one-time setup. Without a key, everything still works, just with a
plainer, rule-based explanation instead of a conversational one.

## Learn more

- **[System documentation](SYSTEM_DOCS.md)** — architecture, every
  component, the full CLI reference, the safety model, and how to build and
  run each piece.
- **[UI_HANDOFF.md](UI_HANDOFF.md)** — the Living Halo's current
  implementation state and what's next for it.
- **[PRD](PRD%20—%20System%20Intelligence_%20OS-Native%20AI%20Diagnostic%20Agent.md)**
  — the product vision and principles this was built against.
- **[Technical Design](Technical%20Design%20—%20System%20Intelligence%20v0.1.md)**
  — the original architecture proposal.

System Intelligence runs entirely on Windows 11, entirely on your own
machine. The only thing that ever leaves it is the specific evidence sent to
Groq for a diagnosis or a chat answer — never raw telemetry, never your
voice, never anything the model wasn't explicitly handed.
