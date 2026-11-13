# mds-harness

A standalone development harness for Elektron Machinedrum MDS 1.1 custom
"User Machines" — load a `.mds` package into the real `dsp56300` emulator
core (the same interpreter that backs `mds-tools`' certifier/disassembler)
and render or play it, without needing real hardware or a full
build→certify→flash→listen round trip.

## System requirements and assumptions

- **Windows with MinGW-w64**, currently. This has only been built and
  run on Windows; the CMake setup is MinGW-specific (forces the plain
  DSP56300 interpreter rather than the JIT, which crashes under this
  toolchain — see "Building" below), and `tools/serve.py` hardcodes
  MinGW's `bin` directory onto `PATH` for every subprocess it spawns
  (`MINGW_BIN` in that file) rather than detecting it. Running this
  anywhere else (Linux/macOS, or Windows with MSVC/clang instead of
  MinGW) means updating that constant and re-verifying the CMake build
  at minimum — not tested, may need more than that.
- **Python 3**, stdlib only — no pip install needed for `tools/serve.py`
  or any of the build-pipeline scripts it shells out to
  (`mds_assemble.py`, `mds_pack.py`, `mds_to_syx.py`).
- **CMake + Ninja**, and a C++17 compiler (MinGW g++, per the point above).
- **A sibling checkout of [`dsp56300`](https://github.com/dsp56300/dsp56300)**
  (GPLv3) — the real DSP56300 interpreter this tool drives. Expected at
  `../dsp56300` relative to this repo by default; override with
  `-DDSP56300_DIR=...` if it lives elsewhere.
- **A specific sibling-directory layout for the source-build and `.syx`-export
  features** (building a machine from its own `.asm56k`/`.json`, and the
  EXPORT .SYX button): `mds-harness` must sit as a direct sibling of
  `tools/` (for `mds_assemble.py`) and `mds-tools/` (for `mds_pack.py`,
  `mds_to_syx.py`) under one common root. Rendering an already-built
  `.mds` directly does **not** need this layout — only the build/export
  pipeline does.
- **A modern browser** for the web UI (uses `fetch`, `Blob`/`URL.createObjectURL`,
  `<input type="file">`, CSS grid — nothing exotic, but no IE11-era
  fallback either).
- No GPU, no audio driver dependency, no admin/elevated privileges
  needed — rendering is pure offline WAV synthesis; "playback" is just
  the browser's own `<audio>` element.

## Quick start

```sh
# One-time build (needs a sibling dsp56300 checkout -- see "Building" below)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target mds-harness

# Every time you want to use it:
cd /path/to/your/machines/project   # wherever your own .asm56k/.json files live
python /path/to/mds-harness/tools/serve.py
```

That opens a browser tab with every machine found under your current
directory (recursively) listed in a dropdown, real knobs for its
parameters, a 16-step trig pattern, and a RENDER & PLAY button. Edit a
`.asm56k`/`.json` source file, click RENDER again — no separate rebuild
step, the server assembles+packs fresh on every click. See "Web UI"
below for the full feature rundown (parameter locks, neighbour-input
simulation, `.syx` export, saved renders with embedded metadata).

## Why

Building a custom USR machine today means editing assembly, running the
full certification pipeline, flashing a `.syx` to real hardware (or an
emulator), and listening — a slow loop for iterating on anything
synthesis-related. This project exists to shortcut that loop for
development: load a machine, poke its parameters, hear the result,
immediately.

## Plan (thin slices, in order)

1. **WAV-dump CLI** — load a `.mds`, set fixed parameter values, trigger
   once, render N blocks, write a `.wav` file. No live audio yet. ✅ done.
2. **Hot-reload parameters** — originally planned as "watch a JSON/text
   file for parameter changes, re-render." Superseded by something more
   useful: a local web UI (below) that renders on demand with real knob
   widgets and plays the result straight back in the browser — no file
   watching, no manual WAV-dragging. ✅ done, exceeds the original scope.
3. **Live audio** — drive the engine via [PortAudio](https://www.portaudio.com/)
   (MIT-licensed) for real-time playback and live parameter tweaking.
   Not started — the web UI's "render on click" loop has covered
   development needs so far.
4. **JUCE wrap** — later, on a separate JUCE/C++ project — drop the core
   engine into an `AudioProcessor`, swapping the audio-driver layer only.
   Not started.

## Architecture

A `MachineEngine` (loads program + Y-memory workspace into `dsp56kEmu`,
exposes `trigger()` / `setParam(slot, value)` / `renderBlock(float* out)`)
is kept fully decoupled from whatever drives it — a WAV writer, a
PortAudio callback, or eventually a JUCE `AudioProcessor`. The engine
should never know or care who's calling it.

## License / dependencies

Depends on [`dsp56300`](https://github.com/dsp56300/dsp56300) (GPLv3) for
the actual DSP56300 interpreter. Deliberately does **not** depend on
`mds-tools` (no explicit upstream license) — parameter-transform formulas
(`bipolar_q23`, `unipolar_q23`, `pitch`, etc.) are reimplemented directly
from the public MDS spec (jmamma/MDS,
`docs/MDX_MachineDumpStandard.txt`) rather than linked from that repo's
own code, to keep this project's own licensing unambiguous if it's ever
shared.

Source of truth for the MDS/MDX format itself:
[github.com/jmamma/MDX](https://github.com/jmamma/MDX) and
[github.com/jmamma/MDS](https://github.com/jmamma/MDS), by Justin
Mammarella.

## Building

Needs a sibling checkout of [`dsp56300`](https://github.com/dsp56300/dsp56300)
(see `../dsp56300` relative to this repo, or pass `-DDSP56300_DIR=...`).

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target mds-harness
```

On Windows with MinGW, put MinGW's `bin` on `PATH` first (same requirement
as this project's sibling tools). The build forces the plain DSP56300
interpreter rather than the JIT (asmjit's x64 backend crashes under this
toolchain — a call through the engine's own tiny call-stub ends up jumping
to the stub's address as if it were native code, confirmed via a `gdb`
backtrace). Not a problem for this tool (it's offline rendering, not live
audio yet) — revisit only if a future milestone genuinely needs JIT speed.

## Usage

### CLI

```sh
./build/mds-harness render machine.mds --list-params
./build/mds-harness render machine.mds --list-params-json   # for scripting -- see tools/serve.py
./build/mds-harness render machine.mds --trig --blocks 200 --param BASE=100 -o out.wav
./build/mds-harness render machine.mds --trig --blocks 200 \
    --neighbour-far tools/assets/neighbour_far.wav \
    --neighbour-near tools/assets/neighbour_near.wav \
    -o out.wav
```

`--neighbour-far`/`--neighbour-near` feed a neighbour/NFX machine's
"tracks back" input from a 16-bit PCM WAV (looping if shorter than the
render); omit either and that signal is silence, same as before this
feature existed. Only one-track-back ("near") and two-tracks-back
("far") are simulated — three-tracks-back is wired into the descriptor
table so a machine reading it gets real silence rather than garbage, but
has no WAV input of its own yet.

### Web UI (the normal way to use this day-to-day)

```sh
cd /path/to/your/machines/project   # wherever your own .asm56k/.json files live
python /path/to/mds-harness/tools/serve.py
```

Starts a local dev server (`127.0.0.1:8765` by default) and opens a
browser tab automatically (pass `--no-open` to skip that). **It scans
wherever you ran it FROM** (the shell's current directory, recursively)
for machines — not a path baked into this repo — so this works from any
project on disk; it doesn't need to sit next to `USR-Machines/`. Pass
`--machines-root <dir>` to point it somewhere else without `cd`-ing
there first. The page: lists every machine found that way in a
dropdown; renders real rotary knobs for each of a machine's declared
parameters (tick marks at min/mid/max, `−`/`+` labels for bipolar
params, `MIN`/`MAX` for everything else); a BARS/BPM transport instead
of a raw block count; and, for neighbour/NFX machines only, a "Neighbour
input" bay with FAR (two tracks back) and NEAR (one track back) slots,
each defaulting to a bundled WAV (`tools/assets/neighbour_{far,near}.wav`
— three stacked tones, low→mid→high for far, reversed for near) with a
file picker to override and a "use default" reset. Clicking RENDER &
PLAY shells out to the real `mds-harness.exe` and plays the resulting
WAV straight back in an `<audio>` element.

### Sequencer and parameter locks ("plocks")

A row of 16 step pads below the knobs — the Machinedrum's real hardware
layout (one row of 16, not a 4×4 grid). Click a pad to toggle whether
that step fires a real TRIG, repeated across every bar in BARS.

Each pad also shows a small key label (`1`-`8`, then `q`-`i` for steps
9-16). Pressing that key **plocks** the step — Elektron's term for a
per-step parameter lock — snapshotting every knob that's currently off
its own default value into that step only; the next trig point (locked
or not) decides each parameter fresh again, so a plock's effect never
leaks past its own step. A knob currently off its default gets a glowing
ring (it's exactly what the next plock keypress would capture); a step
carrying at least one plock gets a small dot on its pad. **Double-tap**
a step's key within the same ~400ms to clear that step's plocks without
touching its on/off state.

### Saving a render + inspecting its metadata

Renders are normally a tempfile — streamed to the browser, deleted
right after. Checking "SAVE RENDER" before clicking RENDER & PLAY
instead writes it to a stable path, `renders/last_render.wav`
(overwritten each time), so a dev or an agent reading the filesystem
later always knows exactly where to look. Every render — saved or not —
also embeds the exact parameters, trig pattern (with any plocks), BPM,
and neighbour WAVs that produced it as a standard RIFF `LIST/INFO/ICMT`
comment chunk, so the file alone answers "what made this sound the way
it does?" without a separate sidecar. See `CLAUDE.md` for a short script
that reads it back out.

### Exporting a real `.syx`

The EXPORT .SYX button runs the *full* toolchain — assemble, pack
**with `--certify`**, sysex-encode — not the fast draft build used for
render previews, because a package needs a real certified cycle budget
before it can be installed on hardware. **This is slow** (the real
certifier's worst-case cycle search took 13+ minutes on one small
machine under concurrent load) — expect it to take a while, especially
if other heavy work is running at the same time. The current knob
values become the exported machine's own defaults (a throwaway copy of
its `.json` manifest; the real one on disk is never touched). Pick the
destination SLOT (1-16) next to the button first.

**Builds from source, not just from a pre-built `.mds`.** Any machine
folder under the machines root that has a matching
`<PREFIX>.asm56k` + `<PREFIX>.json` pair (same naming convention as
`Machines/build.py`, searched recursively — e.g. `NFX-BO/NFXBO.asm56k`,
or `Shelved/SYN-PX/SYNPX.asm56k`) gets listed by its folder name and
rebuilt fresh — assemble, then pack (bundling `<PREFIX>_icon.pbm` if one
exists) — on *every* params/render request, using the same first two
steps as `Machines/build.py` (skipping `--certify` and the final `.syx`
sysex-encode for speed; this is a dev-iteration build, not the official
release pipeline). So a dev (or an agent) can edit a `.asm56k`/`.json`/
icon directly, hit RENDER, and hear the result immediately — no manual
rebuild step, no stale cache, and a build/assemble error surfaces as a
normal error message in the UI instead of a silent failure. Folders
with no source pair of their own (e.g. `refs/` machines extracted from
a real firmware dump) still work exactly as before, used directly from
their existing `.mds`.

The server (`tools/serve.py`) is stdlib-only Python — no Flask, no
build step for the front end (`tools/web/`).

## Status

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

Next: live audio (PortAudio), if/when development needs real-time
parameter tweaking rather than render-on-click.
