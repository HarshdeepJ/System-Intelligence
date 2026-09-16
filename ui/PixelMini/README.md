# Living Halo — System Intelligence UI

A small, formless WPF presence for System Intelligence. The computer is
represented by an opalescent glow attached to the top-center edge of the
screen rather than by a character, window, or widget.

## Run

This machine has the Windows Desktop runtime and Visual Studio compiler but no
`dotnet` SDK, so the included build script targets the installed Windows WPF
framework directly:

```powershell
powershell -ExecutionPolicy Bypass -File .\ui\PixelMini\build.ps1
.\ui\PixelMini\bin\PixelMini.exe
```

With a .NET 8 SDK installed, the normal project also works:

```powershell
dotnet run --project .\ui\PixelMini\PixelMini.csproj
```

## Presence and interaction

- At rest, only a faint two-pixel glint remains at the top center.
- Hold the pointer in the top-center activation strip for 180 ms to reveal the
  halo. The strip is 84 logical pixels wide and six pixels tall.
- The visible halo breathes continuously, with a soft bloom, bright core, and
  a slowly drifting wisp. It never resolves into a face or physical object.
- Calm uses opalescent white, cyan, periwinkle, lilac, and a trace of blush.
  Listening and working cool toward cyan/periwinkle; concern warms toward
  honey/coral; recovery and charging use mint/sky.
- Left-click the halo to open or close a narrow horizontal health strip. Its
  status, CPU, memory, temperature, battery, and disk readings stay on one row.
- Right-click it to open a translucent floating command bar near the top of the
  screen, control voice listening, preview system states, or quit. Typed input
  submits with Enter or the arrow button. Voice recognition streams its current
  transcription into the same bar. On submit, the prompt lifts away and the
  finished response appears alone on a compact glass surface.
- The halo retreats after ten seconds away, unless a card, chat, or menu is
  open.

Telemetry, chat, voice, and explicitly approved actions remain connected. The
old robot artwork in `Assets/` is retained only as historical source material;
the production halo is rendered entirely in WPF.

## Verification

```powershell
.\ui\PixelMini\bin\PixelMini.exe --smoke-layout
.\ui\PixelMini\bin\PixelMini.exe --visual-check
```

The layout smoke test checks top-center activation, reveal, expansion, and
horizontal centering. The visual check writes a 3× transparent render to
`ui/PixelMini/bin/visual-check.png`.
