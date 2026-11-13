# mds-harness -- agent onboarding

This file exists so an agent dropped into this repo cold (new session, no
memory of how this got built) can start being useful immediately,
working *with the person at the keyboard* rather than re-deriving
everything from source. Keep this file current as the project changes --
when you add or change a capability, update the matching section here in
the same turn, not as a follow-up.

**Read `README.md` too** -- it's the user-facing doc (what this is, why
it exists, build instructions, full usage). This file is the
agent-facing supplement: how the pieces fit together, what's fragile,
what's already been tried and discarded, and what to check before
touching things.

## What this is, in one paragraph

A local dev harness for Elektron Machinedrum MDS 1.1 "User Machines":
load a `.mds` (or build one fresh from its own `.asm56k`/`.json`/icon
source) into the real `dsp56300` DSP emulator, render audio through the
actual INIT/TRIG/RENDER machine code, and hear the result -- without
flashing real hardware. The normal way to use it is the local web UI
(`python tools/serve.py`), not the bare CLI.

## Starting it up

```sh
cd mds-harness
cmake --build build --target mds-harness   # if src/ changed; see README's Building section for first-time setup
cd /path/to/your/machines/project          # the server scans THIS directory, see below
python /path/to/mds-harness/tools/serve.py # opens a browser tab at 127.0.0.1:8765
```

**The machines root is wherever `serve.py` was launched FROM** (`os.getcwd()`
at startup, `DEFAULT_MACHINES_ROOT` in `serve.py`), scanned recursively --
not a path hardcoded to this repo's own layout. This was a deliberate
fix (2026-11-13): it used to always default to a specific sibling path
(`../USR-Machines/_authoring/Machines`), which only worked for this one
project's own directory layout. Don't reintroduce a hardcoded default --
if you need a fallback for when cwd isn't right, that's what
`--machines-root <dir>` is for, not a baked-in path.

If a C++ source file changed, rebuild the exe (`cmake --build`) -- the
running server shells out to the exe fresh on every request, so **no
server restart is needed** after a C++-only change. If `tools/serve.py`
itself changed, the server process DOES need restarting (its own Python
code only loads once, at startup) -- kill the old process and relaunch.
Static web files (`tools/web/*`) are read fresh from disk on every
request; a *server* restart is never needed for those, but the
*browser* may still be holding an old copy in memory from before the
edit -- hard-refresh (Ctrl+Shift+R) the tab if a UI change doesn't
seem to have landed. This exact confusion already happened once this
session; don't assume a bug before ruling out a stale tab.

MinGW's `bin` needs to be on `PATH` for anything that touches the real
DSP56300 interpreter, the assembler, the certifier, or the
disassembler -- `serve.py` already sets this itself (`MINGW_BIN`
constant) for every subprocess it spawns. If running a tool by hand
outside the server, set it first:
```sh
export PATH="/c/Users/thisi/AI/Workspace/Tools/mingw64/bin:$PATH"
```
Forgetting this doesn't error loudly -- it fails silently
(`STATUS_DLL_NOT_FOUND`, exit `-1073741511`) in a way that looks like
the tool itself is broken.

## Running this on a different computer / for a different person

This is now fully self-contained -- a fresh clone of just `mds-harness`
(no sibling checkouts of anything except `vendor/dsp56300` -- wait, that's
vendored too, see below) builds and runs the whole pipeline, source-build
and `.syx`-export included. Two things resolved this session that used to
block portability, kept here so neither gets silently reintroduced:

- **`MINGW_BIN`** used to be a literal hardcoded path to this one user's
  MinGW install. Now `find_mingw_bin()` in `serve.py` auto-detects it:
  checks a config file saved from an earlier run
  (`.mds-harness-config.json`, gitignored), then `gcc`/`g++` already on
  `PATH` (nothing to configure), then a short list of common Windows
  install locations, and only if none of those hit, prompts once in the
  terminal and remembers the answer. Never hardcode a path here again --
  if detection needs improving, improve `find_mingw_bin()` itself.
