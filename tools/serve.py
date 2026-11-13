#!/usr/bin/env python3
"""Local dev server for mds-harness: serves the knob UI and a small API
that shells out to mds-harness.exe itself, so clicking "render" in the
browser actually runs the real DSP56300 interpreter and plays back the
result immediately -- no copy-paste, no manual CLI, no dragging files
around.

Stdlib only, deliberately -- this is a dev tool, not a product; no point
pulling in Flask/etc. for three routes.

Usage:
    python tools/serve.py [--port 8765] [--machines-root <dir>]
"""

import argparse
import http.server
import json
import os
import shutil
import subprocess
import sys
import tempfile
import urllib.parse
import uuid
import webbrowser

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(HERE)
WEB_DIR = os.path.join(HERE, "web")

# Self-contained: the assembler (asm56.exe + its Python front end), the
# packager (mds_pack.py + its lib/), and the certifier (mds-certifier.exe)
# are vendored into this repo's own vendor/ directory rather than assumed
# to live in sibling tools/+mds-tools/ checkouts. mds-harness no longer
# needs anything outside itself (+ a sibling dsp56300 checkout, to BUILD
# mds-harness.exe) to build-from-source or export a .syx.
VENDOR_DIR = os.path.join(REPO_ROOT, "vendor")
MDS_ASSEMBLE = os.path.join(VENDOR_DIR, "mds_assemble.py")
MDS_PACK = os.path.join(VENDOR_DIR, "mds-tools", "mds_pack.py")
MDS_TO_SYX = os.path.join(VENDOR_DIR, "mds-tools", "mds_to_syx.py")
MDS_CERTIFIER = os.path.join(VENDOR_DIR, "mds-tools", "mds-certifier.exe")
CONFIG_PATH = os.path.join(REPO_ROOT, ".mds-harness-config.json")


def find_mingw_bin():
    """Finds a MinGW 'bin' directory holding gcc.exe, so the vendored
    assembler/certifier (both native Windows binaries) can run -- without
    ever needing a source-code edit to work on a different person's
    machine. Tries, in order: a path saved from an earlier run; gcc
    already reachable on PATH (nothing to add); a short list of common
    install locations; then, only if actually running in a terminal a
    person can answer, asks once and remembers the answer for next time.
    Returns None if nothing is found -- rendering an already-built .mds
    still works fine without this; only building-from-source/exporting
    a .syx need it."""
    if os.path.isfile(CONFIG_PATH):
        try:
            with open(CONFIG_PATH, "r", encoding="utf-8") as f:
                saved = json.load(f).get("mingw_bin")
            if saved and os.path.isfile(os.path.join(saved, "gcc.exe")):
                return saved
        except (OSError, ValueError):
            pass

    if shutil.which("gcc") or shutil.which("g++"):
        return None  # already reachable -- nothing to prepend to PATH

    for candidate in (r"C:\mingw64\bin", r"C:\MinGW\bin", r"C:\msys64\mingw64\bin", r"C:\msys64\usr\bin"):
        if os.path.isfile(os.path.join(candidate, "gcc.exe")):
            return candidate

    if sys.stdin.isatty():
        print("MinGW's gcc wasn't found on PATH or in common install locations.")
        print("It's needed to assemble/certify machines from source and to")
        print("export a .syx (rendering an already-built .mds works fine")
        print("without it -- press Enter to skip if that's all you need).")
        try:
            answer = input("MinGW 'bin' directory (e.g. C:\\mingw64\\bin): ").strip()
        except (EOFError, KeyboardInterrupt):
            answer = ""
        if answer and os.path.isfile(os.path.join(answer, "gcc.exe")):
            try:
                with open(CONFIG_PATH, "w", encoding="utf-8") as f:
                    json.dump({"mingw_bin": answer}, f)
            except OSError:
                pass
            return answer
        if answer:
            print(f"Warning: no gcc.exe found under {answer} -- continuing without it.", file=sys.stderr)
    return None


MINGW_BIN = find_mingw_bin()


