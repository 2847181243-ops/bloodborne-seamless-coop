#!/usr/bin/env python3
"""Test the strict pr-guard template job.

Runs the two new run-bodies separately:
  step0 = 校验 PR 描述（章节/内容/关联 Issue）
  step1 = 机器可验证项（勾选之外的真实检查）
The `gh` shim simulates issue lookup success/failure without network.
"""
import json
import os
import re
import shutil
import stat
import subprocess
import sys
import tempfile
import _paths as P  # noqa: E402  (shared location-independent paths)

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
REPO = P.ROOT
# Allow testing the workflow from another ref (e.g. the unmerged strict-pr-guard
# branch), because the machine-check step only exists there.
REF = os.environ.get("PR_GUARD_REF", "")
# Portability: TEMP is unset on Linux; tempfile.gettempdir() works everywhere.
if REF:
    WORKFLOW = os.path.join(tempfile.gettempdir(),
                            "prguard-" + REF.replace("/", "_") + ".yml")
    _r = subprocess.run(["git", "-C", REPO, "show", REF + ":.github/workflows/pr-guard.yml"],
                        capture_output=True, text=True, encoding="utf-8", errors="replace")
    if _r.returncode != 0:
        print("cannot read pr-guard.yml from ref " + REF + ": " + _r.stderr[:200])
        sys.exit(2)
    open(WORKFLOW, "w", encoding="utf-8", newline="\n").write(_r.stdout)
    print("using pr-guard.yml from ref: " + REF)
else:
    WORKFLOW = P.path(".github", "workflows", "pr-guard.yml")
BASH = P.BASH
REAL_GIT = P.GIT

lines = open(WORKFLOW, encoding="utf-8").read().split("\n")
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wfjobs import parse_jobs

jobs = parse_jobs(WORKFLOW)
tpl = jobs.get("template", [])
print(f"run-bodies by job: { {k: len(v) for k, v in jobs.items()} }")
# The template job has 1 step on main and 2 once the strict-pr-guard PR lands.
# Assert a range so this harness does not need editing on every base change.
assert 1 <= len(tpl) <= 3, f"unexpected template step count: {len(tpl)}"
STEP = {"desc": tpl[0]}
if len(tpl) >= 2:
    STEP["machine"] = tpl[1]
else:
    # On main the template job has a single step; the two-step (desc + machine)
    # split arrives with the strict-pr-guard PR. Mark machine cases unsupported
    # LOUDLY rather than silently passing them.
    STEP["machine"] = tpl[0]
    print("NOTE: template job has 1 step on this base; 'machine' cases exercise the")
    print("      same step as 'desc' and therefore do NOT validate the new checks.")

SHIM = tempfile.mkdtemp(prefix="shim3-")
gh = os.path.join(SHIM, "gh")
open(gh, "w", encoding="utf-8", newline="\n").write("""#!/usr/bin/env bash
# stub for: gh api repos/<repo>/issues/<n>
if [[ "${GH_SHIM_MODE:-ok}" == "fail" ]]; then
  echo "gh: Resource not accessible (HTTP 403)" >&2
  exit 1
fi
echo '{"title":"${GH_SHIM_TITLE:-stub issue}","state":"${GH_SHIM_STATE:-open}"}'
exit 0
""")
os.chmod(gh, os.stat(gh).st_mode | stat.S_IEXEC)
jq = os.path.join(SHIM, "jq")
open(jq, "w", encoding="utf-8", newline="\n").write("""#!/usr/bin/env bash
# minimal jq -r '.title' / '.state'
key="${2:-}"
case "$key" in
  .title) echo "${GH_SHIM_TITLE:-stub issue}" ;;
  .state) echo "${GH_SHIM_STATE:-open}" ;;
  *) echo "" ;;
esac
exit 0
""")
os.chmod(jq, os.stat(jq).st_mode | stat.S_IEXEC)
gits = os.path.join(SHIM, "git")
open(gits, "w", encoding="utf-8", newline="\n").write(
    f"""#!/usr/bin/env bash
if [[ "$1" == "fetch" ]]; then exit 0; fi
exec "{REAL_GIT.replace(chr(92), '/')}" "$@"
""")
os.chmod(gits, os.stat(gits).st_mode | stat.S_IEXEC)

SEC = ["- [x] 未新增任何形式的隐私数据采集或出网字段",
       "- [x] 未新增持久化标识 / 设备指纹",
       "- [x] 消息附带 HMAC，且有防重放机制（序列号 + 时间戳）",
       "- [x] 校验失败路径会记录 `[SECURITY]` 日志并拒绝",
       "- [x] 中继仍无法解密 / 篡改 / 重放 / 记录日志",
       "- [x] 存档写入已原子化，非法物品被拒绝写入",
       "- [x] 物品发放有记账，唯一物品不重复"]


