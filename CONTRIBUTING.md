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
8. 等 CI（6 项必需检查）+ 等 review/审计 → 由 AI-00 合并到 main
```

### 合并方式（已由规则集强制）

```text
squash 合并 → main 永远线性；合并后自动删除源分支
```

- 规则集 `main-protection` 已启用 `required_linear_history`，且
  `allowed_merge_methods=['squash']` —— **merge commit 与 rebase 均已关闭**。
- **合并后的 main 提交信息取 PR 标题**，因此 **PR 标题必须符合第 3 节的
  `<scope>: <subject>` 规范**；PR 内各条提交信息只保留在 PR 里，不进 main 历史。
- `strict_required_status_checks = true`：**分支落后于 main 时必须先同步**，
  否则必需检查不算数（PR 页会出现 "Update branch"）。
- 合并后源分支自动删除；同名分支复用时需重新从 main 切出。

### 合并前的硬性前置

> **本地预检必须先通过，才能请求合并。**（详见 [`tools/preci/README.md`](tools/preci/README.md)）

```text
[ ] tools/preci/preci.ps1              阶段 1 全绿（push 之前）
[ ] tools/preci/preci.ps1 -Stage 2 -Pr <编号>   阶段 2 全绿（请求合并之前）
[ ] 6 项必需检查全绿（见 .dsh/skills/bbcoop-github-ops/SKILL.md 第 3 节）
[ ] 触及 mod/security/**、mod/network/**、relay/**、tools/audit/**、tools/pentest/** 时：
    带 audit:passed / audit:warning 标签，且 PR 内新增 docs/audit/ 报告，
    报告含「双人独立签核」区、C3-a 与 C3-b 各自的「结论：」与标签一致
[ ] 跨所有权改动已建 cross-agent Issue（§52）
[ ] 无凭据 / 游戏本体 / 玩家存档入库（SECURITY.md §9）
```

**为什么要本地预检**：本项目曾在同一轮里连续 3 次「推上去才发现门禁不过」。
CI 反馈慢、消耗 Actions 额度，而绝大多数失败是**纯本地可判定的**。

**预检不替代 CI**：客户端钩子可被 `--no-verify` 绕过，真正的强制点在 CI 与规则集。
预检的价值是把发现失败的**时间**从「推上去之后」提前到「push 之前」。

**阶段 2 为什么不能省**：45 个阻断项里有 29 个依赖 PR 描述与标签，
push 之前不可能存在。跳过阶段 2 等于让这些门禁在合并前从未被验证过。

---

## 3. 提交信息规范

### 3.0 写作规范（人类语言）

本项目是**给人看的**：任务书要人读，PR 要人 review，门禁报错要人能懂。
因此标题、正文、注释、报错文案一律遵守：

**禁止**

| 禁止 | 反例 | 为什么不行 |
|---|---|---|
| **自造比喻/口号** | 「勾选剧场」「杜绝推上去才发现红」「门禁半开」 | 读者无法知道它指什么，还得反过来问作者 |
| **中英夹杂当术语** | 「pre-CI + 唯一聚合门禁 gate」「`[PASS]` 分支命名」 | 同一个概念要读两种语言；`[PASS]` 不是中文 |
| **只有作者懂的缩写** | 「S2」「PRBODY 那条」 | 上下文一丢就无法理解 |
| **结果不说原因** | 「规范问题：6 error(s)」 | 读者不知道该改哪一行 |

**应当**

| 应当 | 正例 |
|---|---|
| 直说做了什么、为什么 | 「把 PR 勾选框改成由 CI 逐个核对」 |
| 报错写清**哪个文件、哪一行、哪条规则** | 「`docs/x.md` 第 12 行有行尾空白（.editorconfig 要求去掉）」 |
| 中文用中文，代码标识符保留原文 | 「运行 `preci.ps1` 的第 9 项检查」而不是「跑一下 CI-emulation」 |
| 一句话能读懂，就不写第二句 | — |

> 这条规范**没有**机器门禁（"是否是人类语言"无法客观判定），因此写入 PR 模板
> 「合并就绪声明」由人勾选确认。写的时候如果发现自己要解释一个词，那就是不该用那个词。

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

## 5.1 安全审计双人签核（强制）

任务书 §44.14 规定 AI-C3 拥有安全审计职责与一票否决权。本仓库进一步要求
**每次审计由两名审计者独立完成并双签**：

| 角色 | 职责范围 |
|---|---|
| **C3-a** | 密码学 / 协议 / 完整性：E2EE、HMAC、序列号与时间戳防重放、密钥轮换、版本握手强校验 |
| **C3-b** | 隐私 / 中继 / 抗滥用：隐私红线、中继零信任、速率限制与包大小、抗 Sybil、反作弊范围（仅 PvP） |

报告模板与格式要求见 [`docs/audit/README.md`](docs/audit/README.md)。要点：

- 两名审计者**各自独立**给出「结论：」，取值只能是 `PASS` 或 `PASS_WITH_WARNING`；
- 任一方写 `FAIL`、取值与 PR 标签不一致、或任一方缺失 ⇒ **CI 阻断合并**；
- CI（`pr-guard.yml` 的 `security-gate`）会校验上述字段。

> ⚠️ **能力边界（不得含糊）**：这是**文本层强制**——CI 读取报告文本，
> **不是** GitHub 平台级双人审批。仓库为个人账号、CODEOWNERS 单账号，
> 平台无法强制两名审批人（ruleset 的 `required_reviewers` 只支持 team，需组织）。
> 因此「两名审计者是否真的独立」最终仍依赖流程纪律。
> **禁止**对外把该机制表述为平台级保证。

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