def env_with_mingw():
    env = os.environ.copy()
    if MINGW_BIN:
        env["PATH"] = MINGW_BIN + os.pathsep + env.get("PATH", "")
    return env

if sys.platform == "win32":
    DEFAULT_EXE = os.path.join(REPO_ROOT, "build", "mds-harness.exe")
else:
    DEFAULT_EXE = os.path.join(REPO_ROOT, "build", "mds-harness")

# Scans wherever the server was actually launched FROM (the shell's cwd
# at invocation time), not a fixed path baked into this repo -- a dev
# can `cd` into their own machines project (anywhere on disk, doesn't
# need to be a sibling of this checkout) and run
# `python /path/to/mds-harness/tools/serve.py` and it finds their
# machines. Override with --machines-root if the shell's cwd isn't
# where the machines actually live.
DEFAULT_MACHINES_ROOT = os.getcwd()

ASSETS_DIR = os.path.join(HERE, "assets")
DEFAULT_NEIGHBOUR_FAR = os.path.join(ASSETS_DIR, "neighbour_far.wav")
DEFAULT_NEIGHBOUR_NEAR = os.path.join(ASSETS_DIR, "neighbour_near.wav")
UPLOAD_DIR = os.path.join(tempfile.gettempdir(), "mds-harness-uploads")
# Renders are normally a throwaway tempfile (streamed to the browser,
# deleted right after) -- this is the opt-in "save render" destination,
# a single stable path so a dev or an agent reading the filesystem later
# always knows exactly where to look rather than hunting timestamps.
RENDERS_DIR = os.path.join(REPO_ROOT, "renders")
LAST_RENDER_PATH = os.path.join(RENDERS_DIR, "last_render.wav")


def find_source_machines(root):
    """Machine folders with a <PREFIX>.asm56k + .json pair, same naming
    convention Machines/build.py uses (hyphen-stripped folder name as
    prefix: NFX-BO -> NFXBO). These get rebuilt from source fresh on
    every params/render request, so source edits are always reflected --
    no stale-cache risk, no separate "did you rebuild?" step."""
    found = []
    source_dirs = set()
    if not os.path.isdir(root):
        return found, source_dirs
    for dirpath, _dirnames, _filenames in os.walk(root):
        name = os.path.basename(dirpath)
        prefix = name.replace("-", "").replace(" ", "")
        asm = os.path.join(dirpath, prefix + ".asm56k")
        manifest = os.path.join(dirpath, prefix + ".json")
        if os.path.isfile(asm) and os.path.isfile(manifest):
            label = os.path.relpath(dirpath, root).replace(os.sep, "/")
            found.append({"id": "source:" + dirpath, "label": label})
            source_dirs.add(os.path.normpath(dirpath))
    found.sort(key=lambda m: m["label"])
    return found, source_dirs


def find_prebuilt_mds(root, source_dirs):
    """.mds files under folders with no source pair of their own (e.g.
    refs/ machines extracted from a real firmware dump) -- these have
    nothing to rebuild, so they're used exactly as they sit on disk."""
    found = []
    for dirpath, _dirnames, filenames in os.walk(root):
        if os.path.normpath(dirpath) in source_dirs:
            continue
        for name in filenames:
            if name.lower().endswith(".mds"):
                path = os.path.normpath(os.path.join(dirpath, name))
                found.append({"id": "mds:" + path, "label": os.path.relpath(path, root)})
    found.sort(key=lambda m: m["label"])
    return found


