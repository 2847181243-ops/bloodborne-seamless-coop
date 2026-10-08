# 贡献指南 / 多 AI 协作规约

> 本文件是任务书 §38 / §48 / §50 / §52 的工程化落地。与任务书冲突时以任务书为准。

---

## 1. 分支模型

| 分支 | 所有者 | 用途 |
|---|---|---|
| `main` | AI-00 | 集成分支，只接受通过门禁的 PR |
| `agent/a0-platform-integration` | AI-A0 | 平台适配 |
| `agent/a1-reverse-engineering` | AI-A1 | 逆向 + Core Runtime |
| `agent/a2-session-replication` | AI-A2 | Session / Replication |
| `agent/a3-gameplay-systems` | AI-A3 | Gameplay Systems |
| `agent/a4-balance-pvp` | AI-A4 | Balance / PvP |
| `agent/b1-transport` | AI-B1 | 传输层 |
| `agent/b2-discovery` | AI-B2 | 节点发现 / 信令 |
| `agent/b3-reputation` | AI-B3 | 信誉 / 抗滥用 |
| `agent/b4-relay` | AI-B4 | 中继节点 |
| `agent/c1-cryptography` | AI-C1 | 密码学 / E2EE |
| `agent/c2-security-architecture` | AI-C2 | 安全架构 / 威胁模型 |
| `agent/c3-security-audit` | AI-C3 | 安全审计 |
| `agent/c4-penetration-test` | AI-C4 | 渗透测试 |

**规则**

1. 每个 Agent **只修改自己分支内的文件**。
2. 跨分支 / 跨所有权修改 → 必须先由 AI-00 建立 Issue（见 §4）。
3. **不允许**直接向 `main` 推送，所有改动走 PR。
4. **不允许** rebase / force-push 别人的 `agent/*` 分支。
5. AI-C1 的密码学代码不允许被其他 Agent 修改；AI-C3 的审计代码不允许被开发 Agent 修改。

CI 会在 PR 上校验分支名与改动路径是否落在该 Agent 的所有权范围内（`.github/workflows/branch-policy.yml`）。

---

## 2. 标准工作流

```text
1. 读任务书相关章节
2. 检查是否已有对应 Issue（没有就建）
3. git switch agent/<自己的分支> && git pull --ff-only
4. 小步实现 —— 一次 PR 只解决一件事
5. 本地验证（编译 / 运行 / 多人实测，按需）
6. 更新对应 docs/ 文档（逆向结论必须标证据等级）
7. push + 开 PR，套用 PR 模板，勾选自检清单
8. 等 CI + 等 review/审计 → 由 AI-00 合并到 main
```

---

## 3. 提交信息规范

```text
<scope>: <一句话说明>

为什么改 / 改了什么 / 影响哪些系统 / 如何验证
```

`<scope>` 取以下之一：

```text
a0-platform  a1-re          a2-session   a3-gameplay  a4-balance
b1-transport b2-discovery   b3-reputation b4-relay
c1-crypto    c2-security    c3-audit     c4-pentest
docs         ci             config       build
```

示例：

```text
a3-gameplay: 实现 DeathPenalty 惩罚池的叠加与清除

为什么：任务书 §12 要求单人死亡后施加 Max HP 惩罚，且仅在 Lantern Rest 时清除。
改了什么：DeathPenalty 结构加入 penalty_pool / stacking_rule / clear_condition。
影响：Player / Lantern / World Reset 三条流程。
如何验证：35.2 普通区域死亡测试矩阵全部通过；Compile + Runtime Verified。
证据等级：Confirmed（行为实验）
```

> 提交信息里**不得**出现 IP、账号 ID、硬件指纹等任何隐私数据。

---

## 4. Issue 驱动（跨 Agent 协作）

**必须建立 Issue 的情形**（任务书 §37 / §52）：

- AI-A1 无法确认关键函数、地址或原版行为；
- AI-A2 需要改变 AI-A1 已确认的核心接口；
- AI-A3 发现原版机制与假设冲突；
- 大规模修改 `Player / World / Boss / Save / 网络协议 / 死亡经济 / Inventory / Reward`；
- Crash / Deadlock / Memory Corruption / **Save Corruption** / **Duplicate Reward** / State Desync /
  Infinite Respawn / Infinite Enemy Reset；
- 消耗品复制、商店购买复制、NPC 保护范围不清、世界状态被写入客机存档、反作弊误判；
- 决斗场 Hook 方案未定、区域切换导致状态异常、独立存档损坏；
- **安全审计未通过**。