- **The build/export pipeline used to assume `mds-harness` sits as a
  sibling of `tools/` and `mds-tools/`** (a `MACHINEDRUM_ROOT =
  os.path.dirname(REPO_ROOT)` guess). Fixed by vendoring real,
  self-contained copies into `mds-harness/vendor/`:
  - `vendor/mds_assemble.py` + `vendor/assembler/` (assembly.py +
    `asm56.exe`, the real native DSP56300 instruction encoder) --
    originally `tools/mds_assemble.py` + `kitbasher/packs/assembly.py` +
    `kitbasher/build/assembly/asm56.exe`.
  - `vendor/mds-tools/` (`mds_pack.py`, `mds_to_syx.py`, `lib/authoring.py`,
    `lib/mds_format.py`, `mds-certifier.exe`) -- originally
    `mds-tools/tools/*.py` + `mds-tools/build/mds-certifier.exe`.
  - `vendor/dsp56300/` -- not the full ~280MB upstream checkout (that
    includes a wxWidgets GUI debugger, a HxD plugin, VTune/Superluminal
    profiler SDKs as options, none of which this project touches), just
    the ~11MB subset `dsp56kEmu`/`dsp56kBase`/`asmjit` (what
    `dsp56kEmu` actually links against) plus the handful of other
    subdirectories (`vtuneSdk`, `HxDplugin`, `dsp56kTestRunner`,
    `disassemble`) that `source/CMakeLists.txt` unconditionally
    `add_subdirectory()`s regardless -- CMake errors if those
    directories don't exist, even though their *targets* are never
    linked against. `CMakeLists.txt`'s `DSP56300_DIR` now defaults here
    instead of `../dsp56300`; `asmjit` was a git submodule in the
    original checkout and its stray `.git` gitlink file was deliberately
    stripped when vendoring (a nested gitlink inside this repo's own
    tracked tree would make `git add` silently skip its contents) --
    its `LICENSE.md` (zlib-style, MIT-compatible) was kept for
    attribution.

  `mds_pack.py`'s own certifier auto-discovery (`../build/mds-certifier`
  relative to itself) assumed the *original* `mds-tools/tools/` +
  `mds-tools/build/` depth, which the vendored layout doesn't match --
  `build_from_source` in `serve.py` passes `--certifier` explicitly now
  rather than relying on that coincidence. If you ever reorganize
  `vendor/`, re-check that this explicit path still matches.

  All of this is the **same author's own code** throughout (kitbasher,
  mds-tools, and this project) -- that's why vendoring it was a
  reasonable thing to do without a separate licensing review. `dsp56300`
  itself is third-party GPLv3, already why this whole project is GPLv3
  (see `LICENSE`); vendoring its source directly is a normal, accepted
  way to satisfy that license, not a new licensing question.

  **Trimmed when vendoring** (don't re-add these if re-vendoring from a
  fresh upstream checkout): `HxDplugin/HxDapi/Examples/` originally held
  C, C++, *and* Delphi example projects for an unrelated plugin-authoring
  demo; `HxDplugin/CMakeLists.txt` actually only needs one single file
  out of that whole tree (`HxDapi/Examples/C++/DataInspectorPluginExample/
  DataInspectorPluginExample.def`) -- everything else there was deleted.
  `asmjit/test/` (~2.6MB of its own unit-test sources) is still present
  and unused -- confirmed via the CMake configure log, `ASMJIT_TEST=FALSE`
  by default -- left in because trimming it carries more verification
  risk than the HxD case for not much size savings; safe to remove later
  if someone re-checks asmjit's own CMakeLists doesn't need it for
  anything else. Also watch for stray `__pycache__/` directories
  appearing under `vendor/` after running the vendored Python scripts
  directly for testing -- `.gitignore` now excludes them, but they were
  manually deleted twice this session before that fix landed.