def body(sections_ok=True, issue_line="- 关联 Issue：#5", verify="Compile Verified",
         evidence=None, sec=None, empty_section=None, level_text=None):
    s = {
        "改了什么": "内容 A",
        "为什么改": "内容 B\n\n" + issue_line,
        "修改了哪个模块": "- [x] Docs / CI / Config",
        "影响哪些系统": "内容 D",
        "如何验证": f"- [x] {verify}" if verify else "（无勾选）",
        "是否存在兼容性风险": "无",
        # 作者也可能把等级写在正文里；门禁对此做两级回退
        "当前验证等级": level_text if level_text is not None else "（无）",
        # 门禁只检查本节存在且非空；质量判不了，靠写的人。
        # 夹具里给一句真实内容，正面用例才有意义。
        "决策与不确定记录": ("1. 无\n2. 无\n3. 无 —— 本次由 test_pg_strict 覆盖确认真做对了"),
    }
    if evidence is not None:
        s["证据等级（涉及逆向结论时必填）"] = f"- [x] {evidence}"
    if sec is not None:
        s["安全自检（涉及网络 / 存档 / 物品发放时必填）"] = "\n".join(sec) if sec else "（未勾选）"
    if empty_section:
        s[empty_section] = ""
    out = []
    for k, v in s.items():
        out.append(f"## {k}")
        out.append("")
        out.append(v)
        out.append("")
    return "\n".join(out)


CASES = [
    # (name, files, body, step, gh_mode, expect, marker)
    ("desc: complete + issue ok", ["docs/x.md"], body(), "desc", "ok", 0, "关联 Issue #5"),
    ("desc: missing 影响哪些系统", ["docs/x.md"],
     body().replace("## 影响哪些系统\n\n内容 D\n\n", ""), "desc", "ok", 1, "缺少必需章节"),
    ("desc: empty section body", ["docs/x.md"], body(empty_section="是否甶在兼容性风险".replace("甶", "存")),
     "desc", "ok", 1, "是空的"),
    ("desc: issue placeholder #___", ["docs/x.md"], body(issue_line="- 关联 Issue：#___"),
     "desc", "ok", 1, "未填写真实编号"),
    ("desc: no issue field", ["docs/x.md"], body(issue_line=""), "desc", "ok", 1, "缺少「关联 Issue」字段"),
    ("desc: issue API failure distinguishable", ["docs/x.md"], body(), "desc", "fail", 1, "读取 Issue"),
    # 已关闭的 Issue 由「警告」改为「错误」。
    # 起因是实测：最近 8 个 PR 里有 7 个都引用同一个已关闭且无关的 Issue #19 ——
    # 只要编号真实就能过，于是「关联 Issue」成了模板填充，绑定失去意义。
    # 一个新的 PR 引用已关闭的 Issue，通常就说明那是填充而不是真实关联。
    ("desc: closed issue rejected", ["docs/x.md"], body(), "desc", "ok", 1, "已关闭",
     {"GH_SHIM_STATE": "closed"}),

    ("machine: baseline ok (docs only)", ["docs/x.md"], body(), "machine", "ok", 0, "机器可验证项全部通过"),
    ("machine: no verify level anywhere", ["docs/x.md"],
     body(verify=None, level_text="（无）"), "machine", "ok", 1, "未声明验证等级"),
    ("machine: level in prose only (fallback)", ["docs/x.md"],
     body(verify=None, level_text="Runtime Verified"), "machine", "ok", 0, "验证等级（正文）"),
    # Two SEPARATE declaration lines. An earlier version put both on one line
    # ("Compile Verified，也做了 Runtime Verified"), which is not a declaration of
    # either level under the new rule -- so the case asserted the wrong outcome.
    ("machine: two levels declared on separate lines", ["docs/x.md"],
     body(verify=None, level_text="Compile Verified\nRuntime Verified"),
     "machine", "ok", 1, "必须且只能声明 1 个"),
    ("machine: two verify levels checked", ["docs/x.md"],
     body(verify="Compile Verified\n- [x] Runtime Verified"), "machine", "ok", 1, "只能声明 1 个"),
    ("machine: docs/re without evidence", ["docs/re/x.md"], body(), "machine", "ok", 1, "未勾选任何项"),
    ("machine: docs/re with evidence", ["docs/re/x.md"], body(evidence="Confirmed"), "machine", "ok", 0, "证据等级已勾选"),
    ("machine: sensitive path w/o sec selfcheck", ["mod/network/a.cpp"], body(sec=[]), "machine", "ok", 1, "未全部勾选"),
    ("machine: sensitive path with full selfcheck", ["mod/network/a.cpp"], body(sec=SEC), "machine", "ok", 0, "全部勾选"),
    ("machine: sensitive path partial selfcheck", ["mod/network/a.cpp"],
     # 真实场景：模板 7 项，作者只勾了前 3 项 -> 分母应为 7，必须阻断
     body(sec=SEC[:3] + [s.replace("[x]", "[ ]") for s in SEC[3:]]), "machine", "ok", 1, "未全部勾选"),
    ("machine: no module checked", ["docs/x.md"],
     body().replace("- [x] Docs / CI / Config", "- [ ] Docs / CI / Config"), "machine", "ok", 1, "修改了哪个模块"),
]

