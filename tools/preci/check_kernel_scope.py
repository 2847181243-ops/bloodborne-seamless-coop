#!/usr/bin/env python3
"""Kernel 不得实现任何游戏机制 —— 把 01-kernel.md §5 的「人工审计」变成可判定的检查。

出处：
  * 任务书 §3.6 原则 1：Kernel「不含任何业务逻辑」；
  * docs/architecture/01-kernel.md §3.1：不实现死亡/重生、世界重置、敌人与 Boss、
    掉落与库存、PvP 结算、存档写入 —— 它们分别属于 mod.death / mod.world_reset /
    mod.enemy_boss / mod.loot / mod.pvp / mod.save；
  * 01-kernel.md §5 的验收表：判定方式写着「人工审计结论（审计方式与判据由 **T-006** 落地）」
    —— 本文件就是那份判据。

⚠️ 这是**启发式**检查，不是证明。它只能发现「明显的越界」：
   在 src/kernel/ 的代码里出现游戏机制的标识符。它拦不住：
     * 换个名字实现同一件事（`void f()` 里写死亡逻辑）；
     * 通过服务查找拿到别的模块的玩法能力再做业务判断。
   所以它**与人工评审并存**，不能对外的说法是「Kernel 无业务逻辑已被证明」。

做法：
  1. 只扫 `src/kernel/` 下已提交的源码（`git ls-tree HEAD`，与 check_style.py 同一口径：
     看的是 HEAD 里的内容，不是工作树 —— 所以要先 commit 才生效）；
  2. **先剥掉注释与字符串字面量**再匹配。理由：Kernel 的注释里恰恰写着
     「不实现死亡 / 世界重置 / 敌人 …」（就是在声明不做），把注释算进去会把
     正确的声明当成违规 —— 这种「自指」假阳性正是本仓库踩过的坑；
  3. 匹配用**子串**（大小写不敏感），不要求标识符边界。这一条是被负例矩阵逼出来的：
     第一版写了「前后都不能是标识符字符」，结果 `respawnPlayer` / `bossHp` /
     `grantLoot` / `inventorySize` **全部漏掉** —— 而那正是真实代码里最常见的命名方式。
     之所以敢用子串：下面这些词没有任何一个会出现在 Kernel 的正当标识符里
     （`Health` / `tick` / `logger` / `module` / `channel` 都不含它们）。
     ⚠️ 将来若要往 FORBIDDEN 里加词，先确认它在 Kernel 的正当用词里不出现；
     确实需要放过某些用法时，用 ALLOWED 显式登记，而不是把词删掉让检查变松。

退出码：0 通过；1 有越界；3 无法检查（例如不是 git 仓库）。
"""
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))

sys.stdout.reconfigure(encoding="utf-8", errors="replace")

# 只扫 Kernel 自己的代码。
SCAN_PREFIX = "src/kernel/"

# 游戏机制的标识符。列表刻意保守：只放**一眼就是某个模块的职责**的词，
# 不放有歧义的（例如 hp / health / session 在 Kernel 里都有正当含义）。
FORBIDDEN = [
    "respawn",
    "blood_echo",
    "bloodecho",
    "lantern",
    "world_reset",
    "boss",
    "enemy",
    "loot",
    "inventory",
    "pvp",
    "invasion",
    "savegame",
    "save_file",
    "savefile",
    "estus",
    "insight",
    "chalice",
    "consumable",
    "death_penalty",
    "duplicate_item",
    "item_grant",
]

# 白名单：允许出现的正当用法（全词匹配，大小写不敏感）。
#
# 目前为空 —— Kernel 里不该出现上面任何一个词。留这个常量是为了让「将来确实需要
# 放过某个词」时有一个**显式**的位置，而不是去改 FORBIDDEN 让检查变松。
ALLOWED: list[str] = []


