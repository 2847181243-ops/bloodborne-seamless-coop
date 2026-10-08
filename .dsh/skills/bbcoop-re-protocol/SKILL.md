---
name: bbcoop-re-protocol
description: bbport Windows 原生版 Bloodborne 的逆向工程记录协议——函数/地址/结构体/Hook 的必填字段、版本绑定、置信度标记、待确认项写法与交付文件清单。当你要分析 bbport runtime、定位游戏函数、记录 Hook 点、写 docs/re/ 文档，或判断"这个地址能不能用"时加载。
---

# 逆向工程记录协议（AI-A1）

目标平台：**bbport Windows 原生移植版**，游戏 **1.09（CUSA03173）**，x86-64 原生执行，**非模拟器**。
Hook 目标：Windows 原生进程的内存布局与函数地址。不依赖 ShadPS4，不依赖 Steam。

---

## 1. 记录格式（每个关键函数都必须这样写）

````markdown
### <FunctionName>

| 字段 | 值 |
|---|---|
| Function | `sub_XXXXXX` / 符号名 |
| Address | 模块基址 + 偏移 |
| Purpose | 一句话 |
| Arguments | 类型 / 含义 / 单位 |
| Return | 类型 / 含义 |
| Callers | 调用方列表 |
| Called Functions | 被调用列表 |
| Evidence | 反汇编片段 / 交叉引用 / 运行时日志 / 行为实验 |
| Confidence | Confirmed / Probable / Hypothesis / Unknown |
| Related Feature | 死亡 / Lantern / Boss / Loot / … |
````

**版本绑定（强制）**

```text
游戏版本：1.09（CUSA03173）
平台：bbport Windows 原生版
Mod 版本：<x.y.z>
Signature：<bytes>
```

没有 Signature 的地址记录视为 `Unknown`，禁止进入 `AddressResolver`。

---

## 2. 版本安全链

```text
VersionChecker → AddressResolver → HookManager → Game Systems
```

- 版本不匹配 → **禁止**复用旧地址；
- Signature 不匹配 → **禁止**强行 Hook；
- Hook 失败 → 必须记录 `[HOOK]` / `[ERROR]` 日志；
- **不允许静默失败**；**禁止**"可能是这个地址"就直接写内存。

所有 Hook 都必须能回答：失败时会看到什么日志？调用方会怎样降级？

---

## 3. 逆向优先级

```text
P1  bbport runtime 结构 / 进程内存布局 / 模块地址范围
P2  Player / Death / Lantern / World
P3  Enemy / Boss / Session
P4  Loot / Consumable / Shop / NPC
P5  Arena / PvP / Save / 区域切换
```

P1 没结论之前不要动核心代码（任务书 §5.1：禁止一开始就大规模修改核心代码）。

---

## 4. 交付文件清单（`docs/re/`）

```text
player.md  death.md  blood_echoes.md  enemy.md  boss.md  world.md  lantern.md
multiplayer.md  arena.md  loot.md  consumable.md  shop.md  npc.md
addresses.md  structures.md  findings.md
```

函数地址统一进 `addresses.md`，**不要**另建 `functions.md`（两份地址表必然不同步）。

---

## 5. 必须先回答的原版行为问题（未确认前不许自定义规则）

**死亡经济**：Blood Echoes 掉落 / 回收 / 敌人持有规则；再次死亡时上次掉落是否消失；World Reset 是否清除地面 Blood Echoes；PvP 死亡的掉落时机；原版入侵结算规则。

**物品**：随机掉落归属；固定拾取点（光点 / 尸体 / 宝箱）的多人行为；Boss 掉落多人分配；唯一性标志识别；消耗品与捏魂类效果；商人交互与扣费；NPC 伤害判定、敌对触发、友好 NPC 识别。

**决斗场**：原版函数 / 地址 / 匹配逻辑 / 奖励 / 誓约计分；血之同胞失败扣分的实现，以及改为"失败无损失"的 Hook 方案；是否适用入侵者 HP -30%。

**世界**：区域加载与雾门逻辑、雾门移除可行性；原版 Boss 战死亡机制；存档结构。

---

## 6. 待确认项的写法（不要删掉不确定的东西）

```markdown
> **Unknown** —— 原版决斗场匹配逻辑未确认。
> 影响：AI-A2 无法确定是否复用原版石像触发。
> Issue: #<编号>
> 下一步：在运行时对石像交互下断点，记录调用栈。
```

同时建 Issue（`.github/ISSUE_TEMPLATE/re-finding.yml`），否则阻塞会丢失。

---

## 7. 参考工具链

ModEngine2（soulsmods 社区）是运行时注入库，Detours 实现函数重定向，通过 `hook_set.cpp` / `patch.cpp` 动态修改，Hook `CreateFileW` 拦截文件系统访问。

**建议**：评估是否复用其 Hook 框架；需确认与 bbport 的兼容性；可参考其扩展 / 插件架构设计 Mod 模块系统。

---

## 8. 别踩的坑

- 不要把 PS4 原版地址直接当成 bbport 地址 —— eboot 被转换成原生 x86-64 镜像，布局可能不同，必须实测确认。
- 不要把"能编译"当成逆向结论。
- 不要因为某个 Signature 在一次运行中匹配就认为它跨版本稳定。
- 日志里不得出现任何隐私数据。
