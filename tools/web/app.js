"use strict";

const machineSelect = document.getElementById("machine-select");
const knobPanel = document.getElementById("knob-panel");
const renderBtn = document.getElementById("render-btn");
const exportBtn = document.getElementById("export-btn");
const player = document.getElementById("player");
const statusEl = document.getElementById("status");
const barsEl = document.getElementById("bars");
const bpmEl = document.getElementById("bpm");
const saveRenderEl = document.getElementById("save-render");

// Matches the engine's own default sample rate (main.cpp, --sample-rate
// default 32000) and this project's own RENDER-block size (32 samples).
// 4 beats/bar is a 4/4 assumption -- the MDS format has no time-signature
// field, and nothing here claims otherwise.
const SAMPLE_RATE = 32000;
const BEATS_PER_BAR = 4;
const SAMPLES_PER_BLOCK = 32;

function barsToBlocks(bars, bpm) {
  const secondsPerBar = (BEATS_PER_BAR * 60) / bpm;
  const samples = bars * secondsPerBar * SAMPLE_RATE;
  return Math.max(1, Math.round(samples / SAMPLES_PER_BLOCK));
}

let knobs = []; // {name, value, default, min, max}
let lastObjectUrl = null;

// Step sequencer: 16 steps/bar (the Machinedrum's native resolution),
// repeated across however many BARS the transport is set to. Defaults to
// step 1 only, matching this tool's old "always trig once at the start"
// behavior -- nothing changes for anyone who ignores this section.
const STEPS_PER_BAR = 16;
const stepGrid = document.getElementById("step-grid");
const stepsOn = new Array(STEPS_PER_BAR).fill(false);
stepsOn[0] = true;

// Parameter locks ("plocks", Elektron's own term): plocks[step] is a
// {PARAM_NAME: rawValue} map of this one step's own overrides, applied
// only for the trig at that step -- the next trig point (locked or not)
// decides every parameter fresh (see main.cpp's render loop). Scoped per
// STEP, not per bar, matching the UI's current single-pattern-repeated-
// across-every-bar model (see CLAUDE.md -- distinct per-bar patterns are
// a follow-up, not yet built).
//
// Authored entirely via keyboard, Elektron-live-record style: press
// 1-8 to snapshot every knob's CURRENT value as a plock for steps 1-8,
// qwertyui for steps 9-16. One keystroke = "freeze exactly how the
// knobs are right now into this step" (and turns that step on, since a
// plock on a step that never fires is just a trap waiting to confuse
// someone). No step-select/per-knob-lock-button UI -- this is faster
// and matches how a person actually plays with knobs while locking.
const plocks = Array.from({ length: STEPS_PER_BAR }, () => ({}));
const PLOCK_KEYS = "12345678qwertyui";  // index 0-15 -> step 1-16

function stepHasPlocks(step) {
  return Object.keys(plocks[step]).length > 0;
}

function refreshStepPad(i) {
  const pad = stepGrid.children[i];
  pad.classList.toggle("on", stepsOn[i]);
  pad.classList.toggle("has-plock", stepHasPlocks(i));
  pad.setAttribute("aria-checked", String(stepsOn[i]));
}

for (let i = 0; i < STEPS_PER_BAR; i++) {
  const pad = document.createElement("button");
  pad.type = "button";
  pad.className = "step-pad";
  pad.setAttribute("role", "switch");
  pad.setAttribute("aria-checked", "false");
  pad.setAttribute("aria-label", `step ${i + 1}`);
  const key = document.createElement("span");
  key.className = "step-pad-key";
  key.textContent = PLOCK_KEYS[i];
  pad.appendChild(key);
  pad.addEventListener("click", () => {
    stepsOn[i] = !stepsOn[i];
    refreshStepPad(i);
  });
  stepGrid.appendChild(pad);
}
refreshStepPad(0);

const DOUBLE_TAP_MS = 400;
const lastPlockKeyTime = new Array(STEPS_PER_BAR).fill(0);

