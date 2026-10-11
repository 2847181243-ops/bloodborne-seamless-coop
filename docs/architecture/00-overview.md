# 架构总览

这份文档回答一个问题：整个仓库的运行时结构由哪些部分组成，分成几层，谁可以依赖谁。
各文档的分工：Kernel 的职责与边界见 [01-kernel.md](01-kernel.md)，
模块必须实现与禁止的内容见 [02-module-contract.md](02-module-contract.md)，
故障如何被限制在模块内见 [03-fault-isolation.md](03-fault-isolation.md)，
每个模块失败后用户会看到什么见 [04-degradation-matrix.md](04-degradation-matrix.md)。

---

## 1. 运行时的三个组成部分

| 部分 | 数量 | 说明 | 依据 |
|---|---|---|---|
| Kernel | 1 | 加载、配置、日志、事件总线、健康巡检、熔断；不含任何业务逻辑 | 任务书 §3.6 原则 1、注册表 Kernel 行 |
| 模块 | 15 | 分 Layer 0 / Layer 1 / Layer 2 三层 | 任务书 §3.6 注册表 |
| 贯穿性辅助 | 2 | `mod.observability`、`mod.security_audit`，按需接入 Kernel 或各层 | 任务书 §3.6 注册表 |

Kernel 不计入这 15 个模块（任务书 §3.6）；两个贯穿性辅助同样不计入（任务书 §3.6 注册表把「15 个」与「2 个贯穿性辅助」并列写出，合计 5 + 4 + 6 = 15）。

本次重构的目标交付版本是 `v0.1.0-modular`（任务书 §3.6、§41.1 T-020）。

---

## 2. 模块注册表

下表逐字沿用任务书 §3.6 的注册表。**这张表是唯一来源**：
`config/modules.yaml`、`docs/modules/*.md` 与分支写入范围判定都以它为准（任务书 §3.6 末段）。

| 层 | 模块 ID | 名称 | 职责 |
|---|---|---|---|
| Kernel | — | 加载/健康/熔断 | 失败则不加载 Mod，原版启动 |
| Layer 0 | `mod.transport` | 传输 | UDP/TCP/Relay 传输、连接生命周期 |
| Layer 0 | `mod.discovery` | 发现 | 房间码、Peer 发现 |
| Layer 0 | `mod.relay` | 中继 | NAT 回退中继 |
| Layer 0 | `mod.crypto` | 加密 | 握手、加密、认证 |
| Layer 0 | `mod.session_core` | 会话核心 | Session 成员、心跳、状态机 |
| Layer 1 | `mod.game_hook` | 游戏 Hook | 注入、Hook、网络重定向 |
| Layer 1 | `mod.replication_core` | 复制核心 | 实体复制基础 |
| Layer 1 | `mod.join_leave` | 加入/离开 | 客机加入/离开流程 |
| Layer 1 | `mod.area_sync` | 区域同步 | 区域切换同步 |
| Layer 2 | `mod.death` | 死亡 | 死亡/重生同步 |
| Layer 2 | `mod.world_reset` | 世界重置 | Lantern / Party Wipe 同步 |
| Layer 2 | `mod.enemy_boss` | 敌人/Boss | 敌人/Boss 状态同步 |
| Layer 2 | `mod.loot` | 掉落 | 掉落/库存独立同步 |
| Layer 2 | `mod.pvp` | PvP | PvP 结算、反作弊 |
| Layer 2 | `mod.save` | 存档 | `.bloodco` 存档 |
| 贯穿性 | `mod.observability` | 日志 / 指标 | 按需接入 Kernel 或各层 |
| 贯穿性 | `mod.security_audit` | 审计 | 按需接入 Kernel 或各层 |

除这 15 个模块外，任务书还提到两个不属于模块的代码位置：

- `src/platform/`：对应原 AI-A0 的平台集成与启动器，**不在 15 个模块内**（任务书 附录 A）。
- `src/interfaces/`：冻结的 v1 接口，模块之间唯一的通信载体，也不属于任何一个模块（任务书 §3.6 原则 3、§48-c）。

`src/platform/` 与 Kernel 的分工，任务书没有写 → **待定**（见第 7 节）。

---

## 3. 三层与各自的独立可用性

| 层 | 内容 | 独立可用性 | 依据 |
|---|---|---|---|
| Kernel | 加载 / 健康 / 熔断 | 失败则不加载 Mod，原版启动 | 任务书 §3.7 |
| Layer 0 | P2P 传输、发现、中继、加密、Session 核心 | 独立命令行工具可连接、收发、重连 | 任务书 §3.7 |
| Layer 1 | 游戏 Hook、复制核心、加入离开、区域同步 | 双人游戏内可见、可移动、可切换区域 | 任务书 §3.7 |
| Layer 2 | 死亡、世界重置、敌人、掉落、PvP、存档 | 任一模块可禁用，不影响其他模块 | 任务书 §3.7 |

三层的出口条件原文、以及把它们转成脚本与退出码的方式，写在
[03-fault-isolation.md](03-fault-isolation.md) 第 4 节。

---

## 4. 依赖方向

已确定的部分有四条：

1. **模块之间只能通过 `src/interfaces/` 中冻结的 v1 接口通信**（任务书 §3.6 原则 3）。
2. **禁止跨模块 `#include`，禁止模块之间互相 `target_link_libraries`**
   （任务书 §3.6 硬性禁止；`MAINTAINERS.md` §3 第 3 条；`AI_POLICY.md` §2.2 第 5 条）。
   CMake 层面对应的判定是「模块间非法链接被 CMake 拒绝」（任务书 §41.1 T-009）。
