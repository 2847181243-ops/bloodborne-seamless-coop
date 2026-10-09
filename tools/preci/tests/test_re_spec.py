#!/usr/bin/env python3
"""Negative tests for the docs/re/ function-record checks.

Every rule must be able to FAIL, otherwise it is decoration. Cases:
  R-1  missing Confidence            -> error
  R-1  invalid Confidence value      -> error
  R-2  empty Evidence                -> error
  R-3  Confirmed with no evidence class -> error
  R-4  records without version block -> error
  happy path (well-formed record)    -> no error

Also asserts the honest boundary: a Confirmed record whose "evidence" is a plausible
but unverifiable sentence with a real class keyword is ACCEPTED. That is by design --
the checker validates format and self-consistency, not truth. Documenting that here
keeps anyone from over-claiming what the gate guarantees.
"""
import importlib.util
import os
import sys

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
D = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location(
    "crs", os.path.join(D, "..", "check_re_spec.py"))
crs = importlib.util.module_from_spec(spec)
spec.loader.exec_module(crs)

VER = """
游戏版本：1.09（CUSA03173）
平台：bbport Windows 原生版
Mod 版本：0.1.0
Signature：AA BB CC DD
"""

GOOD = VER + """
### BossDeathHandler

| 字段 | 值 |
|---|---|
| Function | `sub_1400A2B10` |
| Evidence | 反汇编 0x1400A2B10 起 40 字节；运行时断点命中 3 次 |
| Confidence | Confirmed |
"""

CASES = [
    ("合格记录（应通过）", GOOD, 0),
    ("缺 Confidence（R-1）", VER + """
### X

| 字段 | 值 |
|---|---|
| Evidence | 反汇编片段 |
""", 1),
    ("Confidence 取值非法（R-1）", VER + """
### X

| 字段 | 值 |
|---|---|
| Evidence | 反汇编片段 |
| Confidence | 大概是吧 |
""", 1),
    ("Evidence 为空（R-2）", VER + """
### X

| 字段 | 值 |
|---|---|
| Evidence |  |
| Confidence | Probable |
""", 1),
    ("Confirmed 但证据不含任何一类（R-3）", VER + """
### X

| 字段 | 值 |
|---|---|
| Evidence | 看起来是这样，逻辑上说得通 |
| Confidence | Confirmed |
""", 1),
    ("有记录但缺版本绑定（R-4）", """
### X

| 字段 | 值 |
|---|---|
| Evidence | 反汇编片段 |
| Confidence | Probable |
""", 1),
    ("Unknown 不需要证据类别（应通过）", VER + """
### X

| 字段 | 值 |
|---|---|
| Evidence | 尚无线索 |
| Confidence | Unknown |
""", 0),
    ("Hypothesis 不需要证据类别（应通过）", VER + """
### X

| 字段 | 值 |
|---|---|
| Evidence | 猜测依据：对照同类游戏的常见实现 |
| Confidence | Hypothesis |
""", 0),
    # HONEST BOUNDARY: a real evidence-class keyword makes it pass even though the
    # checker cannot verify the disassembly exists. Documented, not hidden.
    ("边界：有类别关键词即通过（检查器不验真伪）", VER + """
### X

| 字段 | 值 |
|---|---|
| Evidence | 反汇编片段（内容无法被机器核对） |
| Confidence | Confirmed |
""", 0),
]

passed = failed = 0
print("=" * 68)
print("  docs/re/ 函数记录校验（check_re_spec）")
print("=" * 68)
for name, text, expect_errors in CASES:
    errors, warns = [], []
    n = crs.check_re_record("docs/re/addresses.md", text, errors, warns)
    ok = (len(errors) == 0) if expect_errors == 0 else (len(errors) >= expect_errors)
    passed += ok
    failed += (not ok)
    mark = "OK " if ok else "BAD"
    print(f"  [{mark}] {name:44} errors={len(errors)} 期望{'0' if expect_errors==0 else '>0'}")
    if ok and errors:
        print(f"        → {errors[0][3][:100]}")
    if not ok:
        for e in errors[:2]:
            print(f"        {e[3][:140]}")

print(f"\n  {passed}/{len(CASES)} 符合预期")
sys.exit(0 if failed == 0 else 1)