Everything else -- the machines-root scan, the web UI, the WAV
metadata format below, the CLI itself -- has no assumptions about whose
computer it's running on. The one remaining real platform assumption is
Windows + MinGW itself (see "System requirements" in `README.md`) --
nothing here has been run or verified on macOS/Linux.

## How a render actually happens (the pipeline)

1. **Machine resolution** (`tools/serve.py`, `resolve_machine_id`):
   - `source:<folder>` -- a machine with its own `<PREFIX>.asm56k` +
     `.json` (+ optional `<PREFIX>_icon.pbm`) under the machines root,
     found recursively by `find_source_machines`. Gets assembled+packed
     **fresh on every request** (`build_from_source`, draft/uncertified
     by default -- fast, but not installable on real hardware).
   - `mds:<path>` -- a bare `.mds` with no source pair nearby (e.g.
     `refs/` machines extracted from a real firmware `.syx` dump). Used
     exactly as it sits on disk, no build step.
   - A dev editing a `.asm56k`/`.json`/icon directly and clicking RENDER
     is the whole point of the `source:` path -- there's no manual
     "rebuild" step for them to remember, and a syntax error surfaces
     as a normal UI error message (stderr from `mds_assemble.py` or
     `mds_pack.py`), not a silent failure.
2. **`mds-harness.exe render <mds> ...`** installs the package into the
   real `dsp56kEmu` interpreter, applies parameters, fires TRIG at
   whichever blocks the sequencer step pattern specifies (optionally
   with per-step parameter locks -- "plocks"), calls RENDER per block,
   writes a WAV with the exact render parameters embedded as a RIFF
   `LIST/INFO/ICMT` comment chunk (see `WavWriter::set_comment`).
3. The WAV is normally a tempfile, streamed to the browser and deleted
   immediately after (`_do_render`). Checking "SAVE RENDER" in the UI
   (`?save=1`) instead copies it to `renders/last_render.wav` -- a
   **single stable path**, overwritten each time, specifically so an
   agent reading the filesystem doesn't need to hunt timestamps. That
   file's own embedded comment chunk answers "what parameters/trig
   pattern produced this?" without a separate sidecar file. To read it:
   ```python
   import struct
   data = open("renders/last_render.wav", "rb").read()
   pos = 12
   while pos + 8 <= len(data):
       cid, size = data[pos:pos+4], struct.unpack_from("<I", data, pos+4)[0]
       if cid == b"LIST":
           sub = data[pos+8:pos+8+size][4:]
           sublen = struct.unpack_from("<I", sub, 4)[0]
           print(sub[8:8+sublen].decode())  # the JSON comment
       pos += 8 + size + (size & 1)
   ```
   Known gap: for a `source:`-built machine, the embedded `"mds"` field
   is the throwaway temp build path, not the real source folder --
   useful for everything else in the comment (params, trig pattern,
   neighbour WAVs), just not for tracing back to source by that field
   alone. Not yet fixed; low priority.

   **The comment's JSON schema** (built in `main.cpp`, right after
   `wav.open()`):
   ```json
   {
     "mds": "<path mds-harness.exe was actually given>",
     "bpm": 120,
     "sample_rate": 32000,
     "blocks": 1500,
     "params": {
       "<PARAM_NAME>": { "raw": -1, "value": 0 }
     },
     "trig_at": [
       { "block": 0, "plocks": { "NBAL": 0 } },
       { "block": 500, "plocks": {} }
     ],
     "neighbour_far": "<path, only present if one was given>",
     "neighbour_near": "<path, only present if one was given>"
   }
   ```
   `params` covers every parameter slot the machine declares, keyed by
   name. `raw` is the 0..127 knob position actually used -- `-1` means
   "the machine's own declared default, never overridden by `--param`,"
   any other value means an explicit `--param NAME=VALUE` override was
   given. `value` is that raw value already run through its real
   transform (`unipolar_q23`, `bipolar_q23`, etc. -- see
   `MdsTransform.cpp`) -- i.e. the literal Q23/raw14/etc. word that got
   written into the DSP's own Y-memory. `trig_at` lists every point a
   real `TRIG` fired, in block order; `plocks` is empty (`{}`) for a
   plain trig with no per-step overrides, matching the CLI's own
   `--trig-at BLOCK[:NAME=VAL,...]` syntax exactly.

