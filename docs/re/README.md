# docs/re/ —— 逆向工程文档

**所有者：AI-A1（Senior Game Reverse Engineer + Native C++ Engineer）**
**写入范围：`docs/re/` 与 `mod/core/`（其他 Agent 只读）**

---

## 1. 必填文件清单

```text
docs/re/
├── player.md           玩家状态 / 属性 / 动画
├── death.md            死亡判定 / 死亡流程 / 重生
├── blood_echoes.md     掉落 / 回收 / 敌人持有 / 再次死亡
├── enemy.md            普通敌人 HP / AI / 死亡 / 刷新
├── boss.md             Boss HP / Phase / Aggro / Target / 状态机
├── world.md            区域加载 / 世界状态 / World Reset
├── lantern.md          Lantern Rest / 重生点 / 敌人刷新
├── multiplayer.md      原版多人机制 / 召唤 / 入侵 / 雾门
├── arena.md            决斗场（血之同胞 / 青骑士）
├── loot.md             随机掉落 / 固定拾取 / 宝箱 / Boss 掉落 / 唯一物品
├── consumable.md       消耗品 / 捏魂类 / 回复增益类
├── shop.md             商店 / 商人解锁 / 购买扣费
├── npc.md              友好 NPC 识别 / 伤害判定 / 敌对触发
├── addresses.md        函数地址与模块布局
├── structures.md       结构体布局与偏移
└── findings.md         汇总：结论 / 待确认项 / 证据索引
```

`docs/re/` 之外的 `functions.md`（任务书 §5.3 提及）统一并入 `addresses.md`，避免两份地址表不同步。

---

## 2. 每个关键函数的记录格式

```markdown
### <FunctionName>

| 字段 | 值 |
|---|---|
| Function | `0x0000000000000000` / `sub_XXXXXX` |
| Address | 基址 + 偏移（注明模块） |
| Purpose | 一句话说明 |
| Arguments | 类型 / 含义 / 单位 |
| Return | 类型 / 含义 |
| Callers | 调用方列表 |
| Called Functions | 被调用列表 |
| Evidence | 反汇编片段 / 交叉引用 / 运行时日志 / 行为实验 |
| Confidence | Confirmed / Probable / Hypothesis / Unknown |
| Related Feature | 关联功能（死亡 / Lantern / Boss / Loot …） |
```

**版本绑定（强制）**

```text
游戏版本：1.09（CUSA03173）
平台：bbport Windows 原生版
Mod 版本：<x.y.z>
Signature：<bytes>
```

---

## 2.1 机器校验的规则（写之前先看，否则门禁会红）

本地预检会检查每个 `### <FunctionName>` 记录。**只查机器能判断的东西**，
不假装能判断结论对不对。规则如下：

| 规则 | 检查什么 | 为什么值得机器查 |
|---|---|---|
| **R-1** | `Confidence` 字段存在，且取值是四个之一 | 不写等级 ⇒ 无法区分"确认过"与"猜的"。这是最重要的一条 |
| **R-2** | `Evidence` 字段存在且**非空** | `Evidence` 空白 + `Confidence: Confirmed` 是自相矛盾的 |
| **R-3** | 若 `Confidence` 是 `Confirmed`，`Evidence` 必须提到四类证据之一：`反汇编` / `交叉引用` / `运行时` / `实验` | 堵住"没证据却敢写已确认" |
| **R-4** | 文档里至少有**一处**版本绑定块（四项齐全） | 地址不绑版本就等于没记 —— 游戏一更新全部作废且无人知道 |

**明确不查的**（避免产生"门禁绿了就等于对"的错觉）：

- ❌ 地址**是否真的是**那个函数 —— 机器无法判断，只能靠运行时验证
- ❌ `Purpose` 写得对不对
- ❌ `Evidence` 里贴的反汇编是不是真的
- ❌ `Signature` 是否与 1.09 匹配

> ⚠️ **这四条规则是"格式与自洽性"检查，不是"正确性"检查。**
> 它能让"漏写等级"和"没证据却声明已确认"变成可见的、有位置的问题；
> 它**不能**代替运行时验证。见 `docs/standards/bypass-and-gates.md` 第 1 节。

**写法的正反例**

```markdown
<!-- ✅ 合格：等级 + 具体证据 -->
| Evidence | 反汇编 0x1400A2B10 起 40 字节，见下方片段；运行时在 Boss 死亡时断点命中 3 次 |
| Confidence | Confirmed |

<!-- ❌ 不合格（R-3）：声明已确认但证据里没有任何一类可核对的证据 -->
| Evidence | 看起来是这样 |
| Confidence | Confirmed |

<!-- ❌ 不合格（R-1）：没写等级 -->
| Evidence | 反汇编片段 |
```

**如果确实还没确认**，就老实写 `Hypothesis` 或 `Unknown` ——
**标 `Unknown` 不丢人，把猜测写成事实才是。**（第 5 节）


---

## 3. AI-A1 优先级

```text
Priority 1: bbport runtime 结构 / 游戏进程内存布局
Priority 2: Player / Death / Lantern / World
Priority 3: Enemy / Boss / Session
Priority 4: Loot / Consumable / Shop / NPC
Priority 5: Arena / PvP / Save / 区域切换
```

---

## 4. 必须优先回答的原版行为问题

这些是 AI-A3 / AI-A4 能否正确实现的前提，**未确认前不许自定义规则**：

**死亡经济**

- [ ] Blood Echoes 掉落 / 回收 / 敌人持有的原版规则
- [ ] 再次死亡时上一次掉落是否消失
- [ ] World Reset 是否清除地面上的 Blood Echoes
- [ ] PvP 死亡时 Blood Echoes 的掉落时机
- [ ] 原版入侵结算规则

**物品**

- [ ] 随机掉落归属规则
- [ ] 固定拾取点（光点 / 尸体 / 宝箱）的多人行为
- [ ] Boss 掉落多人分配方式
- [ ] 物品唯一性标志的识别方式
- [ ] 消耗品使用行为、捏魂类效果
- [ ] 商人交互 / 购买扣费 / 解锁状态
- [ ] NPC 伤害判定与敌对触发、友好 NPC 识别

**决斗场**

- [ ] 原版决斗场函数 / 地址 / 匹配逻辑 / 奖励 / 誓约计分
- [ ] 血之同胞失败扣分（-1 净胜）的实现，以及改为“失败无损失”的 Hook 方案
- [ ] 是否适用入侵者 HP -30%

**世界**

- [ ] 原版区域加载 / 雾门逻辑，雾门移除是否可行
- [ ] 原版 Boss 战死亡机制
- [ ] 存档结构（`GMsave\Bloodborne\bbport\user\savedata\1\CUSA01363\SPRJ0005`）

---

## 5. 待确认项的写法

不确定的结论**不要删掉**，按以下格式留在文档里并建 Issue：

```markdown
> **Unknown** —— 原版决斗场匹配逻辑未确认。
> 影响：AI-A2 无法确定是否复用原版石像触发。
> Issue: #<编号>
> 下一步：在运行时对石像交互下断点，记录调用栈。
```