def writecase(work, files, cred):
    def run(args, cwd=work):
        return subprocess.run(args, cwd=cwd, capture_output=True, text=True,
                              encoding="utf-8", errors="replace")
    run([REAL_GIT, "init", "-q", "-b", "main"])
    run([REAL_GIT, "config", "user.email", "t@t"])
    run([REAL_GIT, "config", "user.name", "t"])
    open(os.path.join(work, "README.md"), "w").write("base\n")
    run([REAL_GIT, "add", "-A"])
    run([REAL_GIT, "commit", "-qm", "base"])
    run([REAL_GIT, "update-ref", "refs/remotes/origin/main", "HEAD"])
    run([REAL_GIT, "checkout", "-q", "-b", "lead/t"])
    for rel in files:
        p = os.path.join(work, rel)
        os.makedirs(os.path.dirname(p), exist_ok=True)
        open(p, "w").write("x\n")
    if cred:
        p = os.path.join(work, "docs", "leak.md")
        os.makedirs(os.path.dirname(p), exist_ok=True)
        open(p, "w").write(f"token = {cred}\n")
    run([REAL_GIT, "add", "-A"])
    run([REAL_GIT, "commit", "-qm", "case"])


passed = failed = 0
fails = []
for case in CASES:
    name, files, b, step, mode, expect, marker = case[:7]
    extras = case[7] if len(case) > 7 else {}
    work = tempfile.mkdtemp(prefix="pg-")
    try:
        writecase(work, files, None)
        sh = os.path.join(work, "step.sh")
        # 复现 workflow 的 env 块（YAML env 不会作用于被当普通脚本执行的 run body）
        open(sh, "w", encoding="utf-8", newline="\n").write(
            'export LC_ALL=C.UTF-8\nexport PATH="%s:$PATH"\n' % SHIM
            + STEP[step] + "\n")
        env = dict(os.environ)
        env.update({"BODY": b, "BASE_REF": "main", "PRNUMBER": "9",
                    "GH_SHIM_MODE": mode, "GITHUB_REPOSITORY": "o/r",
                    "GITHUB_TOKEN": "x"})
        env.update(extras)
        r = subprocess.run([BASH, "-e", "-o", "pipefail", "step.sh"], cwd=work,
                           capture_output=True, text=True, encoding="utf-8",
                           errors="replace", env=env)
        out = (r.stdout or "") + (r.stderr or "")
        ok = (r.returncode == expect) and (marker in out if marker else True)
        if ok:
            passed += 1
        else:
            failed += 1
            fails.append((name, expect, r.returncode, marker, out))
        print(f"  [{'PASS' if ok else 'FAIL'}] {name:46} expect={expect} got={r.returncode}")
    finally:
        shutil.rmtree(work, ignore_errors=True)

# credential scan case needs a planted secret
work = tempfile.mkdtemp(prefix="pgcred-")
try:
    writecase(work, ["docs/x.md"], "ghp_" + "A" * 36)
    sh = os.path.join(work, "step.sh")
    open(sh, "w", encoding="utf-8", newline="\n").write(
        'export PATH="%s:$PATH"\n' % SHIM + STEP["machine"] + "\n")
    env = dict(os.environ)
    env.update({"BODY": body(), "BASE_REF": "main", "PRNUMBER": "9",
                "GH_SHIM_MODE": "ok", "GITHUB_REPOSITORY": "o/r", "GITHUB_TOKEN": "x"})
    r = subprocess.run([BASH, "-e", "-o", "pipefail", "step.sh"], cwd=work,
                       capture_output=True, text=True, encoding="utf-8",
                       errors="replace", env=env)
    out = (r.stdout or "") + (r.stderr or "")
    ok = r.returncode == 1 and "凭据" in out
    print(f"  [{'PASS' if ok else 'FAIL'}] {'machine: planted credential caught':46} expect=1 got={r.returncode}")
    if ok:
        passed += 1
    else:
        failed += 1
        fails.append(("planted credential", 1, r.returncode, "凭据", out))
finally:
    shutil.rmtree(work, ignore_errors=True)

dump = os.path.join(tempfile.gettempdir(), "pg-failures.txt")
with open(dump, "w", encoding="utf-8") as fh:
    for name, expect, got, marker, out in fails:
        fh.write(f"\n===== {name} (expect={expect} got={got} marker={marker!r}) =====\n{out}\n")
print(f"\nmatrix: {passed} passed, {failed} failed")
if failed:
    print(f"detail -> {dump}")
shutil.rmtree(SHIM, ignore_errors=True)
sys.exit(0 if failed == 0 else 1)
