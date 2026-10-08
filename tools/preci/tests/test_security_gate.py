#!/usr/bin/env python3
"""Test the security-gate logic (audit evidence + dual sign-off) against a matrix.

Extracts the security-gate `run:` body from pr-guard.yml and executes it in a
throwaway git repo per case, so `git diff origin/main...HEAD` is real.
"""
import hashlib
import os
import re
import shutil
import subprocess
import sys
import tempfile
import _paths as P  # noqa: E402  (shared location-independent paths)

REPO = P.ROOT
WORKFLOW = P.path(".github", "workflows", "pr-guard.yml")
BASH = P.BASH

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wfjobs import parse_jobs

jobs = parse_jobs(WORKFLOW)
if not jobs.get("security-gate"):
    print(f"FAILED to extract security-gate body (jobs={ {k: len(v) for k, v in jobs.items()} })")
    sys.exit(2)
# Take the security-gate body; index-based extraction is what silently broke
# when new steps were inserted into a *different* job.
gate = jobs["security-gate"][0]
print(f"security-gate body: {len(gate.splitlines())} lines "
      f"(jobs: { {k: len(v) for k, v in jobs.items()} })")
print(f"extracted security-gate body: {len(gate.splitlines())} lines")
GATE_SH = os.path.join(tempfile.gettempdir(), "gate-under-test.sh")
# ⚠️ 必须复现 workflow 的 `env: LC_ALL: C.UTF-8`。YAML 的 env 块在把 run body
# 当普通脚本执行时不会生效；缺了它 sed 的 `[:：]` 会按**字节**拆坏全角冒号，
# 于是 PASS 前面残留 0x89 字节，全部正确用例被误判为失败（实测 15/5 vs 20/0）。
open(GATE_SH, "w", encoding="utf-8", newline="\n").write(
    "export LC_ALL=C.UTF-8\n" + gate + "\n")

# The audit gate now demands a verifiable evidence line: a repo path plus that
# file's sha256, which the gate recomputes. Tests that lack it fail by design, so
# the fixture must carry a correct one. The hash is computed from a scratch file;
# the harness re-creates the same content inside each throwaway repo.
import hashlib as _hl
import tempfile as _tf

_EV_CONTENT = b"// audited source\nint a = 1;\n"
EV_PATH = "mod/network/a.cpp"
EV_HASH = _hl.sha256(_EV_CONTENT).hexdigest()
EV_LINE = f"- 证据：{EV_PATH} | sha256={EV_HASH} | 核对了本次改动"

HEADER = """# 审计报告：transport 模块

- 审计对象：{subject}


- 审计编号：AUDIT-20261008-001
- 审计对象：{subject}
- 触发类型：代码审计
- 审计人：AI-C3
- 结论：{overall}

## 1. 审计范围

transport 层。
"""

SIGNOFF = """
## 3. 双人独立签核（必填，缺一即阻断合并）

- C3-a（密码学 / 协议 / 完整性）：{a_role}
  结论：{a}
  审计者：{a_who}

- C3-b（隐私 / 中继 / 抗滥用）：{b_role}
  结论：{b}
  审计者：{b_who}
"""


def report(overall="PASS", a="PASS", b="PASS", a_who="auditor-one", b_who="auditor-two",
           a_role="范围 = E2EE、HMAC、序列号与时间戳防重放、密钥轮换、版本握手强校验",
           b_role="范围 = 隐私红线、中继零信任、速率限制与包大小、抗 Sybil、反作弊范围（仅 PvP）",
           signoff=None, evidence=None,
           subject="PR #1 / commit deadbeef / mod/network/transport/session.cpp"):
    if evidence is None:
        evidence = EV_LINE
    if signoff is None:
        signoff = SIGNOFF.format(a=a, b=b, a_who=a_who, b_who=b_who,
                                 a_role=a_role, b_role=b_role)
    # Evidence goes between the conclusion block and the sign-off, matching
    # the template in docs/audit/README.md.
    ev = "\n## 证据\n\n" + evidence + "\n"
    return HEADER.format(overall=overall, subject=subject) + ev + signoff