def strip_comments_and_strings(text: str) -> str:
    """把注释与字符串字面量替换成空格，保留换行以便报行号。

    只需要处理 C++ 的 //、/* */ 与 "..."、'...'。不追求完美的词法分析：
    目的是避免「注释里声明不做某事」被判成违规。
    """
    out = []
    i = 0
    n = len(text)
    while i < n:
        ch = text[i]
        nxt = text[i + 1] if i + 1 < n else ""
        if ch == "/" and nxt == "/":
            while i < n and text[i] != "\n":
                i += 1
            continue
        if ch == "/" and nxt == "*":
            i += 2
            while i + 1 < n and not (text[i] == "*" and text[i + 1] == "/"):
                out.append("\n" if text[i] == "\n" else " ")
                i += 1
            i += 2
            continue
        if ch in ('"', "'"):
            quote = ch
            i += 1
            while i < n and text[i] != quote:
                if text[i] == "\\" and i + 1 < n:
                    i += 2
                    continue
                out.append("\n" if text[i] == "\n" else " ")
                i += 1
            i += 1
            out.append(" ")
            continue
        out.append(ch)
        i += 1
    return "".join(out)


def committed_files():
    """返回 HEAD 里 src/kernel/ 下的 (路径, 内容)。"""
    tree = subprocess.run(["git", "ls-tree", "-r", "-z", "--name-only", "HEAD"],
                          cwd=ROOT, capture_output=True)
    if tree.returncode != 0:
        return None
    names = [x for x in tree.stdout.decode("utf-8", "replace").split("\0") if x]
    files = []
    for name in names:
        if not name.startswith(SCAN_PREFIX):
            continue
        if not re.search(r"\.(c|cc|cpp|cxx|h|hh|hpp|hxx)$", name):
            continue
        blob = subprocess.run(["git", "cat-file", "blob", f"HEAD:{name}"],
                              cwd=ROOT, capture_output=True)
        if blob.returncode != 0:
            continue
        files.append((name, blob.stdout.decode("utf-8", "replace")))
    return files


def check(files, errors):
    # 子串匹配（大小写不敏感）—— 理由见文件头第 3 条：要求标识符边界会漏掉
    # respawnPlayer / bossHp / grantLoot 这类真实命名，而那正是越界最可能出现的形式。
    patterns = [(w, re.compile(re.escape(w), re.IGNORECASE)) for w in FORBIDDEN]
    allowed = [re.compile(re.escape(w), re.IGNORECASE) for w in ALLOWED]

    scanned = 0
    for name, raw in files:
        # 范围在这里也守一道：committed_files() 已经只取 src/kernel/，
        # 但 check() 是个纯函数，将来被别处用更宽的清单调用时不该误报别的模块。
        # （负例矩阵里有一条就是拿 src/modules/enemy_boss/ 的文件来试这一点。）
        if not name.startswith(SCAN_PREFIX):
            continue
        code = strip_comments_and_strings(raw)
        if not code.strip():
            continue
        scanned += 1
        for lineno, line in enumerate(code.split("\n"), 1):
            for word, pat in patterns:
                if not pat.search(line):
                    continue
                if any(ap.search(line) for ap in allowed):
                    continue
                errors.append(("ERROR", name, lineno,
                               f"Kernel 里出现了游戏机制的标识符 '{word}' —— "
                               f"它属于某个模块，不属于 Kernel（任务书 §3.6 原则 1 / "
                               f"01-kernel.md §3.1）"))
    return scanned


def main() -> int:
    files = committed_files()
    if files is None:
        print("SKIP: 不是 git 仓库，无法读取 HEAD")
        return 3
    if not files:
        print("SKIP: HEAD 里没有 src/kernel/ 下的源码")
        return 3

    errors = []
    scanned = check(files, errors)

    print("=" * 68)
    print("  Kernel 边界检查（Kernel 内不得有游戏机制）")
    print("=" * 68)
    print(f"  扫了 {scanned} 个文件（注释与字符串已剥离；只看 HEAD 里的内容）")
    print(f"  违禁标识符 {len(FORBIDDEN)} 个；白名单 {len(ALLOWED)} 条")
    if errors:
        print()
        for level, name, line, msg in errors:
            print(f"  [{level}] {name}:{line}: {msg}")
        print(f"\n  发现 {len(errors)} 处越界 —— Kernel 不含业务逻辑是任务书 §3.6 原则 1 的硬要求。")
        print("  这些机制属于哪个模块见任务书 §3.6 注册表；不要上收到 Kernel。")
        return 1
    print("  ✅ 没有发现游戏机制的标识符")
    print("  ⚠️ 这是启发式检查，不是证明：换个名字实现同一件事它查不出来，")
    print("     所以要与人评审并存（01-kernel.md §5）。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
