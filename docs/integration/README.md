# docs/integration/ —— 集成测试报告

**所有者：AI-00（Integration Lead）**

> **不得以“所有分支都能编译”为集成完成标准。**
> `Build Passed ≠ Feature Complete`

---

## 1. 集成检查清单（任务书 §51）

每次集成必须逐项检查：

```text
[ ] API / ABI 兼容性
[ ] Hook 冲突
[ ] 网络协议一致性
[ ] 状态机一致性
[ ] Death / Respawn 流程
[ ] Lantern Reset
[ ] Boss 流程
[ ] PvE / PvP Scaling 隔离
[ ] Invasion 流程
[ ] Save / Reward
[ ] Loot / Consumable / Shop / NPC
[ ] Session State
[ ] Blacklist / Anticheat
[ ] Arena
[ ] 区域切换
[ ] 独立存档
[ ] 数据持久化
[ ] 安全审计通过
```

任何一项未通过 → 集成**未完成**。

---

## 2. 报告模板

```markdown
# 集成报告：<版本 / 迭代>

- 版本号：<x.y.z>
- 集成日期：<YYYY-MM-DD>
- 集成人：AI-00
- 参与 Agent：AI-A0 … AI-C4
- 合并的 PR：#… #…
- 结论：集成通过 / 集成受阻

## 1. 合并内容

| PR | Agent | 内容 | CI | 审计 |
|---|---|---|---|---|

## 2. 集成检查清单结果

| 项目 | 结果 | 证据 |
|---|---|---|

## 3. 接口契约一致性

| 契约 | 状态 | 偏差说明 |
|---|---|---|

## 4. 验证等级

| 功能 | Compile | Runtime | Multiplayer | Regression | Feature |
|---|---|---|---|---|---|

## 5. 冲突与裁决

| 冲突 | 类型 | 裁决方 | 结论 | 记录 |
|---|---|---|---|---|

## 6. 阻塞项

## 7. 下一迭代输入
```

---

## 3. 验证等级（任务书 §39）

```text
Compile Verified → Runtime Verified → Multiplayer Verified → Regression Verified → Feature Verified
```

- 不允许跳级声明；
- “多人验证”必须在**真实运行时 + 至少 2P 实测**下完成；
- 用日志输出冒充网络同步、用 Mock 冒充真实功能，一律判为未验证。

---

## 4. 阶段验收对照

最终验收标准以任务书 §40（第一阶段）与 §53（各 Agent 验收）为准，逐条勾选并存档于本目录。