## The sequencer (step pattern + plocks)

The UI's 16-pad row (`tools/web/index.html`'s `#step-grid`, built by
`app.js`) is a **single row of 16** -- that's the Machinedrum's real
hardware layout; don't "fix" it into a 4x4 grid, that's a different
Elektron box. Clicking a pad toggles whether that step fires a TRIG;
`triggerEntries()` in `app.js` computes which RENDER block each enabled
step falls on, across every bar in BARS, and the engine fires a real
`engine.trigger()` there (`MachineEngine::trigger`, called from
`main.cpp`'s render loop wherever `--trig-at` says to).

**Plocks are authored entirely by keyboard, not by clicking.** Each pad
shows a small key label: `1`-`8` for steps 1-8, `q w e r t y u i` for
steps 9-16 (`PLOCK_KEYS` in `app.js`). Pressing that key snapshots
every knob **currently off its own default value** into that step's
plock set (re-snapshotting from scratch each press -- a knob back at
default correctly drops out), and turns the step on. Double-tapping the
same key within 400ms clears that step's plocks instead, leaving its
on/off state alone. A knob off its default gets a persistent glow ring
(`.knob-dial.off-default`, driven live from `render()`, not tied to
keyboard focus) -- that ring is exactly "what the next plock keypress
would capture." An earlier click-to-select-a-step + per-knob-lock-button
UI was built and then explicitly thrown out in favor of this keyboard
approach -- don't reintroduce it; the user asked for exactly this
replacement, not an addition.

Wire format: `--trig-at 500` (plain trig) or
`--trig-at "500:NBAL=80,RATE=40"` (with a plock list). Between the web
UI and the CLI, `tools/serve.py`'s `trig_at` query param joins multiple
trig points with `;` (NOT `,` -- a single point's own plock list already
uses `,` internally, e.g. `0;500:NBAL=80,RATE=40;1000`). A plock's value
holds for exactly that one step; the **next** trig point (locked or
not) decides every parameter's value again from scratch, reverting
anything not re-locked back to its base `--param`/default value --
implemented in `main.cpp`'s render loop by rewriting every tracked
parameter (not just the locked ones) at each trig block.

Still not built: **independently-authored bars**. The UI only authors
one flat 16-step pattern repeated identically across every bar in
BARS -- the user has asked for up to 4 bars with their own distinct
patterns. Check with the user before assuming this is done.

## Neighbour-input simulation (NFX/neighbour machines)

A neighbour machine reads "tracks back" via a spec-mandated descriptor
table at `Y:$7F0`. This engine fakes two real signals -- NEAR (one track
back) and FAR (two tracks back), each from a bundled or user-uploaded
16-bit PCM WAV, looping independently -- and leaves the three-tracks-back
slot **genuinely silent** (not a duplicate of FAR; that was tried and
explicitly walked back, see git history / session notes if curious).
Bundled defaults: `tools/assets/neighbour_{far,near}.wav` (three 1-second
tones each, low->mid->high for far, reversed for near).

**The TRACKMEM bug (read this before debugging "parameters don't
work"):** `TRACK_BUFFERS` (symbol 6) used to resolve to address `0x0`,
colliding with this engine's own parameter workset -- any machine that
*writes* into its track-memory capture buffer (bounce/delay effects,
`NFX-BO` in particular) was silently overwriting its own NBAL/MIX/etc.
values with captured audio mid-render, making every knob look like a
no-op after the first capture. Fixed by resolving it to a real,
isolated address (`0x001000`, 4096 words -- see `resource_base()` in
`MachineEngine.cpp`). If a *new* machine's parameters mysteriously stop
responding partway through a render, suspect this class of bug again --
check what import symbols it declares and whether `resource_base()`
actually gives each one a real, non-colliding address rather than the
`default: return 0` fallback.

## Reference/extracted machines render silent -- not a bug

Anything under `USR-Machines/_authoring/Machines/refs/` was extracted
from a real firmware `.syx` dump (`tools/syx_to_mds.py` ->
`tools/mds_extract_program.py` -> `dsp56kDisassemble.exe`), not authored
through this project's own `.json`->`.mds` compiler. A `.syx` dump
captures a machine *already loaded* on real hardware, with shared
resources (the firmware's own sine table, etc.) already resolved to the
firmware's real fixed address and baked directly into the program --
there's no relocatable import list left for this engine to intercept,
so those addresses just read zero here. This was investigated
thoroughly (see session history if you need the full trace) and is
explicitly deprioritized by the user -- don't re-investigate it as if
it were a new problem, and don't "fix" it by guessing at real firmware
addresses unless specifically asked.

## Exporting a real, installable `.syx`

The EXPORT .SYX button (`export_syx()` in `serve.py`) runs the **full**
toolchain -- assemble, pack **with `--certify`**, sysex-encode -- not
the fast draft build used for render previews, because
`mds_to_syx.py` explicitly refuses an uncertified package ("draft
package: no installable cycle budget"). **This is genuinely slow --
measured 13m34s wall-clock for one small machine** (`NFX-BO`, 535
program words), yet only ~0.4s of that was actual CPU time, meaning it
was mostly *waiting its turn* rather than computing -- almost certainly
CPU contention from other heavy concurrent processes in that session
(a 15-minute background disassembly fork was running at the same time).
`build_from_source`'s certify timeout is `1800` (30min) specifically
because of this measurement -- don't shrink it back down without
re-measuring on an otherwise-idle machine first; 120s (the original
value) silently failed every single export. If you're running other
heavy CPU-bound work (another agent, a big build) at the same time as
an export, expect it to take even longer, not less. The current knob
values become the exported machine's own defaults (a throwaway copy of
the `.json` manifest, never the real one on disk) -- confirmed as the
intended design with the user, not an assumption.

## Things already tried, fixed, or deliberately rejected this session

- JIT (asmjit x64 backend) crashes under this MinGW build -- forced to
  the plain interpreter (`DSP56K_FORCE_INTERPRETER`). Not revisited
  unless a future milestone genuinely needs JIT speed.
- Static-linked the MinGW runtime into `mds-harness.exe` itself so it
  runs from any shell without MinGW on `PATH` -- the exe itself doesn't
  need the `export PATH=...` line above; only the Python-side tools
  (assembler, certifier, disassembler) do.
- Duplicating FAR into the three-tracks-back buffer was tried and
  explicitly reverted -- the user wants a *real* third signal there
  eventually, not a fake stand-in; silence is the honest placeholder.
- A stale open browser tab masked a real fix landing (the user thought
  a bug persisted when it was actually just an unrefreshed page). Ask
  "did you reload the page?" before deep-diving a UI report that
  contradicts what the server/CLI clearly shows.

## If something seems broken

1. Check the browser tab is actually fresh (hard refresh).
2. Reproduce via direct CLI (`./build/mds-harness.exe render ...`)
   before assuming the bug is in `serve.py` or the web UI -- isolates
   which layer is actually at fault, and is what every real bug this
   session turned out to need (TRACKMEM, buf3, the NBAL confusion were
   all root-caused this way, not by staring at the UI).
3. Dump actual sample values (`wave`/`struct` in Python) rather than
   trusting a human's subjective "I don't hear anything" or "sounds the
   same" -- several apparent bugs this session were real but subtler
   than first described (e.g. a parameter that measurably changes the
   waveform without changing the perceived pitch contour, because a
   downstream capture/bounce engine dominates the output shape).
