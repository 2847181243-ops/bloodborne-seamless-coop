#!/usr/bin/env python3
"""Syntax-check every `run:` body in every workflow with `bash -n`."""
import glob
import os
import tempfile
import re
import subprocess
import sys
import _paths as P  # noqa: E402  (shared location-independent paths)

BASH = P.BASH
OUT = os.path.join(tempfile.gettempdir(), "bbcoop-wfcheck")
os.makedirs(OUT, exist_ok=True)

bad = 0
total = 0
for wf in sorted(glob.glob(P.path(".github", "workflows", "*.yml"))):
    lines = open(wf, encoding="utf-8").read().split("\n")
    bodies, cur, pending = [], None, False
    for raw in lines:
        ln = raw.rstrip("\r")
        if pending:
            pending = False
            cur = []
            continue
        if re.match(r"^\s*run: \|\s*$", ln):
            if cur is not None:
                bodies.append("\n".join(cur))
            cur, pending = None, True
            continue
        if cur is not None:
            if ln.strip() == "":
                cur.append("")
                continue
            if re.match(r"^\s{10,}", ln):
                cur.append(ln[10:])
                continue
            bodies.append("\n".join(cur))
            cur = None
    if cur is not None:
        bodies.append("\n".join(cur))

    for i, b in enumerate(bodies, 1):
        total += 1
        p = os.path.join(OUT, f"{os.path.basename(wf)}.{i}.sh")
        open(p, "w", encoding="utf-8", newline="\n").write(b + "\n")
        r = subprocess.run([BASH, "-n", p], capture_output=True, text=True,
                           encoding="utf-8", errors="replace")
        if r.returncode != 0:
            bad += 1
            print(f"  SYNTAX FAIL {os.path.basename(wf)} body {i}: {(r.stderr or '').strip()[:200]}")
    print(f"  {os.path.basename(wf):26} {len(bodies)} run-body(ies)")

print(f"  total run-bodies: {total}   syntax failures: {bad}")
raise SystemExit(1 if bad else 0)
