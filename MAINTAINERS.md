# 维护者与所有权

> 本文件回答三个问题：**谁可以做什么**、**哪些东西是谁的**、**哪些操作任何人都不能做**。
>
> 依据：任务书 §41.1（T-000）、§48-c（模块所有权）、§48-d（分支与权限）、
> §3.6（硬性禁止）、§41.6（执行提示）。
>
> **本文件是权限矩阵的唯一来源。** 分支保护、CODEOWNERS、CI 的写入范围判定
> 都应能对照本文件解释自己。三者若与本文件冲突，以本文件为准并开 Issue 修正。

---

## 1. 角色

| 角色 | 是谁 | 职责 |
|---|---|---|
| **Human Owner** | `@2847181243-ops` | 最终授权；合并到 `main`；打 Release Tag；改密码学与反作弊方向 |
| **AI Maintainer** | AI-00 | 按任务卡执行重构；建 Issue / 分支 / PR；合并到 `develop` |
| **Security Reviewer** | 见 §4 | 审密码学、认证、反作弊相关改动；**当前能力受限，见 §4.1** |

---

## 2. 权限矩阵

| 操作 | AI Maintainer | Human Owner |
|---|---|---|
| 创建 Issue / 分支 / PR | ✅ | ✅ |
| 推送到 `task/*`、`fix/*` | ✅ | ✅ |
| 合并到 `develop`（CI 全绿） | ✅ | ✅ |
| **合并到 `main`** | ❌ **需批准** | ✅ |
| **打 Release Tag** | ❌ **需批准** | ✅ |
| 修改 CI / 构建脚本 | ✅（走 PR） | ✅ |
| **修改 `crypto` / `auth` / 反作弊** | ❌ **需 Security Reviewer + Owner** | ✅ |
| **删除模块 / 破坏性接口变更** | ❌ **需批准** | ✅ |
| `force push` / 重写历史 | ❌ | ❌ |
| 跳过 CI / 绕过分支保护 | ❌ | ❌ |

表里最后两行是**双方都不能做**的，不是"AI 不能而人可以"。
它们对应 §3 的硬性禁止 —— 历史与门禁是这个仓库的可信度基础，
破坏它们之后所有审计结论都失去意义。

---

## 3. 硬性禁止（对所有人）

1. **不降级加密为明文。**
   `crypto` 失败时拒绝联机，不提供"先用明文连上再说"的降级路径。
2. **不提交密钥、Token、凭据。**
   提交历史视为永久公开。误提交必须**立即轮换凭据**并建 Issue 记录。
3. **不跨模块 `#include` 或互相 `target_link_libraries`。**
   模块间只能通过 `src/interfaces/` 中冻结的 v1 接口通信。
4. **不引入未审计的二进制。**
5. **不在 Layer Gate 未通过时合并上层模块。**
   层出口条件见任务书 §41.3。
6. **不为了编译通过而删除功能、为了消除报错而大面积注释代码。**
7. **不用 Mock 冒充真实功能、用日志冒充网络同步、用假数据冒充逆向结论。**
8. **不在未验证等级声称的情况下宣布功能完成。**
   `Build Passed ≠ Feature Complete`。

> 前 5 条对应任务书 §3.6 的硬性禁止。后 3 条来自 §38 与 CONTRIBUTING.md §7，
> 一并列在此处，因为"禁止"应当只有一个地方可查。

---

## 4. 模块所有权（任务书 §48-c）

**一个模块 = 一个所有权单元。** 模块 ID 与层级的完整清单见任务书 §3.6，
那是唯一来源；本表只是把它映射到路径。

| 层 | 模块 | 路径 |
|---|---|---|
| Kernel | — | `src/kernel/` |
| 接口 | — | `src/interfaces/` |
| Layer 0 | `mod.transport` | `src/modules/transport/` |
| Layer 0 | `mod.discovery` | `src/modules/discovery/` |
| Layer 0 | `mod.relay` | `src/modules/relay/` |
| Layer 0 | `mod.crypto` | `src/modules/crypto/` |
| Layer 0 | `mod.session_core` | `src/modules/session_core/` |
| Layer 1 | `mod.game_hook` | `src/modules/game_hook/` |
| Layer 1 | `mod.replication_core` | `src/modules/replication_core/` |
| Layer 1 | `mod.join_leave` | `src/modules/join_leave/` |
| Layer 1 | `mod.area_sync` | `src/modules/area_sync/` |
| Layer 2 | `mod.death` | `src/modules/death/` |
| Layer 2 | `mod.world_reset` | `src/modules/world_reset/` |
| Layer 2 | `mod.enemy_boss` | `src/modules/enemy_boss/` |
| Layer 2 | `mod.loot` | `src/modules/loot/` |
| Layer 2 | `mod.pvp` | `src/modules/pvp/` |
| Layer 2 | `mod.save` | `src/modules/save/` |
| 贯穿性 | `mod.observability` | `src/modules/observability/` |
| 贯穿性 | `mod.security_audit` | `src/modules/security_audit/` |

**两条不变的硬规则**（与任务书 §48-c 一致）：

- `src/modules/crypto/` 的代码**不允许**被其他模块的负责人修改。
- `src/modules/security_audit/` 的代码**不允许**被开发模块的负责人修改。

### 4.1 权限受限之处（不得含糊）