3. **Kernel 向模块提供宿主能力，模块不向 Kernel 提供业务服务。**
   依据是 Kernel 的职责清单只有加载、配置、日志、事件总线、健康巡检、熔断，
   且「不含任何业务逻辑」（任务书 §3.6 原则 1）。
4. **合并顺序单向：Layer 0 → Layer 1 → Layer 2。**
   Layer 0 出口未通过前不得合并 Layer 1 模块，Layer 1 出口未通过前不得合并 Layer 2 模块
   （任务书 §3.7 串行约束、§41.2、§52-b）。

未确定的部分：

- **允许的跨层接口调用方向没有白名单。** 任务书给出了分层、给出了「只能走冻结接口」，
  但没有规定哪一层的哪个接口可以被哪一层调用。附录 A 只说明了旧 AI-A2 被拆成
  `mod.session_core`（Layer 0）与 `mod.replication_core`（Layer 1）之后，
  原来的内部接口变成了跨层接口，必须按 §52-c 的流程**重新冻结方向**（任务书 附录 A）。
  → **待定：跨层接口的方向白名单由 T-004 冻结接口时确定，接口冲突由 AI-00 裁决**
  （任务书 §49、§52-c）。
- **两个贯穿性辅助模块与各层之间的依赖方向没有规定**（任务书 §3.6 只写「按需接入 Kernel 或各层」）。
  → **待定：由 T-004 / T-005 决定**，并影响故障边界是否成立（见
  [03-fault-isolation.md](03-fault-isolation.md) 第 6 节）。

---

## 5. 默认开关与安全硬依赖

- **默认关闭**：Layer 2 的游戏系统模块默认 `enabled: false`，按功能开关启用（任务书 §3.6 原则 4）。
  Layer 0 / Layer 1 模块的默认开关值，任务书没有写 → **待定：由 T-008 在 `config/modules.yaml` 与
  `config/features.yaml` 中确定**。
- **安全硬依赖**：`mod.crypto` 失败时拒绝联机，**不降级为明文**；反作弊失败仅禁用 PvP
  （任务书 §3.6 原则 6）。这两条不是可配置项，任何模块都不得提供绕过路径
  （`MAINTAINERS.md` §3 第 1 条；`AI_POLICY.md` §2.1 第 1 条；
  `docs/security/constraints.md` C-001）。

---

## 6. 目录与所有权

一个模块 = 一个所有权单元（任务书 §48-c）。模块 ID 到路径的映射沿用 §48-c 的表，
完整表见 [02-module-contract.md](02-module-contract.md) 第 6 节；权限矩阵的唯一来源是
`MAINTAINERS.md`。

两条不变的限制（任务书 §48-c；`MAINTAINERS.md` §4）：

- `src/modules/crypto/` 的代码不允许被其他模块的负责人修改。
- `src/modules/security_audit/` 的代码不允许被开发模块的负责人修改。

⚠️ 这两条当前是**文本层强制**，不是平台级保证：仓库是个人账号、`CODEOWNERS` 单账号，
GitHub 不允许自审，把必需审批人数设为 ≥ 1 会让唯一账号无法合并自己的 PR
（`MAINTAINERS.md` §4.1；任务书 §48-d）。**不得对外表述为「平台已强制」**。

---

## 7. 待定事项

| 编号 | 待定内容 | 缺什么 | 由谁决定 |
|---|---|---|---|
| O-1 | 核心模块清单 | 任务书多处使用「非核心模块」（§3.6 原则 5、§41.1 T-012、§41.3 全局 Gate 9），但没有列出哪些模块算核心 | 需要裁决（`MAINTAINERS.md` §7「任务卡范围争议」由 Human Owner 裁决） |
| O-2 | 跨层接口调用方向白名单 | 任务书只给了分层与「只能走冻结接口」 | T-004 冻结接口 + AI-00 裁决（§49、§52-c） |
| O-3 | 两个贯穿性辅助模块的接入位置与依赖方向 | 任务书只写「按需接入 Kernel 或各层」 | T-004 / T-005 |
| O-4 | Layer 0 / Layer 1 模块的默认开关 | 任务书只规定了 Layer 2 默认 `enabled: false` | T-008 |
| O-5 | `src/platform/` 与 Kernel 的分工 | 任务书只说明它不在 15 个模块内（附录 A） | 需要裁决 |

---

## 依据

- 任务书 `bloodborne.markdown`
  - §3.6 模块化架构与故障隔离原则（六条核心原则、硬性禁止、模块注册表）
  - §3.7 分层模型与出口条件（分层表、串行约束）
  - §41.1 T-001 / T-004 / T-005 / T-009 / T-020
  - §41.2 执行顺序与串行约束
  - §48-c 模块所有权
  - §48-d `task/*` 与 `develop` 分支约定
  - §49 冲突裁决机制
  - §52-b Layer Gate 未通过不得合并上层
  - §52-c 接口冻结后的变更流程
  - 附录 A Agent 划分 → 模块划分对照
- `MAINTAINERS.md`（§3 硬性禁止、§4 模块所有权、§4.1 权限受限之处、§6 文件级所有权、§7 冲突裁决）
- `AI_POLICY.md`（§2.1 安全、§2.2 架构）
- `docs/security/constraints.md`（C-001 端到端加密强制）
- `docs/interfaces/README.md`（接口契约矩阵与变更流程）