Issue 至少包含（见 Issue 模板）：

```text
Title / Background / Observed Behavior / Expected Behavior / Evidence
Affected Modules / Possible Cause / Risk / Proposed Solutions / Verification Plan
```

---

## 5. 证据等级（强制）

任何逆向结论必须标注，**禁止把猜测直接当事实写入核心逻辑**：

| 标记 | 含义 |
|---|---|
| `Confirmed` | 已通过源码、反汇编、交叉引用、运行时日志或行为实验确认 |
| `Probable` | 存在较强证据，但尚未完全确认 |
| `Hypothesis` | 当前仅为推测 |
| `Unknown` | 暂无足够证据 |

版本安全链路（任务书 §5.5）：

```text
VersionChecker → AddressResolver → HookManager → Game Systems
```

- 版本不匹配 → **禁止**使用旧地址；
- Signature 不匹配 → **禁止**强行 Hook；
- Hook 失败必须明确记录日志，**不允许静默失败**；
- **禁止**因为“可能是这个地址”就直接写入内存。

---

## 6. 修改代码时必须回答的五个问题

1. 改了什么？
2. 为什么改？
3. 修改了哪个模块？
4. 影响哪些系统？
5. 如何验证？是否存在兼容性风险？

---

## 7. 明令禁止

- 为了编译通过而**删除功能**；
- 为了消除报错而**大面积注释代码**；
- 用 **Mock 冒充真实功能**；
- 用**日志输出冒充网络同步**；
- 用**假数据冒充逆向结论**；
- **编译成功就宣布功能完成**。

> `Build Passed ≠ Feature Complete`

---

## 8. 验证标准

```text
Compile Verified → Runtime Verified → Multiplayer Verified → Regression Verified → Feature Verified
```

PR 模板中必须声明当前处于哪一级，**不能跳级声明**。

---

## 9. 文档要求

| 目录 | 所有者 | 内容 |
|---|---|---|
| `docs/interfaces/` | AI-00 | 接口契约（AI-A1→A2、A2→A3/A4、C1→B1/B4、B3→B4、C3→所有） |
| `docs/re/` | AI-A1 | 逆向文档（player / death / blood_echoes / enemy / boss / world / lantern / …） |
| `docs/security/` | AI-C2 | 威胁模型 / 安全架构 / 隐私方案 / 中继安全协议 |
| `docs/audit/` | AI-C3 | 审计报告（PASS / PASS_WITH_WARNING / FAIL） |
| `docs/pentest/` | AI-C4 | 渗透测试报告与修复验证 |
| `docs/integration/` | AI-00 | 集成测试报告 |
| `docs/conflict_log/` | AI-00 | 冲突记录与裁决 |

`docs/re/` 中每个关键函数至少记录：

```text
Function / Address / Purpose / Arguments / Return / Callers / Called Functions
Evidence / Confidence / Related Feature
```

CI 会校验 `docs/` 目录结构完整性。

---

## 10. 每日同步

每个 Agent 每日输出：

```text
├── 昨日完成 / 今日计划
├── 阻塞项
└── 需要其他 Agent 协助的事项
```

---

## 11. 冲突裁决

| 冲突类型 | 裁决方 | 优先级 |
|---|---|---|
| 接口冲突 | AI-00 | 开发优先 |
| 安全 vs 开发 | AI-C3 | **安全优先** |
| 性能 vs 安全 | AI-C2 | **安全优先** |
| 功能 vs 安全 | AI-C3 | **安全优先** |
| 版本兼容冲突 | AI-00 | 兼容优先 |

> **安全永远优先。** 任何以“性能”或“功能”为由削弱安全的设计，必须建立 Issue 并由 AI-C3 裁决。

---

## 12. 日志规范

日志前缀（任务书 §34）：

```text
[SESSION] [NETWORK] [PLAYER] [DEATH] [RESPAWN] [WORLD] [LANTERN]
[ENEMY] [BOSS] [INVASION] [PVP] [BALANCE] [HOOK] [ERROR]
[SESSION_STATE] [LOOT] [CONSUMABLE] [SHOP] [NPC]
[ANTICHEAT] [BLACKLIST] [HOST_TRUST] [ARENA]
[SECURITY] [CRYPTO] [RELAY] [REPUTATION]
```

- 日志写入必须**异步**，不阻塞主线程；
- **不得记录任何隐私数据**。
