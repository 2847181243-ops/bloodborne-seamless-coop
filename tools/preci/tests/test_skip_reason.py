#!/usr/bin/env python3
"""Negative matrix for the -SkipReason mechanism.

The point of the mechanism is that a SKIP must not be silently acceptable. So the
cases that matter are the ones that must be REJECTED:

  no reason              -> exit 2  (skip still counts as unverified)
  reason too short       -> exit 3  (says nothing)
  platitude ("不适用")    -> exit 3  (no information)
  substantive reason     -> exit 0  AND a trace file that records it

Without this suite the mechanism could be deleted (or its validation weakened) and
nothing would notice -- which is exactly the failure mode this repo has hit before.
"""
import json
import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _paths as P  # noqa: E402

if not sys.platform.startswith("win"):
    print("本套件需要 Windows（preci.ps1 面向 Windows PowerShell 5.1）—— 跳过")
    sys.exit(0)

PRECI = P.path("tools", "preci", "preci.ps1")
TRACE = P.path("tools", "preci", "skip-trace.json")

CASES = [
    ("不给理由（跳过仍算未验证）", [], 2),
    ("理由太短", ["-SkipReason", "太短"], 3),
    ("套话：不适用", ["-SkipReason", "不适用"], 3),
    ("套话：n/a", ["-SkipReason", "n/a"], 3),
    ("套话：以后再说", ["-SkipReason", "以后"], 3),
    ("套话：todo", ["-SkipReason", "TODO"], 3),
    ("实质理由（应通过并留痕）",
     ["-SkipReason",
      "本机缺少 cmake 与 MSVC，构建检查无法在本地复现；CI 的 Windows runner 会真编译，"
      "该跳过已写进 PR 说明，等本地装好工具后不再跳过。"], 0),
]

passed = failed = 0
print("=" * 68)
print("  跳过留痕机制（-SkipReason）")
print("=" * 68)

for name, extra, expect in CASES:
    if os.path.exists(TRACE):
        os.remove(TRACE)
    rc, out = P.run_ps(["-File", PRECI, "-Stage", "1"] + extra, timeout=600)
    if rc is None:
        print(f"  [跳过] {out}")
        sys.exit(0)
    ok = rc == expect
    # the substantive case must actually leave a trace with the reason in it
    trace_ok = True
    if expect == 0:
        trace_ok = os.path.isfile(TRACE)
        if trace_ok:
            try:
                d = json.load(open(TRACE, encoding="utf-8"))
                trace_ok = bool(d.get("reason")) and d.get("skipped", 0) >= 0
                if not d.get("reason"):
                    trace_ok = False
            except Exception as e:
                trace_ok = False
                print(f"        留痕解析失败: {e}")
    ok = ok and trace_ok
    passed += ok
    failed += (not ok)
    print(f"  [{'OK ' if ok else 'BAD'}] {name:30} exit={rc} (期望 {expect})"
          + (f" 留痕={trace_ok}" if expect == 0 else ""))
    if not ok:
        for line in (out or "").splitlines():
            if "跳过理由" in line or "太短" in line or "套话" in line or "未验证" in line:
                print("        " + line.strip()[:150])

if os.path.exists(TRACE):
    os.remove(TRACE)

print(f"\n  {passed}/{len(CASES)} 符合预期")
sys.exit(0 if failed == 0 else 1)
