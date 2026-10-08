#!/usr/bin/env python3
"""Test the fixed verify-level logic against real and synthetic bodies."""
import json
import os
import subprocess
import sys
import shutil
import tempfile
import _paths as P  # noqa: E402  (shared location-independent paths)

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wfjobs import parse_jobs  # noqa: E402

REPO = P.ROOT
WF = os.path.join(REPO, ".github", "workflows", "pr-guard.yml")
BASH = P.BASH
# Portable: SystemRoot only exists on Windows, and the ubuntu runner has curl
# on PATH. A hardcoded Windows path raised KeyError: 'SystemRoot' in CI.
CURL = shutil.which("curl") or os.path.join(
    os.environ.get("SystemRoot", "C:/Windows"), "System32", "curl.exe")
TOKEN = open(os.path.join(os.environ["USERPROFILE"], ".config", "dsh", "token.txt"),
             encoding="ascii").read().strip()
B = "https://api.github.com/repos/2847181243-ops/bloodborne-seamless-coop"

# real PR #6 body
tmp = os.path.join(tempfile.gettempdir(), "p6real.json")
subprocess.run([CURL, "-sS", "--max-time", "40", "-H",
                f"Authorization: Bearer {TOKEN}", "-H", "User-Agent: preci",
                "-H", "Accept: application/vnd.github+json", "-o", tmp,
                f"{B}/pulls/6"], capture_output=True)
real = json.load(open(tmp, encoding="utf-8"))["body"]

machine = parse_jobs(WF)["template"][1]

CASES = [
    ("PR #6 真实正文（诚实说明「不适用」）", real, 0),
    ("只勾 1 个等级，无说明", """## 改了什么
x
## 为什么改
x
- 关联 Issue：#5
## 修改了哪个模块
- [x] Docs / CI / Config
## 影响哪些系统
x
## 如何验证
- [x] Compile Verified
## 是否存在兼容性风险
无
## 当前验证等级
Compile Verified
""", 0),
    ("正文并列声明 2 个等级（应拦）", """## 改了什么
x
## 为什么改
x
- 关联 Issue：#5
## 修改了哪个模块
- [x] Docs / CI / Config
## 影响哪些系统
x
## 如何验证
（无勾选）
## 是否存在兼容性风险
无
## 当前验证等级
Compile Verified
Runtime Verified
""", 1),
    ("无勾选也无正文等级（应拦）", """## 改了什么
x
## 为什么改
x
- 关联 Issue：#5
## 修改了哪个模块
- [x] Docs / CI / Config
## 影响哪些系统
x
## 如何验证
（无勾选）
## 是否存在兼容性风险
无
## 当前验证等级
待补充。
""", 1),
    ("正文写 1 个等级 + 说明「不适用」（应放行）", """## 改了什么
x
## 为什么改
x
- 关联 Issue：#5
## 修改了哪个模块
- [x] Docs / CI / Config
## 影响哪些系统
x
## 如何验证
（无勾选）
## 是否存在兼容性风险
无
## 当前验证等级
Runtime Verified
Multiplayer / Regression / Feature Verified 不适用。
""", 0),
]

ok_all = True
for name, body, expect in CASES:
    bf = os.path.join(tempfile.gettempdir(), "lv-body.md")
    open(bf, "w", encoding="utf-8", newline="\n").write(body)
    sh = os.path.join(tempfile.gettempdir(), "lv-step.sh")
    pre = ("export LC_ALL=C.UTF-8\nexport BASE_REF=main\n"
           "export GITHUB_REPOSITORY=2847181243-ops/bloodborne-seamless-coop\n"
           f"export BODY=\"$(cat '{bf.replace(chr(92), '/')}')\"\n")
    open(sh, "w", encoding="utf-8", newline="\n").write(pre + machine)
    r = subprocess.run([BASH, sh], cwd=REPO, capture_output=True, text=True,
                       encoding="utf-8", errors="replace")
    got = r.returncode
    ok = (got == expect)
    ok_all &= ok
    print(f"  [{'OK ' if ok else 'BAD'}] {name[:44]:46} 期望={expect} 实际={got}")
    if not ok or "等级" in name:
        for l in ((r.stdout or "") + (r.stderr or "")).splitlines():
            if "验证等级" in l or "::error" in l:
                print("         " + l.strip()[:150])

print(f"\n全部符合预期: {ok_all}")
sys.exit(0 if ok_all else 1)