R_OK = report()
R_WARN = report(overall="PASS_WITH_WARNING", a="PASS_WITH_WARNING", b="PASS_WITH_WARNING")
R_A_FAIL = report(a="FAIL")                       # one signer refuses
R_B_MISMATCH = report(b="PASS_WITH_WARNING")      # one signer disagrees with label
R_NO_SIGNOFF = HEADER.format(overall="PASS", subject="PR #1 / commit deadbeef / mod/network/transport/session.cpp") + "\n## 3. 结论\n\n通过。\n"
R_ONE_SIGNER = HEADER.format(overall="PASS", subject="PR #1 / commit deadbeef / mod/network/transport/session.cpp") + """
## 3. 双人独立签核（必填）

- C3-a（密码学 / 协议 / 完整性）：范围 = x
  结论：PASS
"""
R_SIGNOFF_NO_VERDICT = HEADER.format(overall="PASS", subject="PR #1 / commit deadbeef / mod/network/transport/session.cpp") + """
## 3. 双人独立签核（必填）

- C3-a（密码学 / 协议 / 完整性）：范围 = x
  审计者：one

- C3-b（隐私 / 中继 / 抗滥用）：范围 = y
  审计者：two
"""
R_SAME_WHO = report(a_who="same-person", b_who="same-person")
R_HALF_WIDTH = HEADER.format(overall="PASS", subject="PR #1 / commit deadbeef / mod/network/transport/session.cpp") + "\n## 证据\n\n" + EV_LINE + """
## 3. 双人独立签核（必填）

- C3-a（密码学 / 协议 / 完整性）：范围 = x
  结论: PASS
- C3-b（隐私 / 中继 / 抗滥用）：范围 = y
  结论: PASS
"""
R_DUP_HEADING_ONLY = HEADER.format(overall="PASS", subject="PR #1 / commit deadbeef / mod/network/transport/session.cpp") + """
## 3. 双人独立签核占位
## 4. 双人独立签核占位
"""

# Value is the file body written by the harness, so the evidence hash
# (computed from _EV_CONTENT) matches what the gate recomputes.
SENSITIVE = {"mod/network/a.cpp": _EV_CONTENT.decode("utf-8")}
REPORT_PATH = "docs/audit/audit_transport_20261008.md"

CASES = [
    ("no sensitive file",                 {"docs/n.md": "hi"},     "", 0),
    ("sensitive, no label",               SENSITIVE,               "", 1),
    ("sensitive, audit:failed",           SENSITIVE,               "audit:failed", 1),
    ("sensitive, label but no report",    SENSITIVE,               "audit:passed", 1),
    ("report complete + passed",          {**SENSITIVE, REPORT_PATH: R_OK},   "audit:passed", 0),
    ("report WARN + warning",             {**SENSITIVE, REPORT_PATH: R_WARN}, "audit:warning", 0),
    ("report PASS + warning label",       {**SENSITIVE, REPORT_PATH: R_OK},   "audit:warning", 1),
    ("overall FAIL but label passed",     {**SENSITIVE, REPORT_PATH: report(overall="FAIL")}, "audit:passed", 1),
    ("one signer FAIL",                   {**SENSITIVE, REPORT_PATH: R_A_FAIL},    "audit:passed", 1),
    ("one signer mismatches label",       {**SENSITIVE, REPORT_PATH: R_B_MISMATCH}, "audit:passed", 1),
    ("no sign-off section",               {**SENSITIVE, REPORT_PATH: R_NO_SIGNOFF}, "audit:passed", 1),
    ("only one signer",                   {**SENSITIVE, REPORT_PATH: R_ONE_SIGNER}, "audit:passed", 1),
    ("sign-off without verdicts",         {**SENSITIVE, REPORT_PATH: R_SIGNOFF_NO_VERDICT}, "audit:passed", 1),
    ("same person signs both (text cannot tell)", {**SENSITIVE, REPORT_PATH: R_SAME_WHO}, "audit:passed", 0),
    ("half-width colon in verdict",       {**SENSITIVE, REPORT_PATH: R_HALF_WIDTH}, "audit:passed", 0),
    ("heading appears twice, no signers", {**SENSITIVE, REPORT_PATH: R_DUP_HEADING_ONLY}, "audit:passed", 1),
    # Blanking 审计对象 must fail. Note the replacement targets only the
    # "审计编号/审计对象" metadata lines, NOT the evidence line (an earlier version
    # replaced a string that also matched inside the evidence block, so the case
    # failed for the wrong reason -- missing evidence instead of empty subject).
    # Blanking BOTH 审计对象 lines: HEADER carries two, and leaving either one
    # non-empty lets the gate find a valid subject (this case failed for that
    # reason before the second line was parameterised).
    ("empty 审计对象",                     {**SENSITIVE, REPORT_PATH: report(subject="")},
                                                                          "audit:passed", 1),
    ("no 审计报告 title",                  {**SENSITIVE, REPORT_PATH: R_OK.replace("# 审计报告：transport 模块", "# x")},
                                                                          "audit:passed", 1),
    ("bad report filename",               {**SENSITIVE, "docs/audit/audit_transport.md": R_OK}, "audit:passed", 1),
    # New evidence rule: a report without a verifiable evidence line must fail,
    # and a wrong hash must fail even though everything else looks right.
    ("report without evidence line",       {**SENSITIVE, REPORT_PATH: R_OK.replace(EV_LINE, "- 证据：（无）")}, "audit:passed", 1),
    ("report with wrong hash",             {**SENSITIVE, REPORT_PATH: R_OK.replace(EV_HASH, "0" * 64)}, "audit:passed", 1),
    ("report with evidence outside diff",  {**SENSITIVE, REPORT_PATH: R_OK.replace("mod/network/a.cpp", "README.md")}, "audit:passed", 1),
    ("relay path + ok report",            {"relay/node.cpp": "x", EV_PATH: _EV_CONTENT.decode("utf-8"), REPORT_PATH: R_OK}, "audit:passed", 0),
]

