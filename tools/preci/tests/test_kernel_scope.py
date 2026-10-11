#!/usr/bin/env python3
"""check_kernel_scope 的负例矩阵。

一个只会通过的检查没有价值。每个负例注入一处「Kernel 里实现游戏机制」的痕迹，
断言检查必须变红；同时**也要**断言两类假阳性不会发生：

  * Kernel 的注释里写着「不实现死亡 / 世界重置 / 敌人 …」（那是在声明不做），
    剥掉注释之后不该被判违规；
  * `Health` / `tick` / `logger` 这些词在 Kernel 里有正当含义，不能被误伤。
"""
import importlib.util
import os
import sys

import _paths as P  # noqa: E402  (shared location-independent paths)

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
ROOT = P.ROOT

spec = importlib.util.spec_from_file_location(
    "cks", os.path.join(ROOT, "tools", "preci", "check_kernel_scope.py"))
cks = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cks)


def run(files):
    errors = []
    cks.check(files, errors)
    return errors


CLEAN = """// Kernel：模块宿主。
// 本文件不实现任何游戏机制 —— 死亡 / 世界重置 / 敌人 / 掉落 / PvP / 存档
// 分别属于各自的模块（任务书 §3.6 注册表）。
#include "bbcoop/kernel/kernel.h"

namespace bbcoop::kernel {
Health Kernel::refreshHealth(Slot& slot) noexcept {
    const char* label = "health changed";  // 字符串里出现 health 是正常的
    m_logger.log(LogLevel::Warn, slot.status.id, label);
    return slot.status.health;
}
}  // namespace bbcoop::kernel
"""

CASES = [
    ("基线（干净）", [("src/kernel/src/kernel.cpp", CLEAN)], 0),

    # ── 假阳性防线：注释与字符串里提到游戏机制，是**声明不做**，不算违规 ──
    ("注释里声明「不实现死亡」", [("src/kernel/src/x.cpp",
        "// 我们不实现 death / respawn / boss 逻辑\nint f() { return 1; }\n")], 0),
    ("块注释里提到世界重置", [("src/kernel/src/x.cpp",
        "/* world_reset 与 loot 属别的模块 */\nint f() { return 1; }\n")], 0),
    ("字符串字面量里提到", [("src/kernel/src/x.cpp",
        'const char* kWhy = "not implemented: loot, pvp, savefile";\n')], 0),
    ("Kernel 正当用词不被误伤", [("src/kernel/src/x.cpp",
        "bool ok = module->health() != Health::Unavailable;\n"
        "logger.log(LogLevel::Info, \"mod.x\", \"ticked\");\n")], 0),

    # ── 真越界：代码里出现游戏机制的标识符，必须抓到 ──
    ("代码里出现 respawn", [("src/kernel/src/x.cpp",
        "void f() { respawnPlayer(); }\n")], 1),
    ("代码里出现 boss", [("src/kernel/src/x.cpp",
        "int bossHp = 100;\n")], 1),
    ("代码里出现 loot", [("src/kernel/src/x.cpp",
        "void grantLoot() {}\n")], 1),
    ("代码里出现 inventory", [("src/kernel/src/host/main.cpp",
        "std::size_t inventorySize = 0;\n")], 1),
    ("大小写混写的 World_Reset", [("src/kernel/src/x.cpp",
        "void World_Reset() {}\n")], 1),
    ("头文件里出现 savefile", [("src/kernel/include/bbcoop/kernel/x.h",
        "void writeSavefile();\n")], 1),
    ("注释干净但代码越界（两种混在一起）", [("src/kernel/src/x.cpp",
        "// 不实现 pvp\nvoid pvpResolve() {}\n")], 1),

    # ── 扫描范围：非 src/kernel/ 的文件不归它管 ──
    ("模块目录里的 boss 不算 Kernel 越界", [("src/modules/enemy_boss/module.cpp",
        "void bossTick() {}\n")], 0),
]

passed = failed = 0
for name, tree, expect in CASES:
    errs = run(tree)
    ok = (len(errs) == 0) if expect == 0 else (len(errs) >= expect)
    if ok:
        passed += 1
        print(f"  [OK ] {name:34} errors={len(errs)}")
    else:
        failed += 1
        print(f"  [BAD] {name:34} 期望{'=0' if expect == 0 else '>=' + str(expect)} 实际 {len(errs)}")
        for s, f, l, m in errs[:2]:
            print("         " + m[:150])

print(f"\n{passed}/{len(CASES)} 符合预期")
sys.exit(0 if failed == 0 else 1)