document.addEventListener("keydown", (ev) => {
  if (ev.target.tagName === "INPUT" || ev.target.tagName === "TEXTAREA") return;
  if (ev.ctrlKey || ev.metaKey || ev.altKey) return;
  if (ev.repeat) return;  // holding the key down is not repeated tapping
  const idx = PLOCK_KEYS.indexOf(ev.key.toLowerCase());
  if (idx === -1) return;
  ev.preventDefault();

  const now = performance.now();
  const isDoubleTap = now - lastPlockKeyTime[idx] < DOUBLE_TAP_MS;
  lastPlockKeyTime[idx] = isDoubleTap ? 0 : now;  // reset so a triple-tap doesn't double-clear/re-lock oddly
  const lock = plocks[idx];

  if (isDoubleTap) {
    for (const key of Object.keys(lock)) delete lock[key];
    refreshStepPad(idx);
    setStatus(`cleared step ${idx + 1}'s plocks`);
    return;
  }

  // Single tap: re-snapshot from scratch -- only knobs currently off
  // their own default go into the lock, not every knob -- a plock
  // should capture an intentional deviation, not pin every parameter
  // (including ones nobody touched) to whatever they happened to read
  // at that moment. If a previously-locked knob has since been moved
  // back to its default, it correctly drops out of this step's plock.
  for (const key of Object.keys(lock)) delete lock[key];
  for (const k of knobs) if (k.value !== k.default) lock[k.name] = k.value;
  stepsOn[idx] = true;
  refreshStepPad(idx);
  const n = Object.keys(lock).length;
  setStatus(n ? `plocked step ${idx + 1}: ${n} knob(s) off default` : `step ${idx + 1} has no plocks (every knob is at its default)`);
});

// Computes the --trig-at entries main.cpp needs: one per enabled step,
// across every bar in the render, each carrying that step's own plocks
// (if any) as "NAME=VALUE,NAME=VALUE" -- simulating the Machinedrum
// sequencer choosing where triggers land (and what they lock) rather
// than this tool always firing just one plain trig.
function triggerEntries(bars, bpm) {
  const blocksPerBar = barsToBlocks(1, bpm);
  const blocksPerStep = blocksPerBar / STEPS_PER_BAR;
  const entries = [];
  for (let bar = 0; bar < bars; bar++) {
    for (let step = 0; step < STEPS_PER_BAR; step++) {
      if (!stepsOn[step]) continue;
      const block = Math.round(bar * blocksPerBar + step * blocksPerStep);
      const lock = plocks[step];
      const lockPart = Object.keys(lock).length
        ? ":" + Object.entries(lock).map(([name, v]) => `${name}=${v}`).join(",")
        : "";
      entries.push(`${block}${lockPart}`);
    }
  }
  return entries;
}

// Neighbour-input simulation: null means "use the bundled default" (the
// server supplies that path itself); a string is an uploaded override's
// server-side path, returned by /api/upload-neighbour.
const neighbourBay = document.getElementById("neighbour-bay");
const neighbourPaths = { far: null, near: null };

function setupNeighbourSlot(which) {
  const slot = neighbourBay.querySelector(`.neighbour-slot[data-which="${which}"]`);
  const fileInput = slot.querySelector('[data-role="file"]');
  const currentEl = slot.querySelector('[data-role="current"]');
  const resetBtn = slot.querySelector('[data-role="reset"]');
  const defaultText = currentEl.textContent;

  fileInput.addEventListener("change", async () => {
    const file = fileInput.files[0];
    if (!file) return;
    currentEl.textContent = "uploading…";
    try {
      const res = await fetch(`/api/upload-neighbour?which=${which}`, { method: "POST", body: file });
      const data = await res.json();
      if (!res.ok) throw new Error(data.error || "upload failed");
      neighbourPaths[which] = data.path;
      currentEl.textContent = file.name;
    } catch (err) {
      currentEl.textContent = defaultText;
      setStatus(err.message, true);
    }
  });

  resetBtn.addEventListener("click", () => {
    neighbourPaths[which] = null;
    currentEl.textContent = defaultText;
    fileInput.value = "";
  });
}
setupNeighbourSlot("far");
setupNeighbourSlot("near");

function setStatus(text, isError) {
  statusEl.textContent = text;
  statusEl.classList.toggle("error", Boolean(isError));
}

function valueToDeg(value, min, max) {
  const t = max > min ? (value - min) / (max - min) : 0;
  return -135 + t * 270;
}

