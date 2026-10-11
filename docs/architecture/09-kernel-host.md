# 09 Kernel / Module Host 规格（T-005）

> **用途**：把 `01-kernel.md` §2 的六项职责落成可判定的接口、状态机与数值，作为 T-005 实现的依据。
> **前提**：`docs/architecture/01-kernel.md`（边界与不做清单）、`06-process-boundary.md`（进程边界，
> Human Owner 裁定）、`07-kernel-seam.md`（Kernel 与 IPC 的接缝，**已定**）、`08-ipc.md`（T-021，已实现）。
> **依赖的冻结接口**：`src/interfaces/imodule.h`、`module_context.h`、`errors.h`、`version.h`（**不改**）。
> **状态**：规格草案（v1）。数值未标「已定」的一律为**暂定**，实现不得当作任务书规定。
> **最后更新**：2026-10

---

## 1. 目标与非目标

**目标**

1. Kernel 能**加载**模块（注册表 + 编译期注册），`init` 失败**不阻断**其他模块；
2. 统一的 `tick` 调度与**熔断**（超时 / 连续失败 / 进程外通道失效）；
3. **健康巡检与聚合**，并能区分「为什么不可用」（`07-kernel-seam.md` §1.5）；
4. **事件总线**：定死投递语义，否则下游模块会各自假设一套；
5. **日志机制**：`ILogger` 实现 + 有界队列 + 可插拔落盘目标（K-7：Kernel 只做机制）；
6. `bloodcoop-host` 可执行宿主，`--self-test` 输出 JSON（required 失败 → 退出码非 0）；
7. 给 T-006（单测夹具）与 T-009（目标结构）留下可判定的接缝。

**非目标**

- 15 个模块的实现（T-007）；`config/*.yaml` 的内容与解析（T-008）；
- 两个可执行目标的 CMake 结构、模块隔离断言（T-009，`07-kernel-seam.md` §2 已裁决为方案 B）；
- 辅助进程的创建 / 终止 / 重启（T-021 的窄平台适配层，K-10）；
- `--self-test` 的最终字段冻结（T-014 定稿；本规格给出 T-005 实际输出的形状）；
- 任何游戏机制、Hook、密码学、传输。

---

## 2. 决策摘要

| # | 决策 | 依据 |
|---|---|---|
| D-1 | Kernel 眼里**只有一种模块**（`IModule`），但有两个 `source`：`local`（进程内，直接持有 `IModule*`）与 `remote`（进程外，持有代理；存活事实由 IPC 回调驱动） | 07 §1.1（已定） |
| D-2 | Kernel 只依赖 `IAuxLiveness` 三件事：通道关闭回调、本地读心跳计数、请求终止；**不碰进程句柄** | 07 §1.2；K-10（已裁决） |
| D-3 | 熔断判定与恢复：**粘性熔断**（一旦不可用，本会话不自动恢复），阈值与周期集中在 `policy.h` 且全部标「暂定」 | K-1/K-3；06 §9（Kernel 不自动重启） |
| D-4 | Kernel 的生命周期状态与模块自报的 `Health` **分开存**，不混成一个字段 | K-4；`imodule.h` 的 `Health` 是冻结值域 |
| D-5 | 模块**静态链接 + 编译期注册表**（`ModuleDescriptor` 数组），不做运行时动态加载 | K-5；任务书只给 CMake 目标结构 |
| D-6 | 事件总线：`publish` 只入**有界队列**（不分配、满时拒绝并计数），`dispatchPending` 每 tick 同步分发给订阅者；**订阅者内再 publish 被拒绝**（重入防护） | K-8 |
| D-7 | 日志：Kernel 实现机制（有界队列 + 单点 drain + 可插拔 `ILogSink`）；策略与内容归 `mod.observability`。诊断可丢并计数，审计（含 `[SECURITY]`）不静默丢 | K-7（已裁决）；D-11 |
| D-8 | 进程内模块的 `tick` 超时**只能在返回后判定**（无法抢占）；进程内的段错误 / 死循环**拦不住**，不得声称「Gate 第 9 项已满足」 | K-6；06 §5/§6 |
| D-9 | 自检报告每个模块带 `source` 与 `unavailable_reason`；`overall` 的判定规则写死在本规格 §8 | 07 §1.5；任务书 §3.7 Gate 6 |
| D-10 | 配置缺失（T-008 尚未产出 `config/modules.yaml`）时：按**编译期注册表**加载全部模块，并把 `config_source=absent` 写进自检；**不静默假装读过配置** | §41.1 T-008 依赖关系 |

---

## 3. 模块来源与状态

### 3.1 两个维度分开存（D-4）

| 维度 | 取值 | 谁决定 |
|---|---|---|
| `source` | `local` / `remote` | 编译期（T-009 按 `placement` 分派）+ 运行期注册表 |
| Kernel 生命周期 `state` | `registered` → `initializing` → `ready` → `unavailable` → `shutdown` | Kernel |
| 模块自报 `Health` | `Ok` / `Degraded` / `Unavailable`（冻结值域） | 模块自己（`IModule::health`） |

