#!/usr/bin/env python3
"""Negative tests for the build-standards meta-check.

A check that only ever passes is worthless. Each case removes one thing the
standard depends on and asserts the check goes red.
"""
import importlib.util
import os
import sys
import _paths as P  # noqa: E402  (shared location-independent paths)

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
ROOT = P.ROOT
spec = importlib.util.spec_from_file_location(
    "cs", os.path.join(ROOT, "tools", "preci", "check_style.py"))
cs = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cs)

cmake = open(os.path.join(ROOT, "CMakeLists.txt"), "rb").read().decode("utf-8")
doc = open(os.path.join(ROOT, "docs", "standards", "build-standards.md"), "rb").read()


def run(tree):
    errors = []
    cs.check_build_standards(tree, errors)
    return errors


BASE = {"CMakeLists.txt": cmake.encode("utf-8"),
        "docs/standards/build-standards.md": doc}

CODE_W4WX = "target_compile_options(bbcoop_warnings INTERFACE /W4 /WX)"

CASES = [
    ("基线（完整）", BASE, 0),
    # NOTE: str.replace() hits the FIRST occurrence, and "target_compile_options(
    # bbcoop_warnings INTERFACE /W4 /WX)" also appears inside a comment earlier in
    # the file. An earlier version of this test therefore edited the comment and
    # left the real option in place -- the check correctly reported 0 errors and
    # the test looked like a failure. Anchor on the comment-free form instead.
    ("删掉 /WX（零警告的执行者）", {**BASE, "CMakeLists.txt":
        cmake.replace(CODE_W4WX, "target_compile_options(bbcoop_warnings INTERFACE /W4)").encode()}, 1),
    ("删掉 /W4", {**BASE, "CMakeLists.txt":
        cmake.replace(CODE_W4WX, "target_compile_options(bbcoop_warnings INTERFACE /WX)").encode()}, 1),
    ("删掉 /utf-8", {**BASE, "CMakeLists.txt": cmake.replace("/utf-8", "").encode()}, 1),
    ("删掉 /permissive-", {**BASE, "CMakeLists.txt": cmake.replace("/permissive-", "").encode()}, 1),
    ("删掉 /external:W0", {**BASE, "CMakeLists.txt": cmake.replace("/external:W0", "").encode()}, 1),
    ("删掉 /guard:cf", {**BASE, "CMakeLists.txt": cmake.replace("/guard:cf", "").encode()}, 1),
    ("删掉 bbcoop_warnings 目标", {**BASE, "CMakeLists.txt": cmake.replace("bbcoop_warnings", "X").encode()}, 1),
    ("删掉配置阶段自检 FATAL_ERROR", {**BASE, "CMakeLists.txt": cmake.replace("FATAL_ERROR", "WARNING").encode()}, 1),
    ("删掉标准文档", {"CMakeLists.txt": cmake.encode("utf-8")}, 1),
]

passed = failed = 0
for name, tree, expect_min in CASES:
    errs = run(tree)
    ok = (0 if expect_min == 0 else len(errs) >= expect_min)
    if expect_min == 0:
        ok = (len(errs) == 0)
    if ok:
        passed += 1
        print(f"  [OK ] {name:34} errors={len(errs)}")
    else:
        failed += 1
        print(f"  [BAD] {name:34} 期望>= {expect_min} 实际 {len(errs)}")
        for s, f, l, m in errs[:2]:
            print("         " + m[:150])

print(f"\n{passed}/{len(CASES)} 符合预期")
sys.exit(0 if failed == 0 else 1)
