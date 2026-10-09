#!/usr/bin/env python3
"""Negative tests for the audit-evidence check.

Each case plants a specific defect and asserts the gate goes red. The gate BODY is
executed directly (not a copy), with `files` (the changed-file list) and the report
path injected, so the check under test is the real one.
"""
import hashlib
import os
import re
import subprocess
import sys
import tempfile
import _paths as P  # noqa: E402  (shared location-independent paths)

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
ROOT = P.ROOT
WF = P.path(".github", "workflows", "pr-guard.yml")
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wfjobs import parse_jobs  # noqa: E402

BASH = P.BASH
jobs = parse_jobs(WF)
step = next(b for b in jobs["security-gate"] if "docs/audit" in b)

TARGET = "docs/standards/stop-loss-rules.md"
REAL = hashlib.sha256(open(os.path.join(ROOT, TARGET.replace("/", os.sep)), "rb").read()).hexdigest()
README_HASH = hashlib.sha256(open(os.path.join(ROOT, "README.md"), "rb").read()).hexdigest()
RF_REL = "docs/audit/audit_test_20260101.md"

HEAD_REPORT = """# 审计报告

- 审计对象：PR #999 / test
- 结论：PASS

## 双人独立签核

- C3-a
  - 结论：PASS
- C3-b
  - 结论：PASS
"""


def build_report(evidence):
    b = HEAD_REPORT
    if evidence:
        b += "\n## 证据\n\n" + "\n".join(evidence) + "\n"
    return b


CASES = [
    ("无证据行", [], 1),
    ("证据缺 sha256", [f"- 证据：{TARGET} | 查了注释规范"], 1),
    ("哈希是编的", [f"- 证据：{TARGET} | sha256={'0' * 64} | 查了"], 1),
    ("证据文件不存在", [f"- 证据：docs/nope.md | sha256={REAL} | 查了"], 1),
    ("证据不在本 PR 改动内", [f"- 证据：README.md | sha256={README_HASH} | 查了"], 1),
    ("正确哈希（应通过）", [f"- 证据：{TARGET} | sha256={REAL} | 查了止损规则"], 0),
]

scratch = os.path.join(tempfile.gettempdir(), "audit-ev2")
os.makedirs(scratch, exist_ok=True)
rp = os.path.join(scratch, "report.md")
rp_fwd = rp.replace("\\", "/")

passed = failed = 0
for name, ev, expect_fail in CASES:
    open(rp, "w", encoding="utf-8", newline="\n").write(build_report(ev))
    body = step.replace(
        'mapfile -t files < <(git diff --name-only "origin/$BASE_REF...HEAD")',
        # seed the changed-file list: a sensitive path so the gate triggers,
        # plus the evidence target so the "in this PR" check can pass
        f'mapfile -t files <<< "mod/security/x.cpp\n{TARGET}\n{RF_REL}"')
    body = re.sub(r"mapfile -t reports < <\([^\n]*\)",
                  f'mapfile -t reports <<< "{rp_fwd}"', body)
    sh = os.path.join(scratch, "run.sh")
    open(sh, "w", encoding="utf-8", newline="\n").write(
        "export LC_ALL=C.UTF-8\nexport BASE_REF=main\nexport LABELS=audit:passed\n"
        "export PRNUMBER=999\nexport PRBODY=dummy\n" + body)
    r = subprocess.run([BASH, sh], capture_output=True, text=True,
                       encoding="utf-8", errors="replace", cwd=ROOT)
    out = (r.stdout or "") + (r.stderr or "")
    got_fail = 1 if r.returncode != 0 else 0
    ok = got_fail == expect_fail
    passed += ok
    failed += (not ok)
    print(f"  [{'OK ' if ok else 'BAD'}] {name:22} expect_fail={expect_fail} got={got_fail}")
    if not ok:
        for l in out.splitlines():
            if "::error" in l:
                print("        " + l.strip()[:150])
    elif expect_fail:
        err = [l for l in out.splitlines() if "::error" in l]
        if err:
            print(f"        → {err[0].split('::')[-1].strip()[:110]}")

print(f"\n{passed}/{len(CASES)} 符合预期")
sys.exit(0 if failed == 0 else 1)
