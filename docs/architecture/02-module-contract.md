# 模块契约

这份文档回答三个问题：一个模块**必须实现什么**、**可以依赖什么**、**禁止什么**。
适用对象是任务书 §3.6 注册表中的 15 个模块；
Kernel 与两个贯穿性辅助模块的边界见 [01-kernel.md](01-kernel.md) 与本文第 6 节。

---

## 1. 模块的定义

- **一个模块 = 一个所有权单元**（任务书 §48-c）。
- **模块 ID 与层级的唯一来源是任务书 §3.6 的注册表**：
  `config/modules.yaml`、`docs/modules/*.md` 与分支写入范围的判定都以那张表为准
  （任务书 §3.6 末段）。模块 ID 不得自造、不得改名。
- 每个模块有独立的模块 ID、独立的构建目标、独立的测试、独立的健康状态、独立的降级路径
  （任务书 §3.6 原则 2）。
- Layer 2 的游戏系统模块默认 `enabled: false`，按功能开关启用（任务书 §3.6 原则 4）。

---

## 2. 必须实现的内容

### 2.1 目录与文件

每个模块必须提供 `module.cpp` / `module.h`、`CMakeLists.txt`、`README.md`、
`tests/contract_test.cpp`（任务书 §41.1 T-007）。

模块的初始实现默认行为：**返回 `Unavailable` 且不崩溃**（任务书 §41.1 T-007）。
`Unavailable` 是任务书中出现过的模块状态取值；完整的状态取值集合未给出 → 见第 7 节 M-4。

### 2.2 实现冻结接口

模块对外只暴露 `src/interfaces/` 中标注为 v1 冻结的接口实现。
接口文件清单由 T-004 产出：`imodule.h`、`itransport.h`、`isession.h`、`igameplay.h`、
`module_context.h`、`version.h`、`errors.h`（任务书 §41.1 T-004）。
这些接口「可独立编译，不依赖模块头文件」（任务书 §41.1 T-004）。

**不允许静默失败。** 每个接口必须定义失败时的可观测行为（日志前缀 + 错误码）
（`docs/interfaces/README.md` §2）。任何校验失败（HMAC / 序列号 / 版本 / 指纹）
必须记录 `[SECURITY]` 日志并拒绝该消息（`docs/security/constraints.md` C-007；
任务书 §5.5「不允许静默失败」）。

### 2.3 通过模块 Gate 五条

每个模块必须通过（任务书 §3.7、§41.3）：

1. 单元测试
2. 契约测试
3. 独立运行测试
4. 故障注入测试
5. 禁用后核心系统仍可用测试

模块 Gate 未通过时，该模块的任何后续改动不得合并（任务书 §52-b）。

### 2.4 参与配置与文档

- 模块必须在 `config/modules.yaml` 与 `config/features.yaml` 中有条目；
  未知模块 ID 拒绝启动（任务书 §41.1 T-008）。
- 每个模块必须有 `docs/modules/<module>.md` 任务书（任务书 §41.1 T-010）。
  任务书建议该文件包含「本模块的原版行为约束」，内容取自任务书对应章节并标注行号，
  否则那 15 份模块任务书会缺少实质内容（任务书 附录 B）。
- 模块的失败行为必须写进 [04-degradation-matrix.md](04-degradation-matrix.md)，
  并在发生变化时按 §52-d 同步更新（任务书 §52-d）。

### 2.5 构建约束

- 模块统一链接警告接口目标：`target_link_libraries(<target> PRIVATE bbcoop_warnings)`，
  模块不自行决定是否开启警告（`docs/standards/build-standards.md` §2）。
- 警告即错误（`/W4` + `/WX`）。不得用大面积 `/wd` 让 `/WX` 通过
  （`docs/standards/build-standards.md` §2「明确禁止」）。
