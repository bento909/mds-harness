#!/usr/bin/env python3
"""Assemble a plain DSP56300 .asm56k source file into a CLDLOD .lod text file
that mds_pack.py (vendored alongside this file, under vendor/mds-tools/) can
consume directly with --format lod.

Vendored copy: this is mds-harness's own self-contained copy of the
Machinedrum project's tools/mds_assemble.py, adapted only to import the
assembler module and locate its native encoder exe from vendor/assembler/
(this repo's own copy) instead of a sibling kitbasher/ checkout. Logic is
otherwise unchanged -- see vendor/assembler/assembly.py's own docstring
for what it does and does not do.

Usage:
    python vendor/mds_assemble.py machine.asm56k --out machine.lod \
        [--equ name=value ...] [--symbols-out machine.symbols.json]
"""
import argparse
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ASSEMBLER_DIR = HERE / "assembler"
DEFAULT_ASM56_EXE = ASSEMBLER_DIR / "asm56.exe"


def load_assembly_module():
    sys.path.insert(0, str(ASSEMBLER_DIR))
    import assembly as A  # noqa: E402
    return A


def parse_equ(pairs):
    symbols = {}
    for pair in pairs or []:
        if "=" not in pair:
            raise ValueError(f"--equ expects name=value, got {pair!r}")
        name, value = pair.split("=", 1)
        value = value.strip()
        symbols[name.strip()] = int(value, 16 if value.lower().startswith("0x") else 10)
    return symbols


def write_lod(words, labels, out_path, org=0):
    lines = ["_DATA P " + format(org, "x")]
    # 8 words per line keeps lines readable; the parser does not care about width.
    for i in range(0, len(words), 8):
        lines.append(" ".join(format(w & 0xFFFFFF, "06x") for w in words[i:i + 8]))
    if labels:
        lines.append("_SYMBOL P")
        for name, value in labels.items():
            lines.append(f"{name} G {value:x}")
    lines.append("_END")
    out_path.write_text("\n".join(lines) + "\n", encoding="ascii")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("source", type=Path, help="DSP56300 assembly source (kitbasher dialect: labels, .equ, .dc)")
    ap.add_argument("--out", type=Path, required=True, help="output CLDLOD .lod file")
    ap.add_argument("--org", type=lambda s: int(s, 0), default=0, help="program origin in DSP words (default 0)")
    ap.add_argument("--assembler", type=Path, default=DEFAULT_ASM56_EXE, help="path to asm56.exe (default: this repo's own vendored copy)")
    ap.add_argument("--equ", action="append", help="predefine a symbol as name=value (decimal or 0x-hex), repeatable")
    ap.add_argument("--symbols-out", type=Path, help="also write the resolved label map as JSON (name -> word offset)")
    args = ap.parse_args(argv)

    A = load_assembly_module()
    text = args.source.read_text(encoding="utf-8")
    symbols = parse_equ(args.equ)
    assembler = str(args.assembler.resolve()) if args.assembler else None

    words, labels = A.assemble(text, args.org, symbols, exe=assembler)

    write_lod(words, labels, args.out, org=args.org)
    print(f"{args.out}: {len(words)} program words, {len(labels)} labels")
    for name, value in sorted(labels.items(), key=lambda kv: kv[1]):
        print(f"  {name:<12} @ word {value - args.org}")

    if args.symbols_out:
        args.symbols_out.write_text(json.dumps(labels, indent=2, sort_keys=True), encoding="utf-8")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
