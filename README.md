# mds-harness

A standalone development harness for Elektron Machinedrum MDS 1.1 custom
"User Machines" — load a `.mds` package into the real `dsp56300` emulator
core (the same interpreter that backs `mds-tools`' certifier/disassembler)
and render or play it, without needing real hardware or a full
build→certify→flash→listen round trip.

## TL;DR

One-time build, then `python tools/serve.py` from wherever your
machines live. Everything below is a link into "What you can do with
it" further down, simple to advanced:

| I want to... | Do this |
|---|---|
| [Just listen to a machine](#1-listen-to-a-machine-thats-already-built) | Pick it from the dropdown, click RENDER & PLAY. |
| [Explore its sound](#2-explore-a-machines-sound-by-turning-its-knobs) | Turn knobs, click RENDER & PLAY again. |
| [Program a beat](#3-program-a-trig-pattern-the-sequencer) | Click step pads in the 16-step row to toggle them. |
| [Lock a parameter to one step](#4-lock-individual-parameters-to-a-specific-step-plocks) | Dial the knobs you want, press that step's key (`1`-`8`/`q`-`i`). |
| [Feed it a simulated neighbour track](#5-simulate-the-neighbour-track-nfx--neighbour-machines-only) | Use the "Neighbour input" bay (NFX/neighbour machines only). |
| [Keep a render for later / for an agent to read](#6-save-a-render-for-later-inspection-eg-by-an-agent) | Check SAVE RENDER before clicking RENDER & PLAY. |
| [Edit a machine's source and hear it live](#7-edit-a-machines-own-source-and-hear-changes-immediately) | Just edit its `.asm56k`/`.json`, click RENDER again. |
| [Get a real, installable `.syx`](#8-export-a-real-installable-syx) | Click EXPORT .SYX — slow (real certifier), be patient. |
| [Script it, no browser](#9-script-against-the-cli-directly-bypassing-the-web-ui) | `./build/mds-harness render machine.mds --trig -o out.wav` |

## System requirements and assumptions

- **Windows with MinGW-w64**, currently. This has only been built and
  run on Windows; the CMake setup is MinGW-specific (forces the plain
  DSP56300 interpreter rather than the JIT, which crashes under this
  toolchain — see "Building" below). Running this anywhere else
  (Linux/macOS, or Windows with MSVC/clang instead of MinGW) isn't
  tested and would need real verification, not just a config tweak.
- **MinGW is auto-detected, not hardcoded.** The first time
  `tools/serve.py` runs, it checks (in order): a path saved from an
  earlier run, `gcc`/`g++` already on `PATH` (the common case — nothing
  to do), a handful of standard install locations, and only if none of
  those hit, asks once in the terminal and remembers the answer. No
  source edit needed to point it at your own install.
- **Python 3**, stdlib only — no `pip install` needed for anything,
  including the vendored build-pipeline scripts (below).
- **CMake + Ninja**, and a C++17 compiler (MinGW g++, per the point above).
- **Nothing else external.** This repo is fully self-contained: the
  DSP56300 interpreter (`dsp56300`, upstream GPLv3), the assembler
  (`asm56.exe` + its Python front end), the packager (`mds_pack.py`),
  and the certifier (`mds-certifier.exe`) are all vendored into this
  repo's own `vendor/` directory — no sibling checkouts of anything are
  required to clone, build, or use this tool end to end (building a
  machine from source and exporting a real `.syx` included). See
  `CLAUDE.md`'s "Running this on a different computer" section for
  exactly what's vendored and why, if you ever need to re-vendor from a
  fresh upstream checkout.
- **A modern browser** for the web UI (uses `fetch`, `Blob`/`URL.createObjectURL`,
  `<input type="file">`, CSS grid — nothing exotic, but no IE11-era
  fallback either).
- No GPU, no audio driver dependency, no admin/elevated privileges
  needed — rendering is pure offline WAV synthesis; "playback" is just
  the browser's own `<audio>` element.

## Building (one-time setup)

Fully self-contained — no sibling checkouts needed:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target mds-harness
```

`DSP56300_DIR` defaults to this repo's own `vendor/dsp56300/`; pass
`-DDSP56300_DIR=/path/to/dsp56300` to build against a different (e.g.
newer upstream) checkout instead. On Windows, put MinGW's `bin` on
`PATH` first if this exact CMake invocation is run outside
`tools/serve.py` (which auto-detects it — see "System requirements"
above). The build forces the plain DSP56300 interpreter rather than the
JIT (asmjit's x64 backend crashes under this toolchain — a call through
the engine's own tiny call-stub ends up jumping to the stub's address as
if it were native code, confirmed via a `gdb` backtrace). Not a problem
for this tool (it's offline rendering, not live audio yet) — revisit
only if a future milestone genuinely needs JIT speed.

Once built, start the web UI (the normal way to use this day-to-day)
from wherever your machines live:

```sh
cd /path/to/your/machines/project
python /path/to/mds-harness/tools/serve.py
```

That opens a browser tab at `127.0.0.1:8765` (pass `--no-open` to skip
the auto-open, or `--machines-root <dir>` to point it somewhere other
than the current directory). Everything below assumes the server is
already running.

## What you can do with it, simple to advanced

### 1. Listen to a machine that's already built

The simplest case: pick a machine from the **MACHINE** dropdown (every
`.mds` file found recursively under wherever you launched the server
from gets listed), click **RENDER & PLAY**. The real `dsp56kEmu`
interpreter runs the machine's actual INIT/TRIG/RENDER code and the
result plays straight back in the browser's `<audio>` element.

### 2. Explore a machine's sound by turning its knobs

Every parameter the machine declares gets a real rotary knob — drag
vertically, use arrow keys once focused, or type a value directly.
Tick marks mark MIN/MID/MAX (or `−`/`+` for a bipolar parameter, where
the middle position is zero rather than a minimum). **BARS** and
**BPM** set the render length in musical terms rather than a raw block
count. Click RENDER & PLAY again after any change — each render
reflects the knobs exactly as they currently sit.

### 3. Program a trig pattern (the sequencer)

Below the knobs, a row of 16 step pads — the Machinedrum's real
hardware layout (one row of 16, not a 4×4 grid). Click a pad to toggle
whether that step fires a real `TRIG`, repeated across every bar in
BARS. Every 4th pad's resting border is brighter, marking beat
boundaries so the row reads as 4 groups of 4.

### 4. Lock individual parameters to a specific step ("plocks")

Each pad also shows a key label (`1`-`8`, then `q`-`i` for steps
9-16). Dial up whatever knobs you want different for one hit, then
press that step's key: it **plocks** — Elektron's own term for a
per-step parameter lock — snapshotting every knob currently off its
own default value into that step only. The *next* trig point (locked
or not) decides each parameter fresh again, so a plock's effect never
leaks past its own step. A knob off its default glows with a ring (it's
exactly what the next plock keypress would capture); a step carrying at
least one plock gets a small dot on its pad. **Double-tap** a step's
key within ~400ms to clear that step's plocks without touching its
on/off state.

### 5. Simulate the neighbour track (NFX / neighbour machines only)

A machine that reads "tracks back" (the Machinedrum's neighbour-input
mechanism) gets an extra "Neighbour input" section: FAR (two tracks
back) and NEAR (one track back) slots, each defaulting to a bundled WAV
(`tools/assets/neighbour_{far,near}.wav` — three stacked tones,
low→mid→high for far, reversed for near) with a file picker to
override and a "use default" reset. Three-tracks-back is wired into
the descriptor table so a machine reading it gets real silence rather
than garbage, but has no WAV input of its own yet.

### 6. Save a render for later inspection (e.g. by an agent)

Renders are normally a tempfile — streamed to the browser, deleted
right after. Check **SAVE RENDER** before clicking RENDER & PLAY to
instead write it to a stable path, `renders/last_render.wav`
(overwritten each time), so a dev or an agent reading the filesystem
later always knows exactly where to look. Every render — saved or
not — also embeds the exact parameters, trig pattern (with any
plocks), BPM, and neighbour WAVs that produced it as a standard RIFF
`LIST/INFO/ICMT` comment chunk, so the file alone answers "what made
this sound the way it does?" without a separate sidecar. See
`CLAUDE.md` for a short script that reads it back out.

### 7. Edit a machine's own source and hear changes immediately

This is where the dev loop actually speeds up. Any machine folder
under the machines root with a matching `<PREFIX>.asm56k` +
`<PREFIX>.json` pair (same naming convention as `Machines/build.py`,
searched recursively — e.g. `NFX-BO/NFXBO.asm56k`, or
`Shelved/SYN-PX/SYNPX.asm56k`) gets listed by its folder name and
rebuilt fresh — assemble, then pack (bundling `<PREFIX>_icon.pbm` if
one exists) — on *every* params/render request, using the same first
two steps as `Machines/build.py` (skipping `--certify` and the final
`.syx` sysex-encode for speed; this is a dev-iteration build, not the
official release pipeline). Edit the `.asm56k`/`.json`/icon directly in
your own editor, click RENDER in the browser, hear the result — no
manual rebuild step, no stale cache, and a build/assemble error
surfaces as a normal error message in the UI instead of a silent
failure. Folders with no source pair of their own (e.g. `refs/`
machines extracted from a real firmware dump) still work exactly as
before, used directly from their existing `.mds`.

### 8. Export a real, installable `.syx`

The most advanced, slowest action: **EXPORT .SYX** runs the *full*
toolchain — assemble, pack **with `--certify`**, sysex-encode — not
the fast draft build used for render previews, because a package needs
a real certified cycle budget before it can be installed on hardware.
**This is slow** (the real certifier's worst-case cycle search took
13+ minutes on one small machine under concurrent load) — expect it to
take a while, especially if other heavy work is running at the same
time. The knob values sitting in the UI right now become the exported
machine's own defaults (a throwaway copy of its `.json` manifest; the
real one on disk is never touched). Installs to slot 1 by default —
change the destination slot later at install time, it's not worth a
dedicated control here.

### 9. Script against the CLI directly, bypassing the web UI

For automation or batch rendering without a browser:

```sh
./build/mds-harness render machine.mds --list-params
./build/mds-harness render machine.mds --list-params-json   # for scripting -- see tools/serve.py
./build/mds-harness render machine.mds --trig --blocks 200 --param BASE=100 -o out.wav
./build/mds-harness render machine.mds --trig-at "0:NBAL=0" --trig-at "500:NBAL=127" --blocks 1000 -o out.wav
./build/mds-harness render machine.mds --trig --blocks 200 \
    --neighbour-far tools/assets/neighbour_far.wav \
    --neighbour-near tools/assets/neighbour_near.wav \
    -o out.wav
```

`--trig-at BLOCK[:NAME=VAL,...]` is the same sequencer/plock mechanism
the web UI's step grid drives — repeatable, one per trig point.
`--neighbour-far`/`--neighbour-near` feed a neighbour/NFX machine's
"tracks back" input from a 16-bit PCM WAV (looping if shorter than the
render); omit either and that signal is silence. The server
(`tools/serve.py`) itself is stdlib-only Python — no Flask, no build
step for the front end (`tools/web/`) — so reading it is a reasonable
way to see exactly how the UI maps onto these same CLI flags.

## Background

### Why

Building a custom USR machine today means editing assembly, running the
full certification pipeline, flashing a `.syx` to real hardware (or an
emulator), and listening — a slow loop for iterating on anything
synthesis-related. This project exists to shortcut that loop for
development: load a machine, poke its parameters, hear the result,
immediately.

### Plan (thin slices, in order)

1. **WAV-dump CLI** — load a `.mds`, set fixed parameter values, trigger
   once, render N blocks, write a `.wav` file. No live audio yet. ✅ done.
2. **Hot-reload parameters** — originally planned as "watch a JSON/text
   file for parameter changes, re-render." Superseded by something more
   useful: a local web UI that renders on demand with real knob
   widgets and plays the result straight back in the browser — no file
   watching, no manual WAV-dragging. ✅ done, exceeds the original scope.
3. **Live audio** — drive the engine via [PortAudio](https://www.portaudio.com/)
   (MIT-licensed) for real-time playback and live parameter tweaking.
   Not started — the web UI's "render on click" loop has covered
   development needs so far.
4. **JUCE wrap** — later, on a separate JUCE/C++ project — drop the core
   engine into an `AudioProcessor`, swapping the audio-driver layer only.
   Not started.

### Architecture

A `MachineEngine` (loads program + Y-memory workspace into `dsp56kEmu`,
exposes `trigger()` / `setParam(slot, value)` / `renderBlock(float* out)`)
is kept fully decoupled from whatever drives it — a WAV writer, a
PortAudio callback, or eventually a JUCE `AudioProcessor`. The engine
should never know or care who's calling it.

## License / dependencies

Vendored, not linked-against-a-sibling: [`dsp56300`](https://github.com/dsp56300/dsp56300)
(GPLv3) — the actual DSP56300 interpreter — lives at `vendor/dsp56300/`
in this repo, trimmed to the subset actually needed to build (not the
full upstream checkout's GUI debugger/plugin-wrapper/profiler-SDK
tooling this project never touches). Vendoring GPLv3 source directly
into a GPLv3 project (see `LICENSE`) is a normal way to satisfy that
license, not a new licensing question.

`vendor/assembler/` (the DSP56300 instruction encoder) and
`vendor/mds-tools/` (the `.mds` packager and certifier) are vendored
copies of this same author's own tooling from elsewhere in the broader
Machinedrum project — same copyright holder throughout, which is why
vendoring them here doesn't raise a separate licensing question either.
Parameter-transform formulas (`bipolar_q23`, `unipolar_q23`, `pitch`,
etc.) in this project's own C++ engine are still reimplemented directly
from the public MDS spec rather than copied from the vendored packager's
own internals, for clarity of what this engine independently verifies
versus what it just reuses.

Source of truth for the MDS/MDX format itself:
[github.com/jmamma/MDX](https://github.com/jmamma/MDX) and
[github.com/jmamma/MDS](https://github.com/jmamma/MDS), by Justin
Mammarella.

## Status

**Fully self-contained, no sibling checkouts** (2026-11-13) — `dsp56300`,
the assembler, the packager, and the certifier are all vendored into
this repo's own `vendor/` directory; `MINGW_BIN` is auto-detected
instead of hardcoded. Verified with a from-scratch build pointed only
at `vendor/dsp56300` (no sibling `dsp56300` checkout present) and real
renders of `SYN-RB` and `NFX-BO` producing correct, non-silent output
afterward.

**Builds from source on every render** (2026-11-13) — the web UI no
longer requires a pre-built `.mds`; it assembles+packs a machine's own
`.asm56k`/`.json`/icon fresh every time, so source edits are reflected
immediately with no manual build step. Verified against `NFX-BO`: a
fresh on-the-fly build produced byte-for-byte the same render as the
already-existing pre-built `.mds`.

**Local web UI working end-to-end** (2026-11-13) — loads a real `.mds`,
applies parameters through their real transforms, calls INIT/TRIG/RENDER
through the actual `dsp56kEmu` interpreter, and plays real audio back in
the browser on click. Verified against `SYN-RB`, `SYN-PX`, and `NFX-BO`
(including its neighbour-input blend, `NBAL`): correct parameter
parsing, genuine non-silent output, and WAV-fed neighbour simulation
that actually reaches the DSP (confirmed by swapping in different WAVs
and by comparing against a zero-input/silent baseline).

Known limitations:
- Sample services (`SAMPLE_INFO`/`SAMPLE_READ`) and RAM services
  (`RAM_WORKSPACE`/`RAM_RECORD`) resolve to a harmless no-op stub rather
  than real behavior.
- The built-in sine table import is populated with real (synthetically
  generated, not byte-exact to the firmware's own table) data.
  `TRACK_BUFFERS` (symbol 6) is resolved to a real, isolated address
  (`0x001000`, 4096 words) rather than left at 0 — it used to collide
  with this engine's own parameter workset, silently corrupting a
  machine's knob values mid-render the moment it captured anything (see
  `NFX-BO`'s `NBAL` bug, found and fixed 2026-11-13). Any other
  not-yet-handled import symbol still resolves its address but isn't
  backed by real data (zeroed).
- Reference machines extracted from a real firmware `.syx` dump (under
  `USR-Machines/_authoring/Machines/refs/`, e.g. `SYN-LA`) render
  silent — not a bug in this engine. A `.syx` dump captures a machine
  already loaded on real hardware, with shared resources (like the
  firmware's own sine table) already resolved to the *real firmware's*
  fixed address and baked into the program directly, rather than left
  as a relocatable import this engine can intercept. Machines authored
  through this project's own `.json`→`.mds` compiler (`SYN-RB`,
  `SYN-PX`, `NFX-BO`, ...) aren't affected — they emit real relocatable
  imports this engine resolves correctly. Deprioritized; not planned
  work right now.
- The sequencer currently authors one flat 16-step pattern repeated
  identically across every bar in BARS — distinct per-bar patterns (up
  to 4) have been requested but aren't built yet.

Next: live audio (PortAudio), if/when development needs real-time
parameter tweaking rather than render-on-click.

## Shout outs

gearmulator for the 56300 emulator, kitbasher for toolchain stuff,
waftlord and murdenihcam for input and inspiration, MDX20 developers
for this entire experience, not to mention Elektron and Commodore and
the giants on whose shoulders we aspire to stand.