- 语言标准为 C++20，关闭编译器扩展（`docs/standards/build-standards.md` §7）。
- 函数认知复杂度上限 25、单函数行数上限 200（`docs/standards/build-standards.md` §5，
  阈值已写进 `.clang-tidy`）。

模块的路径与默认开关见第 6 节。每个模块的 CMake 目标命名规则，
任务书没有给出（T-009 只要求「每模块 target」）→ 见第 7 节 M-11。

---

## 3. 可以依赖什么

| 可以依赖 | 说明 | 依据 |
|---|---|---|
| `src/interfaces/` 的 v1 冻结接口 | 模块之间唯一的通信方式 | 任务书 §3.6 原则 3 |
| Kernel 提供的宿主能力 | 加载、配置、日志、事件总线、健康巡检、熔断。具体接口名与语义未定 | 任务书 §3.6 原则 1；接口属 T-004 / T-005 产出 |
| CMake 目标 `bloodcoop::kernel`、`bloodcoop::interfaces` | 由 T-009 建立 | 任务书 §41.1 T-009 |
| 每个模块自己的 target | 每个模块一个构建目标 | 任务书 §41.1 T-009 |
| 两个贯穿性辅助模块 | `mod.observability` 与 `mod.security_audit`「按需接入 Kernel 或各层」 | 任务书 §3.6 注册表 |

不允许依赖的内容见第 4 节。

**第三方依赖白名单任务书没有给出** → 见第 7 节 M-6。
可以确定的只有一条：不得引入未审计的二进制（任务书 §3.6 硬性禁止；
`MAINTAINERS.md` §3 第 4 条；`AI_POLICY.md` §2.1 第 3 条）。

---

## 4. 禁止事项

| 编号 | 禁止 | 依据 |
|---|---|---|
| P-1 | 跨模块 `#include`；模块之间互相 `target_link_libraries` | 任务书 §3.6 硬性禁止；`MAINTAINERS.md` §3 第 3 条；`AI_POLICY.md` §2.2 第 5 条 |
| P-2 | 直接修改 `src/interfaces/` 中已冻结的 v1 接口 | 任务书 §52-c；`AI_POLICY.md` §2.2 第 7 条 |
| P-3 | 修改 `src/modules/crypto/` 的代码（其他模块负责人） | 任务书 §48-c；`MAINTAINERS.md` §4 |
| P-4 | 修改 `src/modules/security_audit/` 的代码（开发模块负责人） | 任务书 §48-c；`MAINTAINERS.md` §4 |
| P-5 | 降级加密为明文 | 任务书 §3.6 硬性禁止与原则 6；`MAINTAINERS.md` §3 第 1 条；`AI_POLICY.md` §2.1 第 1 条；`docs/security/constraints.md` C-001 |
| P-6 | 静默失败（校验失败不记录、不拒绝） | 任务书 §5.5；`docs/security/constraints.md` C-007；`docs/interfaces/README.md` §2 |
| P-7 | 在 PvE 模式启用行为反作弊 | `docs/security/constraints.md` C-012；`AI_POLICY.md` §2.1 第 4 条；任务书 §46.0 |
| P-8 | 提交密钥、Token、凭据 | 任务书 §3.6 硬性禁止；`MAINTAINERS.md` §3 第 2 条 |
| P-9 | 引入未审计的二进制 | 任务书 §3.6 硬性禁止；`MAINTAINERS.md` §3 第 4 条 |
| P-10 | 在 Layer Gate 未通过时合并上层模块 | 任务书 §3.6 硬性禁止、§52-b；`AI_POLICY.md` §2.2 第 6 条 |
| P-11 | 为了让改动「合法」而扩大写入范围 | `AI_POLICY.md` §2.3 第 9 条 |
| P-12 | 为了编译通过而删除功能；为消除报错而大面积注释代码 | `MAINTAINERS.md` §3 第 6 条；`AI_POLICY.md` §2.4 第 10、11 条 |
| P-13 | 用 Mock 冒充真实功能、用日志冒充网络同步、用假数据冒充逆向结论 | `MAINTAINERS.md` §3 第 7 条；`AI_POLICY.md` §2.4 第 12 条 |