def build_from_source(folder, certify=False, param_overrides=None):
    """Runs the same assemble+pack steps as Machines/build.py's first two
    stages and returns (mds_path, build_dir_to_clean_up, error).

    By default (certify=False) this skips --certify -- fast, fine for
    rendering a preview, but the resulting .mds has no installable cycle
    budget and mds_to_syx.py will refuse it. Pass certify=True (slower,
    runs the real certifier) when the output needs to become a real
    .syx -- see export_syx_from_source below.

    param_overrides, if given ({NAME: raw 0..127}), rewrites the knob
    defaults in a throwaway copy of the manifest before packing -- lets
    an export bake in "wherever you left the knobs in the UI" rather
    than always exporting the machine's original authored defaults.
    Never touches the real .json on disk.
    """
    name = os.path.basename(os.path.normpath(folder))
    prefix = name.replace("-", "").replace(" ", "")
    asm = os.path.join(folder, prefix + ".asm56k")
    manifest = os.path.join(folder, prefix + ".json")
    icon = os.path.join(folder, prefix + "_icon.pbm")

    build_dir = tempfile.mkdtemp(prefix="mdsh-build-")
    lod = os.path.join(build_dir, prefix + ".lod")
    symbols = os.path.join(build_dir, prefix + ".symbols.json")
    mds_out = os.path.join(build_dir, prefix + ".mds")

    env = env_with_mingw()

    def run(cmd, timeout=60):
        try:
            return subprocess.run(cmd, capture_output=True, timeout=timeout, env=env)
        except subprocess.TimeoutExpired:
            return None

    assemble = run(["python", MDS_ASSEMBLE, asm, "--out", lod, "--symbols-out", symbols])
    if assemble is None or assemble.returncode != 0:
        shutil.rmtree(build_dir, ignore_errors=True)
        msg = "build timed out" if assemble is None else (
            assemble.stderr.decode("utf-8", "replace") or assemble.stdout.decode("utf-8", "replace") or "assemble failed"
        )
        return None, None, "assemble failed:\n" + msg

    manifest_to_pack = manifest
    if param_overrides:
        with open(manifest, "r", encoding="utf-8") as f:
            manifest_data = json.load(f)
        for p in manifest_data.get("params", []):
            raw = param_overrides.get(str(p.get("name", "")).upper())
            if raw is not None:
                p["default"] = raw
        manifest_to_pack = os.path.join(build_dir, prefix + ".overridden.json")
        with open(manifest_to_pack, "w", encoding="utf-8") as f:
            json.dump(manifest_data, f)

    pack_cmd = ["python", MDS_PACK, manifest_to_pack, lod, "--format", "lod", "-o", mds_out]
    if os.path.isfile(icon):
        pack_cmd += ["--bitmap", icon]
    if certify:
        # Explicit --certifier: mds_pack.py's own auto-discovery looks
        # for ../build/mds-certifier(.exe) relative to ITS OWN location,
        # which matched the original mds-tools/tools/ + mds-tools/build/
        # layout by coincidence -- our vendored copy sits one directory
        # shallower (vendor/mds-tools/), so that auto-discovery would
        # silently look in the wrong place. Pointing at it directly
        # sidesteps relying on exact relative-depth coincidences.
        pack_cmd += ["--certify", "--certifier", MDS_CERTIFIER,
                     "--report", os.path.join(build_dir, prefix + "_cert.json")]
    # The real certifier's worst-case cycle search is genuinely slow --
    # measured 13+ minutes wall-clock on this machine (under concurrent
    # CPU load from other tools; 0.4s of that was actual CPU time, so
    # it's mostly waiting its turn, not computing) -- 120s was nowhere
    # near enough and silently failed every export. 1800s (30min) errs
    # generously rather than risk cutting off a real, slow-but-succeeding
    # certify run.
    pack = run(pack_cmd, timeout=1800 if certify else 60)
    if pack is None or pack.returncode != 0:
        shutil.rmtree(build_dir, ignore_errors=True)
        msg = "build timed out" if pack is None else (
            pack.stderr.decode("utf-8", "replace") or pack.stdout.decode("utf-8", "replace") or "pack failed"
        )
        return None, None, "pack failed:\n" + msg

    return mds_out, build_dir, None


def resolve_machine_id(identifier):
    """Turns an /api/machines `id` (or a bare legacy .mds path) into an
    actual .mds file to render, building fresh from source first when
    needed. Returns (mds_path, build_dir_to_clean_up_or_None, error)."""
    if identifier.startswith("mds:"):
        return identifier[len("mds:"):], None, None
    if identifier.startswith("source:"):
        return build_from_source(identifier[len("source:"):])
    return identifier, None, None  # bare path, back-compat with direct API callers


