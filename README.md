# mds-harness

A standalone development harness for Elektron Machinedrum MDS 1.1 custom
"User Machines" — load a `.mds` package into the real `dsp56300` emulator
core (the same interpreter that backs `mds-tools`' certifier/disassembler)
and render or play it, without needing real hardware or a full
build→certify→flash→listen round trip.

## Why

Building a custom USR machine today means editing assembly, running the
full certification pipeline, flashing a `.syx` to real hardware (or an
emulator), and listening — a slow loop for iterating on anything
synthesis-related. This project exists to shortcut that loop for
development: load a machine, poke its parameters, hear the result,
immediately.

## Plan (thin slices, in order)

1. **WAV-dump CLI** — load a `.mds`, set fixed parameter values, trigger
   once, render N blocks, write a `.wav` file. No live audio yet.
2. **Hot-reload parameters** — watch a JSON/text file for parameter
   changes between blocks, re-render. Still file-based, not realtime.
3. **Live audio** — drive the engine via [PortAudio](https://www.portaudio.com/)
   (MIT-licensed) for real-time playback and live parameter tweaking.
4. **JUCE wrap** — later, on a separate JUCE/C++ project — drop the core
   engine into an `AudioProcessor`, swapping the audio-driver layer only.

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

## Status

Just started (2026-11-13) — repo scaffolding only, no engine code yet.
