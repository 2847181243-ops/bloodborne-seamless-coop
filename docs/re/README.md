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
