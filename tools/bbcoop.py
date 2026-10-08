#!/usr/bin/env python3
"""bbcoop —— 本仓库所有工程操作的单一入口。

WHY A SINGLE ENTRY POINT
------------------------
Before this, "do the checks" meant remembering a pile of paths:

    powershell -File tools/preci/preci.ps1 -Stage 2 -Pr 18
    python tools/preci/tests/run_all.py
    python tools/preci/validate_workflows.py

Every one of those is easy to run slightly wrong, and a check nobody runs
correctly is a check that does not exist. One entry point means:

  * contributors learn one command, not six;
  * the set of checks can grow without anyone's habits breaking;
  * CI and humans run the SAME commands, so a local pass means something.

Adapted from the `ohc-run.py` pattern in build_in_harmonyos (see
docs/standards/参考-build_in_harmonyos-可复用经验.md).

USAGE
-----
    python tools/bbcoop.py check              # 本地预检（阶段 1）
    python tools/bbcoop.py check --stage 2 --pr 18
    python tools/bbcoop.py test               # 全部回归矩阵
    python tools/bbcoop.py test security      # 只跑名字匹配的
    python tools/bbcoop.py verify             # check + test（提交前跑这个）
    python tools/bbcoop.py doctor             # 环境与工具链现状
    python tools/bbcoop.py status             # 当前分支 / 改动 / 归属

EXIT CODES (same convention everywhere in this repo)
    0 = 全部通过
    1 = 有失败
    2 = 有未验证项（跳过），或请求的阶段没能跑
    3 = 无法启动（参数 / 环境问题）
"""
import argparse
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
TESTS = os.path.join(HERE, "preci", "tests")
PY = sys.executable

OK, FAIL, UNVERIFIED, USAGE = 0, 1, 2, 3


def hr(title):
    print()
    print("=" * 68)
    print(f"  {title}")
    print("=" * 68)


def sh(args, cwd=None, capture=True):
    r = subprocess.run(args, cwd=cwd or ROOT, capture_output=capture,
                       text=True, encoding="utf-8", errors="replace")
    return r.returncode, (r.stdout or "") + (r.stderr or "")


def combined(rcs):
    """Fold several exit codes into one, keeping the worst meaning."""
    if any(rc == FAIL for rc in rcs):
        return FAIL
    if any(rc == UNVERIFIED for rc in rcs):
        return UNVERIFIED
    if any(rc == USAGE for rc in rcs):
        return USAGE
    return OK


# ── commands ────────────────────────────────────────────────────────────────

def cmd_check(args):
    """Local pre-CI (the two-stage gate)."""
    hr("本地预检 pre-CI" + (f" · 阶段 {args.stage}" if args.stage else ""))
    cmd = ["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass",
           "-File", os.path.join(HERE, "preci", "preci.ps1")]
    if args.stage:
        cmd += ["-Stage", str(args.stage)]
    if args.pr:
        cmd += ["-Pr", str(args.pr)]
    if args.pr_body:
        cmd += ["-PrBody", args.pr_body]
    if args.strict:
        cmd += ["-Strict"]
    if args.allow_skip:
        cmd += ["-AllowSkip"]
    rc, out = sh(cmd)
    print(out.rstrip())
    return rc


def cmd_test(args):
    """The regression matrices."""
    hr("回归矩阵")
    cmd = [PY, os.path.join(TESTS, "run_all.py")]
    if args.filter:
        cmd.append(args.filter)
    env = dict(os.environ)
    # PR_GUARD_REF makes the strict matrix read the working-tree workflow. Without
    # it the suite reads the committed one, which is correct for a clean tree but
    # hides uncommitted edits -- so only set it when asked.
    if args.ref:
        env["PR_GUARD_REF"] = args.ref
    r = subprocess.run(cmd, cwd=ROOT, env=env, text=True, encoding="utf-8",
                       errors="replace")
    return r.returncode


def cmd_verify(args):
    """check then test -- what to run before asking for a review."""
    hr("提交前验证：先本地预检，再跑回归矩阵")
    rc1 = cmd_check(argparse.Namespace(
        stage=1, pr=None, pr_body=None, strict=False, allow_skip=False))
    rc2 = cmd_test(argparse.Namespace(filter=None, ref=None))
    rc = combined([rc1, rc2])
    hr("结论")
    print(f"  预检 exit={rc1}    回归 exit={rc2}    → 合计 {rc}")
    if rc == OK:
        print("  可以提交、可以请求合并。")
    elif rc == FAIL:
        print("  有失败项。修掉再提交，不要先推上去再看 CI。")
    elif rc == UNVERIFIED:
        print("  没有失败，但有未验证项 —— 这**不等于通过**。")
        print("  要么补上工具（见 bbcoop.py doctor），要么在 PR 里说明为什么未验证。")
    return rc


