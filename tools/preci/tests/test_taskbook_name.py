#!/usr/bin/env python3
"""Guard the task book's filename so a rename cannot silently break references.

Why this exists: the task book was named `bloodbrone.markdown` -- missing the "o" in
bloodborne. Fixing it meant updating 74 references across 42 files, including
CI gates that key off the exact path:

    .github/workflows/docs-structure.yml   required=( ... "bloodborne.markdown" ... )
    .github/workflows/branch-policy.yml    [lead]="... bloodborne.markdown ..."
    .github/CODEOWNERS                     /bloodborne.markdown
    .gitattributes                         bloodborne.markdown linguist-documentation

If someone renames the file again and misses ONE of those, the failure mode is
silent: the structure gate would report "missing required file", or the ownership
gate would treat the task book as out of scope, and nobody would connect that to
a rename.

This suite makes the coupling explicit: it derives the real filename from disk and
asserts every gate references it.

It also reports dangling references to the OLD misspelled name -- those would be
broken links in documentation.
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _paths as P  # noqa: E402

ROOT = P.ROOT
OLD_NAME = "bloodbrone.markdown"   # the historical misspelling
CANONICAL = "bloodborne.markdown"

# Every place that keys off the exact path. Missing any of these is a silent break.
GATES = [
    (".github/workflows/docs-structure.yml", "结构门禁的必需文件清单"),
    (".github/workflows/branch-policy.yml", "写入范围 SCOPE"),
    (".github/CODEOWNERS", "CODEOWNERS 归属"),
    (".gitattributes", "Git 属性（文档标记）"),
]

passed = failed = 0
print("=" * 68)
print("  任务书文件名与门禁引用一致性")
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


# 1) the file exists under the correct spelling
exists = os.path.isfile(os.path.join(ROOT, CANONICAL))
check("任务书存在且拼写正确", exists, True,
      f"{CANONICAL} 不存在 —— 若已改名，请同步更新本套件的 CANONICAL 与所有门禁引用")

# 2) no file left under the misspelling
old_exists = os.path.isfile(os.path.join(ROOT, OLD_NAME))
check("旧的错误拼写文件已不存在", old_exists, False,
      f"{OLD_NAME} 仍在 —— 可能是改名遗漏，会出现两份任务书")

# 3) every gate references the canonical name
for rel, what in GATES:
    p = os.path.join(ROOT, rel.replace("/", os.sep))
    if not os.path.isfile(p):
        check(f"{what} 引用正确名", "文件不存在", CANONICAL)
        continue
    t = open(p, encoding="utf-8").read()
    has_new = CANONICAL in t
    has_old = OLD_NAME in t
    if has_old:
        check(f"{what} 无旧拼写残留", True, False,
              f"{rel} 里仍有 '{OLD_NAME}' —— 门禁会指向一个不存在的文件")
    else:
        check(f"{what} 引用正确名", has_new, True,
              f"{rel} 里找不到 '{CANONICAL}'")

# 4) no tracked file still links to the old misspelled name
#    (excluding this suite, which mentions it on purpose)
import subprocess  # noqa: E402
r = subprocess.run(["git", "-C", ROOT, "ls-files"], capture_output=True,
                   text=True, encoding="utf-8", errors="replace")
dangling = []
for f in (r.stdout or "").split("\n"):
    f = f.strip()
    if not f or f.endswith("test_taskbook_name.py"):
        continue
    p = os.path.join(ROOT, f.replace("/", os.sep))
    if not os.path.isfile(p):
        continue
    try:
        t = open(p, encoding="utf-8").read()
    except (UnicodeDecodeError, OSError):
        continue
    if OLD_NAME in t:
        dangling.append(f)

if dangling:
    # not an error: the remaining owners may not have synced yet. Report so it is
    # visible rather than silent.
    print(f"  [INFO] {len(dangling)} 个文件仍引用旧拼写（应由各自所有者同步）：")
    for f in dangling:
        print(f"        {f}")
else:
    print("  [OK ] 全仓库无旧拼写残留")

print(f"\n  {passed}/{passed + failed} 符合预期")
sys.exit(0 if failed == 0 else 1)
