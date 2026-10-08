#!/usr/bin/env python3
"""Validate GitHub workflow files: YAML parse + structural invariants.

Invoked by tools/preci/preci.ps1 (and usable standalone / in CI). Exit 0 = clean,
1 = problems (printed with a leading "!").

Why this exists: `bash -n` on the run bodies proves the shell inside each block is
syntactically valid, but says nothing about the YAML structure. The specific
invariants below each map to a *documented* GitHub behaviour, so violating them has
predictable, confusing consequences:

  * A workflow filtered by `paths:` that backs a REQUIRED check leaves that check
    in "Pending" forever and blocks merging.
    https://docs.github.com/en/pull-requests/how-tos/merge-and-close-pull-requests/troubleshooting-required-status-checks
  * A job that depends on a failed job "is skipped and may not block merging" --
    so an aggregate gate MUST use `if: always()`.

Requires PyYAML. If it is unavailable the caller should report SKIP, never PASS.
"""
import os
import re
import sys

sys.stdout.reconfigure(encoding="utf-8", errors="replace")

try:
    import yaml
except ImportError:
    # Exit 3 + a machine-readable prefix: the caller must report SKIP (a missing
    # dependency is UNVERIFIED, not a failure). Anything else would make a machine
    # without PyYAML look like a broken repo.
    print("SKIP: PyYAML not available -- cannot validate YAML structure")
    sys.exit(3)

if not os.path.isdir(os.path.join(os.path.dirname(os.path.abspath(__file__)))):
    print("SKIP: script directory missing")
    sys.exit(3)

# Workflows that back a required status check. Path filtering on these is a defect.
REQUIRED_BACKING = {
    "docs-structure.yml", "branch-policy.yml", "pr-guard.yml",
    "repo-integrity.yml", "build.yml",
}


def norm_triggers(doc):
    # PyYAML implements YAML 1.1, where the bare key `on` parses as boolean True.
    # Reading only d['on'] silently yields None and makes triggers look empty.
    on = doc.get("on", doc.get(True))
    if isinstance(on, dict):
        return list(on.keys()), on
    if isinstance(on, str):
        return [on], {}
    if isinstance(on, list):
        return on, {}
    return [], {}


def main(argv):
    wf_dir = argv[1] if len(argv) > 1 else os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "..", "..", ".github", "workflows")
    gates_yml = argv[2] if len(argv) > 2 else os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "gates.yml")

    if not os.path.isdir(wf_dir):
        print(f"workflow dir not found: {wf_dir}")
        return 1

    problems = []
    notes = []
    all_names = set()

    for fn in sorted(os.listdir(wf_dir)):
        if not fn.endswith((".yml", ".yaml")):
            continue
        path = os.path.join(wf_dir, fn)
        try:
            doc = yaml.safe_load(open(path, encoding="utf-8"))
        except yaml.YAMLError as e:
            problems.append(f"! {fn}: YAML parse failed: {str(e)[:160]}")
            continue
        if not isinstance(doc, dict):
            problems.append(f"! {fn}: top level is not a mapping")
            continue

        jobs = doc.get("jobs") or {}
        if not isinstance(jobs, dict) or not jobs:
            problems.append(f"! {fn}: no jobs")

        for jn, j in jobs.items():
            if not isinstance(j, dict):
                problems.append(f"! {fn}: job '{jn}' is not a mapping")
                continue
            if "runs-on" not in j and "uses" not in j:
                problems.append(f"! {fn}: job '{jn}' has neither runs-on nor uses")
            all_names.add(j.get("name", jn))

            # needs may be a scalar; iterating a string splits it into characters
            nd = j.get("needs")
            deps = [nd] if isinstance(nd, str) else (nd if isinstance(nd, list) else [])
            for dd in deps:
                if dd not in jobs:
                    problems.append(f"! {fn}: job '{jn}' needs unknown job '{dd}'")

            if jn == "gate" and str(j.get("if", "")).strip() != "always()":
                problems.append(
                    f"! {fn}: aggregate 'gate' job must use `if: always()` -- the docs "
                    f"state a job depending on a failed job may not block merging")

        trigs, on_conf = norm_triggers(doc)
        for t in ("pull_request", "push"):
            conf = on_conf.get(t) if isinstance(on_conf, dict) else None
            if isinstance(conf, dict) and ("paths" in conf or "paths-ignore" in conf):
                if fn in REQUIRED_BACKING:
                    problems.append(
                        f"! {fn}: '{t}' uses paths/paths-ignore while backing a required "
                        f"check -- a filtered workflow's checks stay Pending and block merging")
                else:
                    notes.append(f"{fn}: '{t}' path-filtered but backs no required check -> fine")

        print(f"  [OK  ] {fn:22} jobs={len(jobs)} triggers={trigs}")

    # Cross-check: the contexts gates.yml claims are required must exist as job names
    if os.path.isfile(gates_yml):
        text = open(gates_yml, encoding="utf-8").read()
        if "required_status_checks:" in text:
            block = text.split("required_status_checks:")[1].split("never_require:")[0]
            claimed = re.findall(r"^  - (.+)$", block, re.M)
            for c in claimed:
                c = c.strip()
                if c not in all_names:
                    problems.append(
                        f"! gates.yml requires context '{c}' but no workflow job has that name")

    if notes:
        print("\nnotes (not problems):")
        for n in notes:
            print("  -", n)
    print(f"\nproblems: {len(problems)}")
    for p in problems:
        print("  " + p)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