**为什么分开**：`Health::Unavailable` 既可能是「模块没实现」（T-007 的骨架），也可能是
「被 Kernel 熔断」。只看 `health()` 分不出这两者，而自检报告必须能分（07 §1.5）。

### 3.2 `unavailable_reason`（自检必须能区分）

| 取值 | 含义 | 由谁设置 |
|---|---|---|
| `null` | 可用 | — |
| `init_failed` | `init` 返回失败或创建失败 | `IModule::init` 路径 |
| `tick_timeout` | 单次 `tick` 超过阈值 | 熔断 |
| `tick_failures` | 连续 `tick` 返回失败达到阈值 | 熔断 |
| `channel_closed` | 进程外模块的通道被关闭（含进程被杀） | IPC 回调 |
| `heartbeat_stale` | 进程外模块的心跳计数在 N 内没变化 | 巡检 + 终止请求 |

取值集合与 `08-ipc.md` 的 `UnavailableReason` 逐字一致（同一套措辞，两处不要各写一份）。

### 3.3 状态机

```text
registered ──init ok──▶ ready ──熔断/通道失效──▶ unavailable
     │                     ▲                          
     └──init 失败──────────┴──▶ unavailable（不阻断其他模块）

任一步 ──shutdown()──▶ shutdown（幂等）
```

- 进入 `unavailable` **幂等**：第二次标记不得重复计数、不得重复记日志（07 §1.3 第 2 条）；
- `unavailable` **粘性**（D-3）：本会话不自动恢复；`shutdown` 之后不再 `tick`。

---

## 4. 生命周期

```text
构造 Kernel
  ├─ loadTable()      读注册表（编译期 + 可选配置），建立 ModuleStatus
  ├─ initModules()    逐个 create + init；**每个模块独立判定**，失败只标记自己
  ├─ tickOnce(now)    固定顺序：读 remote 心跳 → 分发事件 → 逐模块 tick（计时/熔断）→ 健康巡检
  └─ shutdown()       逆序 shutdown，撤销服务发布，停止日志 drain
```

- **`init` 失败不阻断**：`create` 返回 `nullptr` 或 `init` 返回非 `Ok` → 该模块
  `unavailable(init_failed)`，继续下一个；
- **tick 顺序**：注册表顺序（确定性，便于复现）；单个模块 tick 抛错/返回失败**不中断**其余模块；
- **`shutdown` 幂等**：`init` 曾经失败的模块也要能安全 `shutdown`（`imodule.h` 的要求）。

---

## 5. 熔断（K-1 / K-3）

| 触发 | 判定 | 暂定数值 |
|---|---|---|
| 单次 `tick` 超时 | 调用返回后测得的耗时 > `kTickTimeoutMs` | **50ms** |
| 连续 `tick` 失败 | 连续返回非 `Ok` 达到 `kMaxConsecutiveTickFailures` | **3 次** |
| 通道关闭 | IPC 回调（`IAuxLiveness::watch`） | 无数值 |
| 心跳停滞 | 计数在 `kHeartbeatStaleMs` 内无变化 → 标记 + `requestTerminate` | 3000ms（`08-ipc.md` D-8，同源） |

**必须如实写清的一条（D-8）**：进程内模块的 `tick` 是普通函数调用，
Kernel **无法抢占**它。所以「超时」是**事后判定**：一个真正卡死的进程内模块会把
游戏进程一起拖住，Kernel 没有补救手段。进程外模块由进程隔离 + 心跳兜住。
**禁止**在任何报告里写「进程内模块崩溃已被隔离」或「Gate 第 9 项已满足」。

健康巡检周期 `kHealthPollIntervalMs`：**暂定 500ms**（K-2）。

---

## 6. 事件总线（K-8）

```text
publish(topic, bytes)  → 有界队列（固定容量，无分配）
dispatchPending(now)   → 每 tick 一次，同步调用订阅者
subscribe(topic, fn, user)
```

| 语义 | 取值 | 理由 |
|---|---|---|
| 投递 | 队列 + 每 tick 同步分发 | 不阻塞调用方；分发时机确定，便于复现 |
| 队列满 | `publish` 返回 `Unavailable` 并计数，**不阻塞、不覆盖** | 与 T-021 的背压策略一致 |
| 重入 | 分发过程中再次 `publish` 被**拒绝**并计数 | 无穷递归是内核级故障，宁可丢事件 |
| 订阅者失败 | 单个订阅者返回错误不影响其他订阅者 | 故障不传播（§3.6 原则 5） |
| 分发顺序 | 按订阅顺序 | 确定性 |
| 回调签名 | 函数指针 + `void*`，`noexcept` | 与 `IAuxLiveness` 同理（`noexcept` 边界上不分配） |

---

## 7. 日志（K-7）

- `KernelLogger` 实现冻结的 `ILogger`，把 `(level, module_id, message)` 组装成定长记录
  入**有界队列**（不分配）；
