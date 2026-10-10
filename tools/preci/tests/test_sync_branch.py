#!/usr/bin/env python3
"""Negative matrix for the `sync/*` branch type.

WHY THIS EXISTS
---------------
`main -> develop` syncs copy main's state onto develop and modify NOBODY's files, so
the ownership/scope check is not applicable. But a sync PR looks like any other PR:
many changed files across many directories. Measured on the first real one, 2 of 18
files fell outside the `[lead]` scope.

The tempting shortcut is to name the branch so it maps to a scope that happens to
pass. That is gaming the check -- picking a label that fits instead of fixing the
policy -- and AI_POLICY.md §2.3 forbids it.

So `sync/*` gets its own type with a PROVABLE precondition: the branch tree must be
byte-identical to `origin/main`. Identical -> nothing is owned, exemption applies.
Different -> FAIL, do not fall through to the scope check.

This suite proves BOTH halves, because a safety exemption that is only tested on its
happy path is not a safety exemption:

    tree == main   -> passes, and says why
    tree != main   -> FAILS (this is the property that stops sync/* becoming a bypass)

It also checks the branch is rejected when `origin/main` is unavailable -- "cannot
verify" must not be treated as "verified".
"""
import os
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _paths as P  # noqa: E402

ROOT = P.ROOT
PRECI = P.path("tools", "preci", "preci.ps1")
GIT = P.GIT

passed = failed = 0


def report(label, ok, detail=""):
    global passed, failed
    passed += ok
    failed += (not ok)
    print(f"  [{'OK ' if ok else 'BAD'}] {label:44} {detail}")


def git(*args, cwd):
    return subprocess.run([GIT, "-C", cwd] + list(args), capture_output=True,
                          text=True, encoding="utf-8", errors="replace")


print("=" * 68)
print("  sync/* 分支：豁免的前提必须被验证")
print("=" * 68)

work = tempfile.mkdtemp(prefix="syncbr-")
git("init", "-q", "-b", "main", cwd=work)
git("config", "user.email", "t@example.com", cwd=work)
git("config", "user.name", "t", cwd=work)

# minimal tree so preci can start
os.makedirs(os.path.join(work, ".github", "workflows"), exist_ok=True)
open(os.path.join(work, "README.md"), "w", encoding="utf-8").write("# x\n")
open(os.path.join(work, ".github", "workflows", "w.yml"), "w",
     encoding="utf-8").write("name: w\non: [push]\njobs:\n  a:\n    runs-on: ubuntu-latest\n"
                             "    steps:\n      - run: echo hi\n")
git("add", "-A", cwd=work)
git("commit", "-q", "-m", "init", cwd=work)

# a fake origin/main that the sync branch is compared against
bare = tempfile.mkdtemp(prefix="syncbr-origin-")
git("init", "-q", "--bare", "-b", "main", cwd=bare)
git("remote", "add", "origin", bare, cwd=work)
git("push", "-q", "-u", "origin", "main", cwd=work)
git("fetch", "-q", "origin", cwd=work)


def run_preci(cwd):
    """Run preci.ps1 against the TEMP working tree.

    Two traps hit here in a row:
      1. P.run_ps() has no cwd parameter -- it always runs in the real repo, so all
         three cases were silently testing the real branch.
      2. Passing cwd= to subprocess is NOT enough either: preci.ps1 derives its repo
         root from $PSScriptRoot, not from the current directory.
    The parameter that exists for exactly this is `-Repo` (its own comment says it is
    there so the negative-case tests exercise the real checks instead of a
    re-implementation drifting).
    """
    ps = P._find_powershell()
    r = subprocess.run([ps, "-NoProfile", "-ExecutionPolicy", "Bypass",
                        "-File", PRECI, "-Repo", cwd,
                        "-Stage", "1", "-AllowSkip"],
                       cwd=cwd, capture_output=True, text=True,
                       encoding="utf-8", errors="replace", timeout=900)
    return r.returncode, ((r.stdout or "") + (r.stderr or ""))


def show(out, label):
    """Print preci's branch/scope lines so a failure explains itself.

    Earlier this suite reported 0/3 with no output at all, which told me nothing
    about which of the three cases broke or why.
    """
    print(f"        ── {label} 的关键输出 ──")
    shown = 0
    for line in (out or "").splitlines():
        s = line.strip()
        if not s:
            continue
        if ("分支" in s or "同步" in s or "范围" in s or "main" in s
                or "[失败]" in s or "[通过]" in s or "::error" in s):
            print("        " + s[:165])
            shown += 1
            if shown >= 7:
                break
    if shown == 0:
        print("        （没有可识别的行，尾部 5 行：）")
        for line in (out or "").splitlines()[-5:]:
            if line.strip():
                print("        " + line.strip()[:165])


# case 1: branch identical to main -> exemption applies
git("checkout", "-q", "-b", "sync/develop", cwd=work)
rc, out = run_preci(work)
said_ok = "sync" in out and ("完全一致" in out or "不适用" in out)
report("内容与 main 一致（应通过并说明原因）",
       said_ok, f"提及豁免={said_ok}")
if not said_ok:
    show(out, "case 1")

# case 2: branch carries an authored change -> MUST fail
with open(os.path.join(work, "sneaky.md"), "w", encoding="utf-8") as fh:
    fh.write("this should not be allowed on a sync branch\n")
git("add", "-A", cwd=work)
git("commit", "-q", "-m", "sneaky change on a sync branch", cwd=work)
rc, out = run_preci(work)
blocked = ("不一致" in out) or ("不得携带" in out)
report("内容与 main 不一致（应失败）",
       blocked, f"报错={blocked}")
if not blocked:
    show(out, "case 2")

# case 3: no origin/main at all -> must not pass
git("remote", "remove", "origin", cwd=work)
git("update-ref", "-d", "refs/remotes/origin/main", cwd=work)
rc, out = run_preci(work)
no_pass = ("无法验证" in out) or ("取不到" in out) or ("不一致" in out)
report("取不到 origin/main（不应放行）",
       no_pass, f"报错={no_pass}")
if not no_pass:
    show(out, "case 3")

print(f"\n  {passed}/{passed + failed} 符合预期")
print("\n  豁免是被证明的，不是被信任的：")
print("  一旦有人在 sync/* 上放了改动，树就不再等于 main，门禁立刻失败。")
sys.exit(0 if failed == 0 else 1)