function buildKnob(param) {
  const wrap = document.createElement("div");
  wrap.className = "knob-module";

  const name = document.createElement("div");
  name.className = "knob-name";
  name.textContent = param.name;
  wrap.appendChild(name);

  const dial = document.createElement("div");
  dial.className = "knob-dial";
  dial.tabIndex = 0;
  dial.setAttribute("role", "slider");
  dial.setAttribute("aria-label", param.name);
  dial.setAttribute("aria-valuemin", String(param.min));
  dial.setAttribute("aria-valuemax", String(param.max));

  // conversion 3 = Bipolar Q23 (spec: "0 becomes -1, 64 becomes zero") --
  // mark its ends -/+ since the middle is "zero", not a plain minimum.
  // Everything else (raw14, unipolar/squared Q23, pitch, boolean, index,
  // decay, tempo) runs start-to-finish, so MIN/MAX is the honest label.
  const isBipolar = param.conversion === 3;
  const minText = isBipolar ? "−" : "MIN";
  const maxText = isBipolar ? "+" : "MAX";

  for (const kind of ["min", "mid", "max"]) {
    const tick = document.createElement("div");
    tick.className = `knob-tick ${kind}`;
    dial.appendChild(tick);
  }

  const pointer = document.createElement("div");
  pointer.className = "knob-pointer";
  dial.appendChild(pointer);

  const minLabel = document.createElement("span");
  minLabel.className = "knob-end-label min";
  minLabel.textContent = minText;
  dial.appendChild(minLabel);

  const maxLabel = document.createElement("span");
  maxLabel.className = "knob-end-label max";
  maxLabel.textContent = maxText;
  dial.appendChild(maxLabel);

  wrap.appendChild(dial);

  const input = document.createElement("input");
  input.type = "number";
  input.className = "knob-value-input";
  input.min = String(param.min);
  input.max = String(param.max);
  wrap.appendChild(input);

  const reset = document.createElement("button");
  reset.className = "knob-reset";
  reset.type = "button";
  reset.textContent = "reset";
  wrap.appendChild(reset);

  const state = { name: param.name, value: param.default, min: param.min, max: param.max, default: param.default };

  function render() {
    pointer.style.transform = `translateX(-50%) rotate(${valueToDeg(state.value, state.min, state.max)}deg)`;
    input.value = String(state.value);
    dial.setAttribute("aria-valuenow", String(state.value));
    // Live indicator, independent of keyboard focus: a knob off its own
    // default is exactly what the next plock keypress (1-8/qwertyui)
    // would capture into that step -- this ring is "will be plocked if
    // you press a step key right now," not a record of a past plock.
    dial.classList.toggle("off-default", state.value !== state.default);
  }

  function setValue(v) {
    state.value = Math.max(state.min, Math.min(state.max, Math.round(v)));
    render();
  }

  dial.addEventListener("pointerdown", (ev) => {
    dial.setPointerCapture(ev.pointerId);
    const startY = ev.clientY;
    const startValue = state.value;
    const range = state.max - state.min || 1;
    function onMove(moveEv) {
      const deltaY = startY - moveEv.clientY; // drag up = increase
      const deltaValue = (deltaY / 120) * range;
      setValue(startValue + deltaValue);
    }
    function onUp() {
      dial.removeEventListener("pointermove", onMove);
      dial.removeEventListener("pointerup", onUp);
    }
    dial.addEventListener("pointermove", onMove);
    dial.addEventListener("pointerup", onUp);
  });

  dial.addEventListener("keydown", (ev) => {
    if (ev.key === "ArrowUp" || ev.key === "ArrowRight") { setValue(state.value + 1); ev.preventDefault(); }
    if (ev.key === "ArrowDown" || ev.key === "ArrowLeft") { setValue(state.value - 1); ev.preventDefault(); }
  });

  input.addEventListener("change", () => setValue(Number(input.value)));
  reset.addEventListener("click", () => setValue(state.default));

  render();
  knobPanel.appendChild(wrap);
  return state;
}

