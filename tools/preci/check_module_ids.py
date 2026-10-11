#!/usr/bin/env python3
"""模块身份检查：src/modules/ 的骨架必须与任务书 §3.6 注册表对得上。

为什么需要这个检查：
  * 任务书 §3.6 明写「**本节是唯一来源**：config/modules.yaml、docs/modules/*.md 与
    branch-policy 的 SCOPE 均以此表为准」；
  * 模块 ID 是身份：自检输出、日志过滤、配置（T-008）三处都按它匹配，
    错一个字就是「模块存在但谁也找不到它」；
  * 17 个模块 × 每个一个 ID，靠人眼逐字核对不可靠 —— 而这类错误没有任何运行时症状。

检查四件事：
  1. §3.6 表里的每个模块 ID，在 src/modules/ 下都必须有一个骨架，且 ID 逐字一致；
  2. 不允许出现表里没有的模块 ID（不能自己造模块）；
  3. 创建符号的名字必须是 `bbcoop_create_mod_<ID 去掉 mod. 前缀>`
     （固定命名，T-008/T-009 生成的模块表按它取地址）；
  4. 表里没有的目录也报出来（避免留下没人管的空目录）。

范围说明：本检查管**身份与存在性**，不管实现内容（那是各模块作者的事）。
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))

sys.stdout.reconfigure(encoding="utf-8", errors="replace")

TASKBOOK = "bloodborne.markdown"
MODULES_PREFIX = "src/modules/"

# §3.6 的表行形如：
#   | Layer 0 | `mod.transport` | 传输 | UDP/TCP/Relay 传输、连接生命周期 |
#   | 贯穿性 | `mod.observability` | 日志 / 指标 | 按需接入 Kernel 或各层 |
TABLE_ROW = re.compile(r"^\|\s*(Layer [0-9]|贯穿性)\s*\|\s*`(mod\.[a-z0-9_]+)`\s*\|")

# module.cpp 里的 ID 常量与创建符号。
ID_CONST = re.compile(r'kModuleId\s*=\s*"(mod\.[a-z0-9_]+)"')
CREATE_FN = re.compile(r"bbcoop::IModule\*\s+(bbcoop_create_mod_[a-z0-9_]+)\s*\(")


def expected_ids(taskbook_text: str):
    """从 §3.6 的表里取模块 ID（按出现顺序，去重）。"""
    ids = []
    for line in taskbook_text.split("\n"):
        m = TABLE_ROW.match(line)
        if m:
            ids.append(m.group(2))
    seen = set()
    unique = []
    for i in ids:
        if i not in seen:
            seen.add(i)
            unique.append(i)
    return unique


def check(tree, errors):
    """tree: {路径: bytes}。返回 (期望数, 实际骨架数, 备注列表)。"""
    notes = []

    if TASKBOOK not in tree:
        errors.append(("ERROR", TASKBOOK, 0, "读不到任务书，无法核对 §3.6 注册表"))
        return 0, 0, notes
    expected = expected_ids(tree[TASKBOOK].decode("utf-8", "replace"))
    if not expected:
        errors.append(("ERROR", TASKBOOK, 0,
                       "在任务书里没找到 §3.6 的模块表（表的唯一来源）—— 表格式变了？"))
        return 0, 0, notes

    # 收集 src/modules/ 下的骨架：每个目录的 module.cpp。
    found = {}          # 目录 -> (声明的 ID, 创建符号名)
    dirs = set()
    for name, blob in tree.items():
        if not name.startswith(MODULES_PREFIX):
            continue
        rel = name[len(MODULES_PREFIX):]
        parts = rel.split("/")
        if len(parts) < 2:
            continue
        dirs.add(parts[0])
        if parts[1] != "module.cpp":
            continue
        text = blob.decode("utf-8", "replace")
        idm = ID_CONST.search(text)
        fnm = CREATE_FN.search(text)
        found[parts[0]] = (idm.group(1) if idm else None,
                           fnm.group(1) if fnm else None)

    # 1) 表里的每个 ID 都要有骨架，且 ID 逐字一致
    for mid in expected:
        suffix = mid[len("mod."):]
        if suffix not in dirs:
            errors.append(("ERROR", f"{MODULES_PREFIX}{suffix}/", 0,
                           f"§3.6 注册表里的 `{mid}` 没有骨架目录 —— "
                           f"缺模块意味着自检与配置里少一项能力"))
            continue
        if suffix not in found:
            errors.append(("ERROR", f"{MODULES_PREFIX}{suffix}/", 0,
                           f"`{mid}` 的目录里没有 module.cpp（骨架不完整）"))
            continue
        declared, create_fn = found[suffix]
        if declared is None:
            errors.append(("ERROR", f"{MODULES_PREFIX}{suffix}/module.cpp", 0,
                           f"没有找到 kModuleId 常量 —— 模块 ID 必须能在源码里核对"))
            continue
        if declared != mid:
            errors.append(("ERROR", f"{MODULES_PREFIX}{suffix}/module.cpp", 0,
                           f"模块 ID 与任务书 §3.6 不一致：源码写的是 `{declared}`，"
                           f"注册表是 `{mid}`（ID 是身份，错一个字模块就找不到了）"))
        # 3) 创建符号的固定命名
        want_fn = "bbcoop_create_mod_" + suffix
        if create_fn is None:
            errors.append(("ERROR", f"{MODULES_PREFIX}{suffix}/module.cpp", 0,
                           "没有找到 bbcoop_create_mod_* 创建符号"))
        elif create_fn != want_fn:
            errors.append(("ERROR", f"{MODULES_PREFIX}{suffix}/module.cpp", 0,
                           f"创建符号命名不符契约：写的是 `{create_fn}`，应为 `{want_fn}`"
                           f"（T-008/T-009 生成的模块表按这个名字取地址）"))

    # 2) 不允许表里没有的模块 ID
    expected_suffixes = {m[len("mod."):] for m in expected}
    for suffix, (declared, _) in sorted(found.items()):
        if suffix not in expected_suffixes:
            errors.append(("ERROR", f"{MODULES_PREFIX}{suffix}/module.cpp", 0,
                           f"模块 `{declared}` 不在任务书 §3.6 注册表里 —— "
                           f"不得自行发明模块（§3.6 是唯一来源）"))

    # 4) 目录里有 README 但没有 module.cpp：留下没人管的空目录
    for suffix in sorted(dirs - set(found)):
        errors.append(("WARN", f"{MODULES_PREFIX}{suffix}/", 0,
                       "目录存在但没有 module.cpp —— 骨架没落地或已被删除"))

    notes.append(f"§3.6 注册表 {len(expected)} 个模块；"
                 f"找到 {len(found)} 个骨架、{len(dirs)} 个目录")
    return len(expected), len(found), notes


def main() -> int:
    import subprocess

    tree = subprocess.run(["git", "ls-tree", "-r", "-z", "--name-only", "HEAD"],
                          cwd=ROOT, capture_output=True)
    if tree.returncode != 0:
        print("SKIP: 不是 git 仓库")
        return 3
    names = [x for x in tree.stdout.decode("utf-8", "replace").split("\0") if x]
    blobs = {}
    for n in names:
        if not (n == TASKBOOK or n.startswith(MODULES_PREFIX)):
            continue
        b = subprocess.run(["git", "cat-file", "blob", f"HEAD:{n}"], cwd=ROOT,
                           capture_output=True)
        if b.returncode == 0:
            blobs[n] = b.stdout
    if not blobs:
        print("SKIP: HEAD 里没有任务书或 src/modules/")
        return 3

    errors = []
    exp, got, notes = check(blobs, errors)

    print("=" * 68)
    print("  模块身份检查（src/modules/ 骨架 vs 任务书 §3.6 注册表）")
    print("=" * 68)
    for n in notes:
        print(f"  {n}")
    hard = [e for e in errors if e[0] == "ERROR"]
    soft = [e for e in errors if e[0] != "ERROR"]
    for lvl, name, line, msg in hard + soft:
        print(f"  [{lvl}] {name}: {msg}")
    if hard:
        print(f"\n  发现 {len(hard)} 处不一致。§3.6 是模块身份的唯一来源，"
              f"不要改源码去迁就一个手写的 ID。")
        return 1
    print("  ✅ 模块身份与 §3.6 注册表一致")
    return 0


if __name__ == "__main__":
    sys.exit(main())
