#!/usr/bin/env python3
"""check_module_ids 的负例矩阵。

一个只会通过的检查没有价值。每个负例改坏一处「模块身份」，断言检查必须变红。
基线用**仓库真实内容**（任务书 §3.6 的表 + src/modules/ 下的 17 个骨架），
所以它同时也在核对「当前仓库确实一致」。
"""
import importlib.util
import os
import subprocess
import sys

import _paths as P  # noqa: E402  (shared location-independent paths)

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
ROOT = P.ROOT

spec = importlib.util.spec_from_file_location(
    "cmi", os.path.join(ROOT, "tools", "preci", "check_module_ids.py"))
cmi = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cmi)


def load_tree():
    """按 check_module_ids.main() 的口径取 HEAD 内容：key 一律用正斜杠。"""
    names = subprocess.run(["git", "ls-tree", "-r", "-z", "--name-only", "HEAD"],
                           cwd=ROOT, capture_output=True).stdout.decode("utf-8")
    tree = {}
    for n in [x for x in names.split("\0") if x]:
        if not (n == cmi.TASKBOOK or n.startswith(cmi.MODULES_PREFIX)):
            continue
        blob = subprocess.run(["git", "cat-file", "blob", f"HEAD:{n}"], cwd=ROOT,
                              capture_output=True)
        if blob.returncode == 0:
            tree[n] = blob.stdout
    return tree


REAL = load_tree()
if not REAL:
    print("SKIP: HEAD 里没有任务书或 src/modules/（先提交再跑）")
    sys.exit(3)


def run(tree):
    errors = []
    cmi.check(tree, errors)
    return [e for e in errors if e[0] == "ERROR"]


def with_module(suffix, mutate):
    t = dict(REAL)
    key = f"{cmi.MODULES_PREFIX}{suffix}/module.cpp"
    t[key] = mutate(t[key].decode("utf-8")).encode("utf-8")
    return t


CASES = [
    ("基线（仓库真实内容）", REAL, 0),

    # ── 真错误：身份对不上，必须抓到 ─────────────────────────────────────────
    ("模块 ID 写错一个字",
     with_module("transport", lambda s: s.replace('"mod.transport"', '"mod.transportt"')), 1),
    ("模块 ID 换成了别人的",
     with_module("crypto", lambda s: s.replace('"mod.crypto"', '"mod.session_core"')), 1),
    ("kModuleId 常量被删掉",
     with_module("pvp", lambda s: s.replace('kModuleId = "mod.pvp"', 'kModuleId = ""')), 1),
    ("创建符号命名不符契约",
     with_module("relay", lambda s: s.replace("bbcoop_create_mod_relay",
                                              "bbcoop_create_relay")), 1),
    ("整个模块目录缺失",
     {k: v for k, v in REAL.items()
      if not k.startswith(f"{cmi.MODULES_PREFIX}death/")}, 1),
    ("目录在但没有 module.cpp",
     {k: v for k, v in REAL.items()
      if k != f"{cmi.MODULES_PREFIX}loot/module.cpp"}, 1),
    ("自己发明一个表里没有的模块",
     {**REAL, f"{cmi.MODULES_PREFIX}pvp_ranked/module.cpp":
      b'constexpr const char* kModuleId = "mod.pvp_ranked";\n'
      b"bbcoop::IModule* bbcoop_create_mod_pvp_ranked() noexcept { return nullptr; }\n"}, 1),
    ("任务书里找不到 §3.6 的表",
     {**REAL, cmi.TASKBOOK: b"# 没有表的任务书\n"}, 1),

    # ── 假阳性防线：注释里提到模块名不算违规 ────────────────────────────────
    ("注释里提到别的模块 ID（不属于本模块的 ID 声明）",
     with_module("save", lambda s: s.replace(
         "#include <cstdint>",
         "// 注意：mod.loot 的库存同步与 mod.save 的存档写入是两件事\n#include <cstdint>")), 0),
]

passed = failed = 0
for name, tree, expect in CASES:
    errs = run(tree)
    ok = (len(errs) == 0) if expect == 0 else (len(errs) >= expect)
    if ok:
        passed += 1
        print(f"  [OK ] {name:38} errors={len(errs)}")
    else:
        failed += 1
        print(f"  [BAD] {name:38} 期望{'=0' if expect == 0 else '>=1'} 实际 {len(errs)}")
        for lvl, f, l, m in errs[:2]:
            print("         " + m[:130])

print(f"\n{passed}/{len(CASES)} 符合预期")
sys.exit(0 if failed == 0 else 1)