---

## 5. 依赖变化时的义务

出现下列任一情况时，**必须**同步更新故障注入套件与降级矩阵（任务书 §52-d）：

- 新增一个模块；
- 一个模块新增对外的依赖（调用别的模块或依赖新的系统能力）；
- 一个模块的失败行为发生变化（原来会崩，现在降级；或反之）；
- 新增一类失败场景（如新的外部依赖、新的超时路径）。

产出要求（任务书 §52-d）：

| 产出 | 内容 |
|---|---|
| 故障注入用例 | 每个用例有明确断言，不是「跑一下看看」 |
| 降级矩阵 | 写明：哪个模块失败 → 哪些功能降级 → 用户看到什么 |
| 断言证据 | 用例的真实输出，写进 `REFACTOR-REPORT.md` |

「没有环境」不是理由：无法自动化的用例必须逐个列出**具体技术原因**
（缺哪类依赖、缺哪个系统能力）（任务书 §52-d）。

---

## 6. 模块 ID、层、路径与默认开关

模块 ID 与层级沿用任务书 §3.6；路径沿用任务书 §48-c 与 `MAINTAINERS.md` §4。

| 层 | 模块 ID | 路径 | 默认开关 | 依据 |
|---|---|---|---|---|
| Kernel | — | `src/kernel/` | 不适用 | 任务书 §48-c |
| 接口 | — | `src/interfaces/` | 不适用 | 任务书 §48-c |
| Layer 0 | `mod.transport` | `src/modules/transport/` | 待定（见 M-5） | 任务书 §48-c；§3.6 原则 4 只规定 L2 |
| Layer 0 | `mod.discovery` | `src/modules/discovery/` | 待定（见 M-5） | 同上 |
| Layer 0 | `mod.relay` | `src/modules/relay/` | 待定（见 M-5） | 同上 |
| Layer 0 | `mod.crypto` | `src/modules/crypto/` | 待定（见 M-5）；失败时拒绝联机，不可配置绕过 | 任务书 §3.6 原则 6 |
| Layer 0 | `mod.session_core` | `src/modules/session_core/` | 待定（见 M-5） | 同上 |
| Layer 1 | `mod.game_hook` | `src/modules/game_hook/` | 待定（见 M-5） | 同上 |
| Layer 1 | `mod.replication_core` | `src/modules/replication_core/` | 待定（见 M-5） | 同上 |
| Layer 1 | `mod.join_leave` | `src/modules/join_leave/` | 待定（见 M-5） | 同上 |
| Layer 1 | `mod.area_sync` | `src/modules/area_sync/` | 待定（见 M-5） | 同上 |
| Layer 2 | `mod.death` | `src/modules/death/` | `enabled: false` | 任务书 §3.6 原则 4 |
| Layer 2 | `mod.world_reset` | `src/modules/world_reset/` | `enabled: false` | 任务书 §3.6 原则 4 |
| Layer 2 | `mod.enemy_boss` | `src/modules/enemy_boss/` | `enabled: false` | 任务书 §3.6 原则 4 |
| Layer 2 | `mod.loot` | `src/modules/loot/` | `enabled: false` | 任务书 §3.6 原则 4 |
| Layer 2 | `mod.pvp` | `src/modules/pvp/` | `enabled: false` | 任务书 §3.6 原则 4 |
| Layer 2 | `mod.save` | `src/modules/save/` | `enabled: false` | 任务书 §3.6 原则 4 |
| 贯穿性 | `mod.observability` | `src/modules/observability/` | 待定（见 M-7） | `MAINTAINERS.md` §4 |
| 贯穿性 | `mod.security_audit` | `src/modules/security_audit/` | 待定（见 M-7） | `MAINTAINERS.md` §4 |

