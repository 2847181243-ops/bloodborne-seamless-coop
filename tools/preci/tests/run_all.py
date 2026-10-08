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
    2 = passed, but something was SKIPPED that SHOULD have run
        (e.g. PyYAML or bash missing). Skills that cannot run in this environment
        by design are reported as 跳过 and do NOT make the run unverified.
"""
import importlib
import os
import sys
import traceback

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _paths as P  # noqa: E402

# (module, label, needs, optional_needs)
#
# `needs`        -- something that SHOULD exist here; if missing the run is
#                   "unverified" (exit 2) because a check we expected did not run.
# `optional_needs` -- credentials/tools that legitimately may not exist in this
#                   environment. A suite that cannot run for this reason is
#                   reported as 跳过 and does not by itself make the run unverified.
#
# Why the distinction matters: treating "no GitHub token on a CI runner" the same as
# "PyYAML failed to install" would make CI permanently red for a non-problem, and
# people would then learn to ignore red CI.
SUITES = [
    ("test_preci_negative", "本地预检 · 负例矩阵", "powershell", ()),
    ("test_build_std", "编译标准防削弱", None, ()),
    ("test_pg_strict", "PR 描述严格矩阵", None, ()),
    ("test_bp2", "分支与写入范围矩阵", None, ()),
    ("test_security_gate", "审计门禁矩阵", "bash", ()),
    ("test_audit_evidence", "审计证据核对", "bash", ()),
    ("wf_syntax", "workflow 语法", None, ()),
    ("validate_wf_yaml", "workflow 结构", "yaml", ()),
    # Drives preci.ps1 AND calls the GitHub API, so it needs a local token.
    # CI has no such token; the suite is expected to be skipped there.
    ("test_level_fix", "验证等级判定", "powershell", ("github_token",)),
]

TOKEN_PATH = os.path.join(os.path.expanduser("~"), ".config", "dsh", "token.txt")


def have(cap):
    """Return (ok, reason)."""
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
            return False, "没装 PyYAML（workflow 结构检查会返回 SKIP）"
    if cap == "github_token":
        ok = os.path.isfile(TOKEN_PATH)
        return ok, "" if ok else f"没有本地 GitHub token（{TOKEN_PATH}）"
    return True, ""


def main():
    want = sys.argv[1] if len(sys.argv) > 1 else None
    print("=" * 68)
    print("  pre-CI 回归矩阵")
    print(f"  仓库：{P.ROOT}")
    print(f"  平台：{sys.platform}    PowerShell：{P._find_powershell() or '无'}")
    print("=" * 68)

    results = []
    for mod_name, label, need, opt_needs in SUITES:
        if want and want not in mod_name and want not in label:
            continue
        print()
        print(f"── {label}  ({mod_name}) " + "─" * max(0, 40 - len(label)))

        ok, why = have(need)
        if not ok:
            print(f"  [跳过·未验证] {why}")
            results.append((label, "skip_unverified", why))
            continue

        blocked = ""
        for opt in opt_needs:
            ook, owhy = have(opt)
            if not ook:
                blocked = owhy
                break
        if blocked:
            print(f"  [跳过·环境不具备] {blocked}")
            results.append((label, "skip_env", blocked))
            continue

        try:
            mod = importlib.import_module(mod_name)
            rc = 0
            if hasattr(mod, "main"):
                rc = mod.main() or 0
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
    kinds = {"pass": 0, "fail": 0, "skip_unverified": 0, "skip_env": 0}
    for label, st, why in results:
        kinds[st] = kinds.get(st, 0) + 1
        mark = {"pass": "通过", "fail": "失败",
                "skip_unverified": "跳过·未验证", "skip_env": "跳过·环境不具备"}[st]
        extra = f"   ({why})" if why else ""
        print(f"  [{mark}] {label}{extra}")
    print()
    print(f"  通过 {kinds['pass']}    失败 {kinds['fail']}    "
          f"跳过·未验证 {kinds['skip_unverified']}    跳过·环境不具备 {kinds['skip_env']}")

    if kinds["fail"]:
        return 1
    if kinds["skip_unverified"]:
        print("  有期望运行但没能运行的检查 —— 退出码 2（不等于通过）")
        return 2
    if kinds["skip_env"]:
        print("  有套件因本环境不具备条件而跳过（如 CI 上没有 GitHub token）—— 不影响结论。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