async function loadParams(mdsPath) {
  knobPanel.innerHTML = "";
  knobs = [];
  // Plocks are keyed by parameter NAME, which is machine-specific --
  // carrying them over to a different machine would silently attach
  // meaningless leftover locks, so clear on every machine switch.
  for (const lock of plocks) for (const key of Object.keys(lock)) delete lock[key];
  for (let j = 0; j < STEPS_PER_BAR; j++) refreshStepPad(j);
  setStatus("loading parameters…");
  try {
    const res = await fetch(`/api/params?mds=${encodeURIComponent(mdsPath)}`);
    const data = await res.json();
    if (!res.ok) throw new Error(data.error || "failed to load parameters");
    if (!data.params.length) {
      knobPanel.innerHTML = '<p class="empty-state">This machine declares no parameters.</p>';
    }
    for (const param of data.params) knobs.push(buildKnob(param));
    neighbourBay.hidden = !data.neighbour;
    setStatus(`${data.params.length} parameter(s) loaded`);
  } catch (err) {
    knobPanel.innerHTML = `<p class="empty-state">${err.message}</p>`;
    setStatus(err.message, true);
  }
}

async function loadMachines() {
  setStatus("loading machine list…");
  try {
    const res = await fetch("/api/machines");
    const data = await res.json();
    if (!res.ok) throw new Error(data.error || "failed to list machines");
    machineSelect.innerHTML = "";
    for (const m of data.machines) {
      const opt = document.createElement("option");
      opt.value = m.id;
      opt.textContent = m.label;
      machineSelect.appendChild(opt);
    }
    if (data.machines.length) {
      machineSelect.value = data.machines[0].id;
      await loadParams(data.machines[0].id);
    } else {
      setStatus("no .mds files found under " + data.root, true);
    }
  } catch (err) {
    setStatus(err.message, true);
  }
}

machineSelect.addEventListener("change", () => loadParams(machineSelect.value));

renderBtn.addEventListener("click", async () => {
  renderBtn.disabled = true;
  setStatus("rendering…");
  try {
    const bpm = Number(bpmEl.value);
    const bars = Number(barsEl.value);
    const params = new URLSearchParams();
    params.set("mds", machineSelect.value);
    params.set("trig_at", triggerEntries(bars, bpm).join(";"));
    params.set("blocks", String(barsToBlocks(bars, bpm)));
    params.set("bpm", String(bpm));
    if (saveRenderEl.checked) params.set("save", "1");
    if (!neighbourBay.hidden) {
      if (neighbourPaths.far) params.set("neighbour_far", neighbourPaths.far);
      if (neighbourPaths.near) params.set("neighbour_near", neighbourPaths.near);
    }
    for (const k of knobs) params.set(k.name, String(k.value));

    const res = await fetch(`/api/render?${params.toString()}`);
    if (!res.ok) {
      const data = await res.json().catch(() => ({}));
      throw new Error(data.error || "render failed");
    }
    const blob = await res.blob();
    if (lastObjectUrl) URL.revokeObjectURL(lastObjectUrl);
    lastObjectUrl = URL.createObjectURL(blob);
    player.src = lastObjectUrl;
    await player.play();
    setStatus("playing");
  } catch (err) {
    setStatus(err.message, true);
  } finally {
    renderBtn.disabled = false;
  }
});

exportBtn.addEventListener("click", async () => {
  exportBtn.disabled = true;
  setStatus("exporting .syx (runs the full toolchain -- assemble, certify, sysex-encode. The real certifier's worst-case cycle search is slow, measured 13+ min under load; this can take a while)…");
  try {
    const params = new URLSearchParams();
    params.set("mds", machineSelect.value);
    // Destination slot defaults to 1 server-side (same as Machines/build.py);
    // it's install-time metadata only, changeable later at install, not
    // worth a dedicated UI control.
    for (const k of knobs) params.set(k.name, String(k.value)); // current knobs become the exported machine's defaults

    const res = await fetch(`/api/export-syx?${params.toString()}`);
    if (!res.ok) {
      const data = await res.json().catch(() => ({}));
      throw new Error(data.error || "export failed");
    }
    const disposition = res.headers.get("Content-Disposition") || "";
    const match = disposition.match(/filename="([^"]+)"/);
    const filename = match ? match[1] : "machine.syx";

    const blob = await res.blob();
    const url = URL.createObjectURL(blob);
    const a = document.createElement("a");
    a.href = url;
    a.download = filename;
    document.body.appendChild(a);
    a.click();
    a.remove();
    URL.revokeObjectURL(url);
    setStatus(`exported ${filename}`);
  } catch (err) {
    setStatus(err.message, true);
  } finally {
    exportBtn.disabled = false;
  }
});

loadMachines();