- **分类**：`Warn` / `Error` 与 `[SECURITY]` 前缀 → 审计类（不静默丢：队列满时 `log` 计数并
  进入显式失败态，由 drain 侧暴露 `dropped_audit`）；`Debug` / `Info` → 诊断类（可丢并计数）；
- **落盘目标可插拔**：`ILogSink`（`write(record)`），宿主提供文件实现，测试提供内存实现；
- **换行与控制字符转义**（防日志注入，REF-002 B6）：记录里的 `\n` / `\r` / 控制字符转义后写出；
- **隐私**：不记录 IP / 账号 ID / 硬件指纹（C-016）。格式化只接受字符串，类型层面就不给记对象的入口。

---

## 8. `--self-test` 输出（D-9）

`bloodcoop-host --self-test` 输出**单行 JSON**（便于 CI 解析）：

```json
{
  "overall": "pass",
  "protocol_version": 1,
  "config_source": "absent",
  "modules": [
    {"id": "mod.transport", "source": "remote", "state": "ready",
     "health": "Ok", "unavailable_reason": null, "tick_timeouts": 0, "tick_failures": 0}
  ],
  "log": {"dropped_diagnostic": 0, "dropped_audit": 0}
}
```

判定规则（写死，避免各处理解不同）：

- `overall = "fail"` 当且仅当存在 `state != "ready"` 的模块，**或** `dropped_audit > 0`；
- `overall = "pass"` 时退出码 0；`fail` 时退出码非 0（任务书 §41.1 T-014）；
- 「required 项」的定义：本版本里**全部已注册模块**都是 required（核心模块清单未裁决，
  见 `01-kernel.md` §4 → 在裁决前不做区分，避免自行发明一个清单）。

---

## 9. 验收（可判定）

| # | 判据 | 对应 | 本轮 |
|---|---|---|---|
| A-1 | 一个模块 `init` 失败 → 其余模块全部完成初始化，且失败者的 `unavailable_reason=init_failed` | §41.1 T-005 | 待实现 |
| A-2 | 注入一个 `tick` 耗时超过阈值的模块 → 该模块被熔断，其余模块继续 tick | §41.1 T-005 | 待实现 |
| A-3 | 连续 `tick` 失败达阈值 → `tick_failures` 熔断，且**只标记一次** | D-3 | 待实现 |
| A-4 | 通道关闭回调 → 该 remote 模块 `channel_closed`，且幂等（重复回调不重复计数） | 07 §1.3 | 待实现 |
| A-5 | 心跳计数冻结超过 N → `heartbeat_stale` + 恰好一次终止请求 | 07 §1.3 | 待实现 |
| A-6 | 健康聚合正确：三种 `Health` 与两种来源都能在报告里区分 | §41.1 T-006 | 待实现 |
| A-7 | `--self-test` 输出可被解析的 JSON，required 失败时退出码非 0 | §41.1 T-005/T-014 | 待实现 |
| A-8 | 事件总线：队列满不阻塞、重入被拒绝且计数 | D-6 | 待实现 |
| A-9 | 日志：诊断丢并计数、审计不静默丢、控制字符被转义 | D-7 | 待实现 |
| A-10 | **Kernel 内无业务逻辑**：不得出现死亡 / 世界重置 / 敌人 / 掉落 / PvP / 存档机制 | `01-kernel.md` §3 | 待实现（人工审计 + 代码评审） |
| A-11 | **不得声称 Gate 第 9 项已满足**：文档与自检输出里都不出现该说法 | 06 §5 | 待实现（文本检查） |

---

## 10. 待定 / 未核实

| 项 | 现状 | 归属 |
|---|---|---|
| `tick` 超时阈值（暂定 50ms） | 任务书未给数值 | T-013 实测后定 |
| 健康巡检周期（暂定 500ms） | 任务书未给 | T-013 |
| 连续失败阈值（暂定 3） | 任务书未给 | T-013 |
| 熔断后的恢复（本规格取「粘性、不自动恢复」） | 任务书未给 | T-005 已定；若将来要自动恢复，需建 Issue |
| 事件总线容量（暂定 64 条） | 任务书未给 | T-005 已定数值，待实测 |
| 核心模块清单 | 任务书提到「非核心模块」但没有清单 | **需要裁决**（`01-kernel.md` §4） |
| `config/modules.yaml` 的字段（含 `placement`） | 由 T-008 产出 | T-008 |
| 两个可执行目标与模块隔离断言 | 方案 B 已裁决 | T-009 |
| `--self-test` 的最终字段 | 本规格给形状，未冻结 | T-014 |

---

## 11. 依据

- 任务书 §3.6（原则 1/2/5/6、模块注册表 §3.6）、§3.7（分层与出口条件、Gate 6）、§41.1（T-005/T-006/T-009/T-014）、§41.3（全局 Gate）
- `docs/architecture/01-kernel.md`、`06-process-boundary.md`、`07-kernel-seam.md`、`08-ipc.md`
- `docs/references/REF-001`（进程监督）、`REF-002`（日志管线）
- `src/interfaces/imodule.h`、`module_context.h`、`errors.h`
- Issue #66（T-005 卡）