def export_syx(identifier, slot, param_overrides):
    """Produces a real, installable .syx for `identifier` -- runs the
    FULL toolchain a dev would otherwise run by hand (assemble, pack
    WITH --certify, sysex-encode), not the fast draft build used for
    render previews. param_overrides lets "wherever the knobs are right
    now in the UI" become the exported machine's own defaults. Returns
    (bytes, filename, error)."""
    env = env_with_mingw()

    def encode(mds_path, prefix, out_dir):
        syx_out = os.path.join(out_dir, prefix + ".syx")
        try:
            r = subprocess.run(
                ["python", MDS_TO_SYX, mds_path, "--slot", str(slot), "-o", syx_out],
                capture_output=True, timeout=30, env=env,
            )
        except subprocess.TimeoutExpired:
            return None, "syx-encode timed out"
        if r.returncode != 0:
            return None, "syx-encode failed:\n" + (
                r.stderr.decode("utf-8", "replace") or r.stdout.decode("utf-8", "replace") or "unknown error"
            )
        with open(syx_out, "rb") as f:
            return f.read(), None

    if identifier.startswith("source:"):
        folder = identifier[len("source:"):]
        prefix = os.path.basename(os.path.normpath(folder)).replace("-", "").replace(" ", "")
        mds_path, build_dir, err = build_from_source(folder, certify=True, param_overrides=param_overrides)
        if err:
            return None, None, err
        try:
            data, err = encode(mds_path, prefix, build_dir)
            if err:
                return None, None, err
            return data, prefix + ".syx", None
        finally:
            shutil.rmtree(build_dir, ignore_errors=True)

    if identifier.startswith("mds:"):
        mds_path = identifier[len("mds:"):]
        prefix = os.path.splitext(os.path.basename(mds_path))[0]
        tmp_dir = tempfile.mkdtemp(prefix="mdsh-syx-")
        try:
            # No source/.json manifest for these (e.g. refs/ machines
            # extracted from a firmware dump) -- knob overrides can't be
            # baked into a new manifest, so export the .mds exactly as it
            # already sits on disk. If it was never certified, this
            # fails with mds_to_syx.py's own clear error -- not papered
            # over here.
            data, err = encode(mds_path, prefix, tmp_dir)
            if err:
                return None, None, err
            return data, prefix + ".syx", None
        finally:
            shutil.rmtree(tmp_dir, ignore_errors=True)

    return None, None, "export requires a machine loaded from this server's own machine list"