任务书要求"修改 `crypto` / `auth` / 反作弊需 Security Reviewer + Owner"。
**本仓库当前无法由平台强制这一点。**

原因：仓库是个人账号、`CODEOWNERS` 单账号，**GitHub 不允许自审**。
把 `required_approving_review_count` 设为 ≥1 会让唯一账号无法合并自己的 PR ——
连正常的 PR 都合不了。

因此：

- 该要求当前是 **文本层强制**（靠记录、审计与流程纪律），
  **不是平台级保证**。
- **禁止**对外把它表述为"平台已强制"。要变成硬约束，需要引入第二名协作者账号。
- 这一限制同时记在任务书 §48-d 与 `CONTRIBUTING.md` §5.1，三处一致。

---

## 5. 分支与合并

| 分支 | 用途 | 谁能合并 | 门禁 |
|---|---|---|---|
| `main` | 发布线 | **Human Owner 批准后** | 6 项必需检查 + 线性历史 + squash-only |
| `develop` | 集成分支 | CI 全绿即可 | **与 `main` 完全相同的 6 项必需检查** |
| `task/T-xxx-<slug>` | 任务卡 | — | 提 PR 到 `develop` |
| `fix/<topic>` | 修复 | — | 提 PR 到 `develop` |

**为什么 `develop` 的门禁与 `main` 一样严**：`develop` 里的东西之后要进 `main`。
若 `develop` 比 `main` 松，`main` 就成了唯一防线，**`develop` 会变成绕过门禁的通道**。

### 5.1 分支名与所有权

`develop` 上的 `task/*` 分支**不带 Agent 标识** —— 这是有意设计：
所有权由**模块**决定（§4），不由分支名里的代号决定。

迁移期 `branch-policy.yml` 用一份**联合写入范围**承接 `task/*`。
它**不是最终的模块所有权划分**；把 15 个模块逐个写进 SCOPE 的工作属于 T-000 之后
的收紧步骤。在收紧之前，任务卡 PR 的越界判定比最终形态宽。

### 5.2 `main → develop` 的回流

任务书 §48-d 描述了 `develop → main`，没有描述反向。
**当前做法**：从 `main` 开分支提 PR 到 `develop`（即同步），不要直接改 `develop`。

热修流程：

```text
从 main 开 fix/<topic> → PR 到 main（Owner 批准）→ 再从 main 开分支 PR 到 develop
```

不要"只在 develop 修好再一起进 main" —— 那会让 `main` 在一段时间里带着已知缺陷。

---

## 6. 谁改什么（文件级所有权）

| 路径 | 归属 | 说明 |
|---|---|---|
| `bloodborne.markdown` | Human Owner / AI-00 | **任务书，唯一需求来源**；只增补，不改写原文 |
| `src/kernel/`、`src/interfaces/` | AI-00 | Kernel 与冻结接口 |
| `src/modules/<module>/` | 该模块 | 见 §4 的路径表 |
| `src/modules/crypto/` | 受限 | 其他模块负责人不得修改 |
| `src/modules/security_audit/` | 受限 | 开发模块负责人不得修改 |
| `docs/security/` | 安全架构 | 含约束清单 |
| `docs/audit/` | 安全审计 | 开发方不得修改 |
| `docs/re/` | 逆向 | 结论必须标证据等级 |
| `docs/interfaces/` | AI-00 | 接口契约 |
| `docs/standards/` | AI-00 | 规范与交接 |
| `docs/architecture/`、`docs/modules/` | AI-00 | T-001 / T-010 产出 |
| `config/` | AI-00 | 模块与功能开关 |
| `.github/`、`tools/preci/` | AI-00 | 门禁自身 |
| `relay/` | 受限 | 中继节点 |

`.github/CODEOWNERS` 是本表在平台层的映射。
**两处必须一致**；改了一处而没改另一处，等于给出一条无人值守的路径。

---

## 7. 冲突裁决

| 冲突类型 | 裁决方 | 优先级 |
|---|---|---|
| 接口冲突 | AI-00 | 开发优先 |
| 安全 vs 开发 | 安全审计 | **安全优先** |
| 性能 vs 安全 | 安全架构 | **安全优先** |
| 功能 vs 安全 | 安全审计 | **安全优先** |
| 版本兼容冲突 | AI-00 | 兼容优先 |
| **层出口条件是否达标** | Human Owner | — |
| **任务卡范围争议** | Human Owner | — |

> **安全永远优先。** 任何以"性能"或"功能"为由削弱安全的设计，
> 必须建 Issue 并裁决，不能由实现者自行决定。

---

## 8. 参考

- 任务书：`bloodborne.markdown`
  - §3.6 模块化架构与硬性禁止
  - §3.7 分层模型与 Gate
  - §41.1 任务卡（含每卡的标准工作流）
  - §41.2 执行顺序与串行约束
  - §41.6 执行提示（**T-000 的 PR 不得自动合并**）
  - §48-c 模块所有权
  - §48-d 分支与权限矩阵
- `CONTRIBUTING.md` —— 日常工作流、提交规范、证据等级
- `SECURITY.md` —— 安全一票否决、隐私红线、威胁模型
- `.github/CODEOWNERS` —— 本文件 §6 在平台层的映射
- `.github/workflows/branch-policy.yml` —— 写入范围的机器判定