def cmd_doctor(_args):
    """Environment and toolchain reality check."""
    hr("环境与工具链")
    checks = []

    def probe(label, ok, detail="", optional=False):
        checks.append((label, ok, detail, optional))
        mark = "有" if ok else ("可选·无" if optional else "缺")
        print(f"  [{mark:>5}] {label:<26} {detail}")

    # git
    rc, out = sh(["git", "--version"])
    probe("git", rc == 0, out.strip())

    # the repo itself
    rc, out = sh(["git", "rev-parse", "--abbrev-ref", "HEAD"])
    probe("当前分支", rc == 0, out.strip())

    # python bits
    for mod in ("yaml",):
        try:
            __import__(mod)
            probe(f"python: {mod}", True, "已装")
        except ImportError:
            probe(f"python: {mod}", False,
                  "未装 —— workflow 结构检查会跳过（可 pip install pyyaml）",
                  optional=True)

    # bash (needed by the gate matrices)
    sys.path.insert(0, TESTS)
    try:
        import _paths as P
        bash = P.BASH
    except Exception:
        bash = "bash"
    probe("bash (Git Bash)", os.path.isfile(bash), bash)

    # local toolchain (optional, installed by tools/setup-toolchain.ps1)
    tl = os.path.join(HERE, "toolchain", "pylibs")
    for name, rel in (("cmake", r"cmake\data\bin\cmake.exe"),
                      ("zig (本地编译器)", r"ziglang\zig.exe"),
                      ("clang-format", r"clang_format\data\bin\clang-format.exe"),
                      ("clang-tidy", r"clang_tidy\data\bin\clang-tidy.exe")):
        p = os.path.join(tl, rel)
        probe(name, os.path.isfile(p),
              p if os.path.isfile(p) else "跑 tools/setup-toolchain.ps1 装（不需要管理员）",
              optional=True)
    rc, out = sh(["where", "ninja"])
    probe("ninja", rc == 0, out.strip().splitlines()[0] if out.strip() else "未找到",
          optional=True)

    hr("MSVC（CI 的权威编译器）")
    print("  本机：没有。安装需要管理员权限，pip 装不了。")
    print("  说明与步骤见 docs/standards/build-standards.md 第 1 节。")
    print("  本地用 zig 做冒烟测试就够，权威构建在 CI。")

    hr("结论")
    missing = [c for c in checks if not c[1] and not c[3]]
    optional_missing = [c for c in checks if not c[1] and c[3]]
    if missing:
        print("  必须项缺失：")
        for label, _, detail, _ in missing:
            print(f"    - {label}: {detail}")
        return FAIL
    if optional_missing:
        print(f"  可选工具缺 {len(optional_missing)} 项 —— 相关检查会报「跳过」而不是通过。")
        for label, _, _, _ in optional_missing:
            print(f"    - {label}")
        return UNVERIFIED
    print("  全部就绪。")
    return OK


def cmd_status(_args):
    """Where am I, what did I change, and is any of it out of my scope?"""
    hr("当前状态")
    rc, branch = sh(["git", "rev-parse", "--abbrev-ref", "HEAD"])
    print(f"  分支：{branch.strip()}")
    rc, head = sh(["git", "log", "-1", "--format=%h %s"])
    print(f"  HEAD：{head.strip()[:80]}")

    rc, base = sh(["git", "rev-parse", "--short", "origin/main"])
    print(f"  origin/main：{base.strip()}")

    rc, out = sh(["git", "-c", "core.quotepath=false", "diff", "--stat", "origin/main..HEAD"])
    print()
    print("  与 main 的真实差异：")
    if out.strip():
        for line in out.strip().splitlines():
            print("    " + line.strip())
    else:
        print("    （无）")

    rc, out = sh(["git", "-c", "core.quotepath=false", "status", "--porcelain"])
    print()
    print("  未提交改动：")
    if out.strip():
        for line in out.strip().splitlines():
            print("    " + line)
    else:
        print("    （无）")

    hr("归属自查")
    print("  写入范围来自 .github/workflows/branch-policy.yml。")
    print("  跑 `python tools/bbcoop.py check` 会指出越界文件。")
    return OK


# ── wiring ──────────────────────────────────────────────────────────────────

def main():
    ap = argparse.ArgumentParser(
        prog="bbcoop",
        description="本仓库所有工程操作的单一入口（预检 / 测试 / 环境 / 状态）",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="退出码：0 通过 / 1 失败 / 2 有未验证项 / 3 无法启动")
    sub = ap.add_subparsers(dest="cmd")

    c = sub.add_parser("check", help="本地预检（默认阶段 1）")
    c.add_argument("--stage", type=int, choices=[1, 2])
    c.add_argument("--pr", type=int, help="PR 编号（阶段 2 用）")
    c.add_argument("--pr-body", help="PR 描述文件路径（阶段 2 用）")
    c.add_argument("--strict", action="store_true", help="把跳过当失败")
    c.add_argument("--allow-skip", action="store_true", help="把跳过当通过（默认不这样）")
    c.set_defaults(func=cmd_check)

    t = sub.add_parser("test", help="跑回归矩阵")
    t.add_argument("filter", nargs="?", help="只跑名字匹配的套件")
    t.add_argument("--ref", help="用工作区的 workflow 而不是已提交的（传给 PR_GUARD_REF）")
    t.set_defaults(func=cmd_test)

    v = sub.add_parser("verify", help="check + test（提交前跑这个）")
    v.set_defaults(func=cmd_verify)

    d = sub.add_parser("doctor", help="环境与工具链现状")
    d.set_defaults(func=cmd_doctor)

    s = sub.add_parser("status", help="分支 / 改动 / 归属")
    s.set_defaults(func=cmd_status)

    args = ap.parse_args()
    if not getattr(args, "cmd", None):
        ap.print_help()
        return USAGE
    try:
        return args.func(args) or OK
    except KeyboardInterrupt:
        print("\n  被中断。")
        return USAGE


if __name__ == "__main__":
    sys.exit(main())
