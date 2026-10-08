#!/usr/bin/env python3
"""Run every pre-CI regression suite and summarise.

A gate that is never re-run is decoration. This runner is what makes the suites in
this directory actually re-run, locally and in CI.

Usage:
    python tools/preci/tests/run_all.py            # all suites
    python tools/preci/tests/run_all.py preci       # only suites whose name matches

Exit codes follow the same convention as preci.ps1:
    0 = everything passed
    1 = at least one suite failed
    2 = passed, but something was SKIPPED (e.g. PyYAML or bash missing)
"""
import importlib
import os
import sys
import traceback

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _paths as P  # noqa: E402

# Suites are (module_name, human label, needs).
# `needs` names a capability the suite cannot run without; when absent the suite is
# SKIPPED and counted as unverified rather than silently treated as passing.
SUITES = [
    ("test_preci_negative", "本地预检 · 负例矩阵", "powershell"),
    ("test_level_fix", "验证等级判定", "powershell"),
    ("test_build_std", "编译标准防削弱", None),
    ("test_pg_strict", "PR 描述严格矩阵", None),
    ("test_bp2", "分支与写入范围矩阵", None),
    ("test_security_gate", "审计门禁矩阵", "bash"),
    ("test_audit_evidence", "审计证据核对", "bash"),
    ("wf_syntax", "workflow 语法", None),
    ("validate_wf_yaml", "workflow 结构", "yaml"),
]


def have(cap):
    if cap is None:
        return True, ""
    if cap == "bash":
        ok = os.path.isfile(P.BASH)
        return ok, "" if ok else f"找不到 bash（试过 {P.BASH}）"
    if cap == "powershell":
        host = P._find_powershell()
        return bool(host), "" if host else "找不到 PowerShell（powershell.exe 与 pwsh 都没有）"
    if cap == "yaml":
        try:
            import yaml  # noqa: F401
            return True, ""
        except ImportError:
            return False, "没装 PyYAML（validate_workflows.py 会返回 SKIP）"
    return True, ""


def main():
    want = sys.argv[1] if len(sys.argv) > 1 else None
    print("=" * 68)
    print("  pre-CI 回归矩阵")
    print(f"  仓库：{P.ROOT}")
    print("=" * 68)

    results = []
    for mod_name, label, need in SUITES:
        if want and want not in mod_name and want not in label:
            continue
        ok, why = have(need)
        print()
        print(f"── {label}  ({mod_name}) " + "─" * max(0, 40 - len(label)))
        if not ok:
            print(f"  [跳过] {why}")
            results.append((label, "skip", why))
            continue
        try:
            mod = importlib.import_module(mod_name)
            rc = 0
            if hasattr(mod, "main"):
                rc = mod.main() or 0
            # modules that run at import time and sys.exit(): treat import success
            # plus absence of SystemExit as pass
            results.append((label, "pass" if rc == 0 else "fail", ""))
        except SystemExit as e:
            code = e.code if isinstance(e.code, int) else (0 if e.code is None else 1)
            results.append((label, "pass" if code == 0 else "fail", ""))
        except Exception:
            tb = traceback.format_exc()
            print("  [异常] " + tb.splitlines()[-1])
            results.append((label, "fail", tb))

    print()
    print("=" * 68)
    print("  汇总")
    print("=" * 68)
    npass = sum(1 for _, s, _ in results if s == "pass")
    nfail = sum(1 for _, s, _ in results if s == "fail")
    nskip = sum(1 for _, s, _ in results if s == "skip")
    for label, st, why in results:
        mark = {"pass": "通过", "fail": "失败", "skip": "跳过"}[st]
        extra = f"   ({why})" if why else ""
        print(f"  [{mark}] {label}{extra}")
    print()
    print(f"  通过 {npass}    失败 {nfail}    跳过 {nskip}")

    if nfail:
        return 1
    if nskip:
        # Same rule as preci.ps1: "did not check" is not "checked and fine".
        print("  有未验证项 —— 退出码 2（不等于通过）")
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
