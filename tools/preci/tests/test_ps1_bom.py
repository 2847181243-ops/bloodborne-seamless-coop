#!/usr/bin/env python3
"""Guard the UTF-8 BOM on PowerShell files.

WHY THIS EXISTS
---------------
`tools/preci/preci.ps1` contains Chinese text and starts with a UTF-8 BOM
(`ef bb bf`). Windows PowerShell 5.1 reads a BOM-less file as the **system ANSI
codepage** (cp936/GBK on a Chinese Windows). The Chinese then becomes mojibake and
string literals break -- the script dies with a ParserError pointing at a line that
looks perfectly fine in any modern editor.

This actually happened: an edit tool rewrote preci.ps1 without the BOM. The symptom
was NOT "preci.ps1 is broken" -- it surfaced as two unrelated-looking failures inside
the skip-reason suite (`exit=1` where `2` and `0` were expected), because that suite
drives preci.ps1 and just saw "the script failed". Diagnosing it meant reading two
layers of output.

Measured state at the time:
    HEAD       tools/preci/preci.ps1        ef bb bf   (BOM present)
    working    tools/preci/preci.ps1        3c 23 0a   (BOM lost -> broken)
    HEAD       tools/preci/messages.json    7b 0a 20   (no BOM, and that is fine)

So the rule is per-file, not "all files need a BOM".

⚠️ Careful when checking this: `git show <ref>:<file> > out` in PowerShell writes
**UTF-16LE**, so the captured file starts with `ff fe` regardless of the real content.
Use `git cat-file blob` instead. That mistake cost time here.
"""
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _paths as P  # noqa: E402

ROOT = P.ROOT
BOM = b"\xef\xbb\xbf"

# Files that MUST keep a UTF-8 BOM, and why each one.
NEEDS_BOM = {
    "tools/preci/preci.ps1":
        "含中文；无 BOM 时 PowerShell 5.1 按 GBK 读，字符串字面量破损",
    "tools/setup-toolchain.ps1":
        "含中文；同上",
}

# Files that contain non-ASCII but deliberately have NO BOM.
# Listed so a future reader does not "fix" them.
NO_BOM_OK = {
    "tools/preci/messages.json": "由 Python 读取，UTF-8 无 BOM 是正确形式",
}

passed = failed = 0
print("=" * 68)
print("  PowerShell 文件的 UTF-8 BOM")
print("=" * 68)


def check(label, got, expect, detail=""):
    global passed, failed
    ok = got == expect
    if ok:
        passed += 1
        print(f"  [OK ] {label:44} {got}")
    else:
        failed += 1
        print(f"  [BAD] {label:44} 期望 {expect}，实际 {got}")
        if detail:
            print(f"        {detail}")
    return ok


for rel, why in NEEDS_BOM.items():
    p = os.path.join(ROOT, rel.replace("/", os.sep))
    if not os.path.isfile(p):
        check(f"{os.path.basename(rel)} 存在", False, True, f"{rel} 不存在")
        continue
    raw = open(p, "rb").read()
    has = raw.startswith(BOM)
    check(f"{os.path.basename(rel)} 有 BOM", has, True,
          f"{rel} 缺 UTF-8 BOM —— {why}。"
          f"用 edit 工具改写后容易丢失，需重新加回："
          f"open(p,'wb').write(b'\\xef\\xbb\\xbf' + raw)")
    if has:
        # a BOM alone is not enough: the content must still decode as UTF-8
        try:
            raw.decode("utf-8-sig")
            check(f"{os.path.basename(rel)} 内容为合法 UTF-8", True, True)
        except UnicodeDecodeError as e:
            check(f"{os.path.basename(rel)} 内容为合法 UTF-8", False, True, str(e))

for rel, why in NO_BOM_OK.items():
    p = os.path.join(ROOT, rel.replace("/", os.sep))
    if not os.path.isfile(p):
        continue
    raw = open(p, "rb").read()
    check(f"{os.path.basename(rel)} 保持无 BOM", raw.startswith(BOM), False,
          f"{rel} 多了 BOM —— {why}。本文件不应有 BOM。")

# The strongest check: actually run a PowerShell parse.
# Earlier I "verified" the syntax with PSParser::Tokenize, which tokenizes even
# broken input and reported a healthy token count while the script could not run.
# Executing the real thing is the only honest test.
print()
ps1 = os.path.join(ROOT, "tools", "preci", "preci.ps1")
if os.path.isfile(ps1):
    ps = ("$ErrorActionPreference='Continue'; "
          "try { & '%s' -Stage 1 -AllowSkip *>$null; exit $LASTEXITCODE } "
          "catch { Write-Output $_.Exception.Message; exit 99 }" % ps1.replace("'", "''"))
    r = subprocess.run(["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass",
                        "-Command", ps],
                       capture_output=True, text=True, encoding="utf-8",
                       errors="replace", timeout=900)
    out = (r.stdout or "") + (r.stderr or "")
    broken = "ParserError" in out or "Unexpected token" in out or r.returncode == 99
    check("preci.ps1 能被 PowerShell 真正执行", not broken, True,
          "ParserError —— 通常是 BOM 丢失导致中文变成乱码，字符串字面量破损。"
          f"原始输出：{out.strip()[:200]}")
    if not broken:
        print(f"        退出码 {r.returncode}"
              + ("（1 = 有检查失败，对脚本本身是正常的）" if r.returncode == 1 else ""))

print(f"\n  {passed}/{passed + failed} 符合预期")
sys.exit(0 if failed == 0 else 1)