class Handler(http.server.BaseHTTPRequestHandler):
    exe_path = DEFAULT_EXE
    machines_root = DEFAULT_MACHINES_ROOT

    def log_message(self, fmt, *args):  # quieter default logging
        sys.stderr.write("%s - %s\n" % (self.address_string(), fmt % args))

    def _send(self, status, content_type, body):
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def _send_json(self, status, obj):
        self._send(status, "application/json", json.dumps(obj).encode("utf-8"))

    def _static(self, rel_path):
        path = os.path.normpath(os.path.join(WEB_DIR, rel_path.lstrip("/")))
        if not path.startswith(WEB_DIR) or not os.path.isfile(path):
            self._send(404, "text/plain", b"not found")
            return
        ext = os.path.splitext(path)[1].lower()
        content_type = {
            ".html": "text/html; charset=utf-8",
            ".js": "application/javascript; charset=utf-8",
            ".css": "text/css; charset=utf-8",
        }.get(ext, "application/octet-stream")
        with open(path, "rb") as f:
            self._send(200, content_type, f.read())

    def do_GET(self):
        parsed = urllib.parse.urlparse(self.path)
        query = urllib.parse.parse_qs(parsed.query)

        if parsed.path == "/" or parsed.path == "/index.html":
            self._static("index.html")
        elif parsed.path in ("/app.js", "/style.css"):
            self._static(parsed.path)
        elif parsed.path == "/api/machines":
            self._api_machines()
        elif parsed.path == "/api/params":
            self._api_params(query)
        elif parsed.path == "/api/render":
            self._api_render(query)
        elif parsed.path == "/api/export-syx":
            self._api_export_syx(query)
        elif parsed.path == "/api/neighbour-defaults":
            self._send_json(200, {
                "far": {"path": DEFAULT_NEIGHBOUR_FAR, "label": "default (low→mid→high)"},
                "near": {"path": DEFAULT_NEIGHBOUR_NEAR, "label": "default (high→mid→low)"},
            })
        else:
            self._send(404, "text/plain", b"not found")

    def do_POST(self):
        parsed = urllib.parse.urlparse(self.path)
        query = urllib.parse.parse_qs(parsed.query)
        if parsed.path == "/api/upload-neighbour":
            self._api_upload_neighbour(query)
        else:
            self._send(404, "text/plain", b"not found")

    def _api_upload_neighbour(self, query):
        which = query.get("which", [""])[0]
        if which not in ("far", "near"):
            self._send_json(400, {"error": "?which= must be 'far' or 'near'"})
            return
        length = int(self.headers.get("Content-Length", "0"))
        if length <= 0 or length > 50 * 1024 * 1024:
            self._send_json(400, {"error": "missing or too-large request body (max 50MB)"})
            return
        body = self.rfile.read(length)
        os.makedirs(UPLOAD_DIR, exist_ok=True)
        out_path = os.path.join(UPLOAD_DIR, "%s-%s.wav" % (which, uuid.uuid4().hex))
        with open(out_path, "wb") as f:
            f.write(body)
        self._send_json(200, {"path": out_path})

    def _api_machines(self):
        try:
            source_machines, source_dirs = find_source_machines(self.machines_root)
            prebuilt = find_prebuilt_mds(self.machines_root, source_dirs)
        except OSError as e:
            self._send_json(500, {"error": str(e)})
            return
        machines = sorted(source_machines, key=lambda m: m["label"]) + prebuilt
        self._send_json(200, {"root": self.machines_root, "machines": machines})

    def _run_exe(self, args):
        if not os.path.isfile(self.exe_path):
            return None, "mds-harness executable not found at " + self.exe_path
        try:
            result = subprocess.run(
                [self.exe_path] + args, capture_output=True, timeout=30
            )
        except subprocess.TimeoutExpired:
            return None, "render timed out"
        if result.returncode != 0:
            return None, result.stderr.decode("utf-8", "replace") or "render failed"
        return result, None

    def _api_params(self, query):
        identifier = query.get("mds", [None])[0]
        if not identifier:
            self._send_json(400, {"error": "missing ?mds="})
            return
        mds, build_dir, err = resolve_machine_id(identifier)
        if err:
            self._send_json(500, {"error": err})
            return
        try:
            result, err = self._run_exe(["render", mds, "--list-params-json"])
            if err:
                self._send_json(500, {"error": err})
                return
            self._send(200, "application/json", result.stdout)
        finally:
            if build_dir:
                shutil.rmtree(build_dir, ignore_errors=True)

    def _api_render(self, query):
        identifier = query.get("mds", [None])[0]
        if not identifier:
            self._send_json(400, {"error": "missing ?mds="})
            return
        mds, build_dir, err = resolve_machine_id(identifier)
        if err:
            self._send_json(500, {"error": err})
            return
        try:
            self._do_render(mds, query)
        finally:
            if build_dir:
                shutil.rmtree(build_dir, ignore_errors=True)

    def _do_render(self, mds, query):
        args = ["render", mds]
        # trig_at: comma-separated block numbers, one per enabled sequencer
        # step (computed client-side from the step grid + bars + bpm).
        # Falls back to a single trig at block 0 (old behavior) if the UI
        # didn't send a step pattern at all. ";" separates trig points --
        # NOT "," -- because a single point can itself be
        # "500:NBAL=80,RATE=40" (a plock list), which needs "," internally.
        trig_at_raw = query.get("trig_at", [None])[0]
        if trig_at_raw:
            for tok in trig_at_raw.split(";"):
                tok = tok.strip()
                if tok:
                    args += ["--trig-at", tok]
        elif query.get("trig", ["0"])[0] == "1":
            args.append("--trig")
        args += ["--blocks", query.get("blocks", ["500"])[0]]
        if "bpm" in query:
            args += ["--bpm", query["bpm"][0]]

        # Neighbour simulation WAVs: default to the bundled far/near pair
        # unless the UI passed a path from a prior /api/upload-neighbour
        # call. Only accept paths actually inside our own asset/upload
        # directories -- this is a local dev server, but there's no reason
        # to let a render request read an arbitrary file off disk.
        def resolve_neighbour(raw, default_path):
            if not raw:
                return default_path
            resolved = os.path.abspath(raw)
            if resolved.startswith(os.path.abspath(ASSETS_DIR)) or resolved.startswith(os.path.abspath(UPLOAD_DIR)):
                return resolved
            return default_path

        far_wav = resolve_neighbour(query.get("neighbour_far", [None])[0], DEFAULT_NEIGHBOUR_FAR)
        near_wav = resolve_neighbour(query.get("neighbour_near", [None])[0], DEFAULT_NEIGHBOUR_NEAR)
        if os.path.isfile(far_wav):
            args += ["--neighbour-far", far_wav]
        if os.path.isfile(near_wav):
            args += ["--neighbour-near", near_wav]

        for key, values in query.items():
            if key in ("mds", "trig", "trig_at", "blocks", "bpm", "neighbour_far", "neighbour_near", "save"):
                continue
            args += ["--param", "%s=%s" % (key, values[0])]

        with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as tmp:
            tmp_path = tmp.name
        try:
            args += ["-o", tmp_path]
            _result, err = self._run_exe(args)
            if err:
                self._send_json(500, {"error": err})
                return
            with open(tmp_path, "rb") as f:
                data = f.read()
            if query.get("save", ["0"])[0] == "1":
                os.makedirs(RENDERS_DIR, exist_ok=True)
                with open(LAST_RENDER_PATH, "wb") as f:
                    f.write(data)
                print("saved render: " + LAST_RENDER_PATH, file=sys.stderr)
            self._send(200, "audio/wav", data)
        finally:
            try:
                os.remove(tmp_path)
            except OSError:
                pass

    def _api_export_syx(self, query):
        identifier = query.get("mds", [None])[0]
        if not identifier:
            self._send_json(400, {"error": "missing ?mds="})
            return
        try:
            slot = int(query.get("slot", ["1"])[0])
        except ValueError:
            slot = 1
        slot = max(1, min(16, slot))

        overrides = {}
        for key, values in query.items():
            if key in ("mds", "slot"):
                continue
            try:
                overrides[key.upper()] = max(0, min(127, int(values[0])))
            except ValueError:
                continue

        data, filename, err = export_syx(identifier, slot, overrides)
        if err:
            self._send_json(500, {"error": err})
            return
        self.send_response(200)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Content-Disposition", 'attachment; filename="%s"' % filename)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(data)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--exe", default=DEFAULT_EXE, help="path to mds-harness(.exe)")
    parser.add_argument("--machines-root", default=DEFAULT_MACHINES_ROOT)
    parser.add_argument("--no-open", action="store_true", help="don't launch a browser tab automatically")
    args = parser.parse_args()

    Handler.exe_path = os.path.abspath(args.exe)
    Handler.machines_root = os.path.abspath(args.machines_root)

    url = f"http://127.0.0.1:{args.port}"
    server = http.server.ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    print(f"mds-harness UI: {url}")
    print(f"  exe:           {Handler.exe_path}")
    print(f"  machines root: {Handler.machines_root}")
    if not args.no_open:
        webbrowser.open(url)  # socket is already bound above, so the tab has something to load
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