⚠️ **任务书 §48-c 的所有权表里没有列出 `mod.observability` 与 `mod.security_audit`**
（该表只有 Kernel、接口与 15 个模块共 17 行），但 §48-c 的正文又要求
「`mod.security_audit` 的代码不允许被开发模块的负责人修改」。
本表按任务书 §3.6 注册表（18 个条目）与 `MAINTAINERS.md` §4（含两条贯穿性行）补全这两行。
这是任务书内部的不一致，见第 7 节 M-1。

---

## 7. 待定事项

| 编号 | 待定内容 | 缺什么 | 由谁决定 |
|---|---|---|---|
| M-1 | `mod.observability` 与 `mod.security_audit` 的所有权归属 | 任务书 §3.6 注册表列出了它们，§48-c 的所有权表却没有；两处不一致 | 需要裁决（`MAINTAINERS.md` §7） |
| M-2 | `mod.observability` 与 `mod.security_audit` 接入哪一层、以什么形式接入 | 任务书 §3.6 只写「按需接入 Kernel 或各层」 | T-004 / T-005 |
| M-3 | 冻结接口的具体签名与数据结构 | 任务书只给了文件名（`imodule.h` 等）与「不依赖模块头文件、标注 v1 冻结」两条验收 | T-004 |
| M-4 | 模块健康状态的完整取值集合 | 任务书只出现过 `Unavailable` 一个取值（T-007） | T-004 / T-005 |
| M-5 | Layer 0 / Layer 1 模块的默认开关值 | 任务书只规定 Layer 2 默认 `enabled: false` | T-008 |
| M-6 | 第三方依赖白名单 | 任务书只禁止「未审计的二进制」，没有给可用清单 | 需要裁决；`AI_POLICY.md` §3 要求不确定时建 Issue 问人 |
| M-7 | 两个贯穿性辅助模块的默认开关 | 任务书没有写 | T-008 |
| M-8 | 契约测试的范围与判据 | 任务书只写了「每个模块必须有 `tests/contract_test.cpp`」与「契约测试通过」 | T-007 / T-012 |
| M-9 | `config/modules.yaml` / `features.yaml` 的字段定义 | 任务书只给了文件名、覆盖范围与「未知模块 ID 拒绝启动」 | T-008 |
| M-10 | 错误码体系 | 任务书要求每个接口定义「日志前缀 + 错误码」（`docs/interfaces/README.md` §2），并列出 `errors.h`，但没有给具体错误码 | T-004 |
| M-11 | 每个模块的 CMake 目标命名规则 | 任务书只要求「每模块 target」，没有给命名规则 | T-009 |

---

## 依据

- 任务书 `bloodborne.markdown`
  - §3.6 模块化架构与故障隔离原则（六条核心原则、硬性禁止、模块注册表）
  - §3.7 分层模型与出口条件（模块 Gate 五条）
  - §5.5 版本安全（不允许静默失败）
  - §41.1 T-004 / T-005 / T-007 / T-008 / T-009 / T-010 / T-012
  - §41.3 Gate 验收标准
  - §46.0 反作弊适用范围
  - §48-c 模块所有权
  - §52-b Layer Gate 未通过不得合并上层
  - §52-c 接口冻结后的变更流程
  - §52-d 故障注入与降级矩阵的更新义务
  - 附录 A、附录 B
- `MAINTAINERS.md`（§3 硬性禁止、§4 模块所有权、§6 文件级所有权、§7 冲突裁决）
- `AI_POLICY.md`（§2 硬性禁止、§3 遇到不确定时的处理顺序）
- `docs/security/constraints.md`（C-001、C-007、C-012）
- `docs/interfaces/README.md`（每个接口必须定义的字段、不允许静默失败、契约变更流程）
- `docs/standards/build-standards.md`（编译标准、复杂度门槛、C++ 标准）