passed = failed = 0
fails = []
for name, files, labels, expect in CASES:
    work = tempfile.mkdtemp(prefix="gatecase-")
    try:
        def run(args, cwd=work):
            return subprocess.run(args, cwd=cwd, capture_output=True, text=True,
                                  encoding="utf-8", errors="replace")

        run(["git", "init", "-q", "-b", "main"])
        run(["git", "config", "user.email", "t@t"])
        run(["git", "config", "user.name", "t"])
        os.makedirs(os.path.join(work, "docs", "audit"), exist_ok=True)
        open(os.path.join(work, "README.md"), "w").write("base\n")
        # A real file the audit report can cite in a verifiable evidence line.
        # It must be committed to the BASE with DIFFERENT content: if the branch
        # writes the identical bytes, git sees no change, the path never appears in
        # `git diff origin/main...HEAD`, and the gate reports "未触及安全路径".
        # That exact mistake made 18 cases fail before it was caught.
        os.makedirs(os.path.join(work, "mod", "network"), exist_ok=True)
        _ev = os.path.join(work, "mod", "network", "a.cpp")
        open(_ev, "wb").write(b"// base version\n")
        open(os.path.join(work, "docs", "audit", "README.md"), "w").write("base\n")
        run(["git", "add", "-A"])
        run(["git", "commit", "-qm", "base"])
        run(["git", "update-ref", "refs/remotes/origin/main", "HEAD"])

        run(["git", "checkout", "-q", "-b", "lead/test"])
        for rel, content in files.items():
            p = os.path.join(work, rel)
            os.makedirs(os.path.dirname(p), exist_ok=True)
            # write as bytes: a text write can rewrite newlines, which would
            # change the sha256 the gate recomputes
            with open(p, "wb") as fh:
                fh.write(content.encode("utf-8") if isinstance(content, str) else content)
        run(["git", "add", "-A"])
        run(["git", "commit", "-qm", "case"])

        env = dict(os.environ)
        env.update({"BASE_REF": "main", "LABELS": labels, "PRNUMBER": "1", "PRBODY": "b"})
        sh = os.path.join(work, "gate.sh")
        shutil.copy(GATE_SH, sh)
        r = subprocess.run([BASH, "gate.sh"], cwd=work, capture_output=True, text=True,
                           encoding="utf-8", errors="replace", env=env)
        got = r.returncode
        ok = (got == expect)
        if ok:
            passed += 1
        else:
            failed += 1
            fails.append((name, expect, got, (r.stdout or "") + (r.stderr or "")))
        mark = "PASS" if ok else "FAIL"
        label = name.encode("ascii", "replace").decode("ascii")
        print(f"  [{mark}] {label:52} expect={expect} got={got}")
    finally:
        shutil.rmtree(work, ignore_errors=True)

dump = os.path.join(tempfile.gettempdir(), "gate-matrix-failures.txt")
with open(dump, "w", encoding="utf-8") as fh:
    for name, expect, got, out in fails:
        fh.write(f"\n===== {name} (expect={expect} got={got}) =====\n{out}\n")
print()
print(f"matrix: {passed} passed, {failed} failed")
if failed:
    print(f"failure detail -> {dump}")
sys.exit(0 if failed == 0 else 1)
