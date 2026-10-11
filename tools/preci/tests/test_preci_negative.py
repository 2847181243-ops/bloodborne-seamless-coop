#!/usr/bin/env python3
"""Negative-case tests for tools/preci/preci.ps1.

Proves the pre-CI is not "always green": each case injects a specific defect into
an isolated clone and asserts the matching check turns red.

Runs the REAL script via -Repo, so the checks themselves are exercised rather
than a re-implementation.
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile
import _paths as P  # noqa: E402  (shared location-independent paths)

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
SRC = P.ROOT
PRECI = os.path.join(SRC, "tools", "preci", "preci.ps1")
# Resolved portably. Hardcoding "powershell.exe" failed on the ubuntu runner with
# FileNotFoundError; _paths tries powershell.exe, powershell, then pwsh.
PS = P._find_powershell() or "powershell.exe"
GIT = P.GIT


def run(args, cwd):
    return subprocess.run(args, cwd=cwd, capture_output=True, text=True,
                          encoding="utf-8", errors="replace")


def fresh_clone():
    d = tempfile.mkdtemp(prefix="preci-neg-")
    r = run([GIT, "clone", "-q", SRC, d], cwd=tempfile.gettempdir())
    if r.returncode != 0:
        print("clone failed:", r.stderr[:300])
        sys.exit(2)
    run([GIT, "config", "user.email", "t@t"], d)
    run([GIT, "config", "user.name", "t"], d)
    # make the clone's origin/main point at its own HEAD
    run([GIT, "update-ref", "refs/remotes/origin/main", "HEAD"], d)
    return d


def scenario(name, branch, mutate, expect_substr, stage=1):
    d = fresh_clone()
    try:
        run([GIT, "checkout", "-q", "-b", branch], d)
        mutate(d)
        run([GIT, "add", "-A"], d)
        run([GIT, "commit", "-qm", "case"], d)
        cmd = [PS, "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", PRECI,
               "-Repo", d, "-Stage", str(stage), "-Base", "origin/main"]
        r = run(cmd, d)
        out = (r.stdout or "") + (r.stderr or "")
        caught = expect_substr in out
        # a case is correct if it is caught AND the script exits non-zero
        ok = caught and r.returncode == 1
        print(f"  [{'PASS' if ok else 'FAIL'}] {name:42} exit={r.returncode} "
              f"caught={caught}")
        if not ok:
            snippet = "\n".join([l for l in out.splitlines()
                                 if "FAIL" in l or "越界" in l or "must" in l][:6])
            print("        " + snippet.replace("\n", "\n        "))
        return ok
    finally:
        shutil.rmtree(d, ignore_errors=True)


def scenario_xa(name, labels, arg, expect_code, expect_substr, stage=1, pr_body=None):
    """Cross-ownership authorization cases.

    The issue-label lookup is replaced by PRCI_LABELS_FILE so the matrix never
    needs the network: `labels` is the label list, None simulates a read failure.
    ``expect_code=None`` means "only assert the message" (used for stage 2, where
    the other stage-2 checks legitimately fail on the synthetic PR body).
    """
    d = fresh_clone()
    try:
        run([GIT, "checkout", "-q", "-b", "lead/xa"], d)
        # 用 markdown 制造越界：它不触发样式检查，能单独验证授权逻辑
        os.makedirs(os.path.join(d, "docs", "security"), exist_ok=True)
        open(os.path.join(d, "docs", "security", "x.md"), "w",
             encoding="utf-8", newline="\n").write("x\n")
        run([GIT, "add", "-A"], d)
        run([GIT, "commit", "-qm", "case"], d)
        env = dict(os.environ)
        if labels is None:
            env["PRCI_LABELS_FILE"] = os.path.join(d, "missing-labels.txt")
        else:
            lf = os.path.join(d, "labels.txt")
            open(lf, "w", encoding="utf-8", newline="\n").write(labels + "\n")
            env["PRCI_LABELS_FILE"] = lf
        cmd = [PS, "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", PRECI,
               "-Repo", d, "-Stage", str(stage), "-Base", "origin/main", "-AllowSkip"]
        if arg is not None:
            cmd += ["-CrossAgentIssue", str(arg)]
        if pr_body is not None:
            bp = os.path.join(d, "pr-body.md")
            open(bp, "w", encoding="utf-8", newline="\n").write(pr_body)
            cmd += ["-PrBody", bp]
        # env 必须显式传入：run() 包装器不接收 env，早先的版本因此把
        # PRCI_LABELS_FILE 丢掉了，真去查了线上 Issue（还恰好带 cross-agent 标签）
        r = subprocess.run(cmd, cwd=d, capture_output=True, text=True,
                           encoding="utf-8", errors="replace", env=env)
        out = (r.stdout or "") + (r.stderr or "")
        caught = expect_substr in out
        code_ok = (expect_code is None) or (r.returncode == expect_code)
        ok = caught and code_ok
        print(f"  [{'PASS' if ok else 'FAIL'}] {name:46} exit={r.returncode} "
              f"expect={expect_code} caught={caught}")
        if not ok:
            snippet = "\n".join([l for l in out.splitlines()
                                  if ("cross-agent" in l or "授权" in l or "越界" in l)][:6])
            print("        " + snippet.replace("\n", "\n        "))
        return ok
    finally:
        shutil.rmtree(d, ignore_errors=True)


def m_scope(d):
    os.makedirs(os.path.join(d, "mod", "security"), exist_ok=True)
    open(os.path.join(d, "mod", "security", "hack.cpp"), "w").write("x\n")


def m_branch(d):
    pass  # branch name itself is the defect


def m_encoding(d):
    with open(os.path.join(d, "README.md"), "ab") as fh:
        fh.write(b"\xe7\x80\xb9")  # GBK mojibake marker


def m_secret(d):
    os.makedirs(os.path.join(d, "docs"), exist_ok=True)
    with open(os.path.join(d, "docs", "leak.md"), "w") as fh:
        fh.write("token = ghp_" + "A" * 36 + "\n")


def m_codeowners(d):
    with open(os.path.join(d, ".github", "CODEOWNERS"), "a",
              encoding="utf-8", newline="\n") as fh:
        fh.write("  /mod/security/   @someone\n")


def m_structure(d):
    p = os.path.join(d, "SECURITY.md")
    if os.path.exists(p):
        os.remove(p)


def m_wfsyntax(d):
    p = os.path.join(d, ".github", "workflows", "build.yml")
    t = open(p, encoding="utf-8", errors="replace").read()
    # Two earlier attempts at this case were themselves broken and proved nothing:
    #   (a) anchored on 'set -uo pipefail', which build.yml does not contain
    #       (it uses 'set -euo pipefail'), so nothing was injected;
    #   (b) injected at column 0, which lands OUTSIDE the `run: |` block, so the
    #       extracted body was unaffected.
    # Inject INSIDE the run body (10-space indent) an unbalanced `if`.
    anchor = "          set -euo pipefail"
    if anchor not in t:
        # fall back: indent whatever the set line is by re-using its own indent
        m = re.search(r"(?m)^(\s*)(set -e[uo]* pipefail)\s*$", t)
        assert m, "no set -e..o pipefail anchor found; fix the test"
        anchor = m.group(0)
    t = t.replace(anchor, anchor + "\n          if [ 1 -eq 1 ]; then", 1)
    assert "if [ 1 -eq 1 ]; then" in t, "injection failed"
    open(p, "w", encoding="utf-8", newline="\n").write(t)


def m_trailing_ws(d):
    """NOTE: markdown is deliberately EXEMPT from trailing-whitespace in
    .editorconfig (two trailing spaces mean <br>). An earlier version of this
    case injected into CONTRIBUTING.md and therefore proved nothing -- the gate
    passed, correctly. Use a non-markdown tracked file."""
    p = os.path.join(d, "tools", "preci", "gates.yml")
    t = open(p, encoding="utf-8").read()
    open(p, "w", encoding="utf-8", newline="\n").write(t + "trailing spaces here   \n")


def m_no_final_newline(d):
    p = os.path.join(d, "SECURITY.md")
    t = open(p, encoding="utf-8").read().rstrip("\n")
    open(p, "w", encoding="utf-8", newline="\n").write(t)


def m_mass_commented_code(d):
    """Task book §38 forbids mass-commenting code to silence errors."""
    os.makedirs(os.path.join(d, "mod", "core"), exist_ok=True)
    lines = ["// mod/core/thing.cpp -- test fixture", "// owner: AI-00", ""]
    for i in range(20):
        lines.append(f"// int value{i} = compute({i});")
    open(os.path.join(d, "mod", "core", "thing.cpp"), "w",
         encoding="utf-8", newline="\n").write("\n".join(lines) + "\n")


def m_no_file_header(d):
    os.makedirs(os.path.join(d, "mod", "core"), exist_ok=True)
    open(os.path.join(d, "mod", "core", "bare.cpp"), "w",
         encoding="utf-8", newline="\n").write("int main() { return 0; }\n")


CASES = [
    ("写入范围越界（lead 改 mod/security/）", "lead/oob", m_scope, "越界"),
    ("分支命名非法", "feature/nope", m_branch, "命名不合法"),
    ("GBK 误码残留", "lead/enc", m_encoding, "GBK"),
    ("凭据注入", "lead/cred", m_secret, "凭据"),
    ("CODEOWNERS 规则缩进", "lead/co", m_codeowners, "must not be indented"),
    ("必需文件缺失（SECURITY.md）", "lead/miss", m_structure, "SECURITY.md"),
    ("workflow run body 语法错误", "lead/wfbad", m_wfsyntax, "语法错误"),
    ("行尾空白（.editorconfig）", "lead/ws", m_trailing_ws, "trailing whitespace"),
    ("缺文件末尾换行", "lead/nonl", m_no_final_newline, "missing final newline"),
    # Style cases writing into mod/core/ MUST use an agent branch: on a
    # lead/* branch the ownership check fires first (mod/core/ is outside
    # lead's scope) and the style gate never runs, proving nothing.
    ("大面积注释掉代码（任务书 §38）", "agent/a1-comment", m_mass_commented_code,
     "commented-out code"),
    ("源文件缺文件头注释", "agent/a1-nohdr", m_no_file_header, "missing file header"),
]

XA_CASES = [
    ("跨所有权：cross-agent 授权通过", "bug,cross-agent", 5, 0,
     "已由 Issue #5（cross-agent）授权"),
    ("跨所有权：标签不符必须失败", "bug", 5, 1, "未带 cross-agent 标签"),
    ("跨所有权：标签读取失败（非标签问题）", None, 5, 1, "读取 Issue #5 的标签失败"),
    ("跨所有权：阶段 1 未提供 Issue", "cross-agent", None, 1, "-CrossAgentIssue"),
    ("跨所有权：阶段 2 从 PR 描述授权通过", "cross-agent", None, None,
     "已由 Issue #5（cross-agent）授权", 2, "改了什么\n\n关联 Issue：#5\n"),
    ("跨所有权：阶段 2 引用无标签 Issue", "bug", None, None,
     "未带 cross-agent 标签", 2, "关联 Issue：#5\n"),
]

print("=== pre-CI 负例矩阵（每个用例注入一个缺陷，必须被抓到）===")
passed = 0
for name, br, fn, exp in CASES:
    if scenario(name, br, fn, exp):
        passed += 1

print("\n=== 跨所有权授权（cross-agent）===")
xa_passed = 0
for case in XA_CASES:
    if scenario_xa(*case):
        xa_passed += 1

total = len(CASES) + len(XA_CASES)
got = passed + xa_passed
print(f"\ncaught {got}/{total}")
sys.exit(0 if got == total else 1)
