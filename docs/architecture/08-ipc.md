# 08 IPC 层规格（T-021）

> **用途**：定死进程外模块与游戏进程之间的通信、代理/桩、可跨边界类型与辅助进程生命周期。
> **前提**：`docs/architecture/06-process-boundary.md`（进程边界，Human Owner 裁定）、
> `docs/architecture/07-kernel-seam.md`（Kernel 与 IPC 的接缝）、`docs/standards/许可与合规.md`。
> **依据**：按 `docs/references/REF-001` / `REF-002` 的先例核对；K-7 / K-10 裁决（`01-kernel.md`）。
> **状态**：规格草案（v1，待 T-021 实现校验）；数值未标「已定」的一律为**暂定/待定**，实现不得当作任务书规定。
> **最后更新**：2026-10

---

## 1. 目标与非目标

**目标**

1. 进程外模块（Layer 0 五模块 + `mod.observability`）能被 Kernel 加载、巡检、熔断，行为与进程内模块一致；
2. 提供 `ITransport` / `ISession` 的代理与桩；调用方代码不变（06 §7）；
3. 定义可跨边界类型白名单与序列化约束；
4. 定义辅助进程生命周期：启动、握手、心跳、崩溃/卡住判定、终止、重启（含退避）；
5. 定义日志经 IPC 回传（辅助进程零文件权限）与「可丢 / 不可丢」分级；
6. 为 T-005 / T-006 / T-009 / T-013 提供可判定接口与故障注入点。

**非目标**

- 具体游戏功能（由模块卡负责）；
- 跨边界调用的性能实测（T-013 负责，本规格只给形状与预算位置）；
- 进程层沙箱加固的具体 API（06 §5.3 是加固层，实现随 T-021 落地，另见 LOADER/平台适配）。

## 2. 决策摘要

| # | 决策 | 依据 |
|---|---|---|
| D-1 | 数据面用**共享内存环形缓冲**，控制面用**控制通道**（事件/握手/终止请求）；不为每次调用做一次系统调用 | 06 §4.1；REF-002 B2 |
| D-2 | 调用必须**批量**：按帧或固定间隔提交；`rtt_ms()` / `member_count()` 这类查询在游戏进程侧**本地缓存**，由辅助进程定期推送 | 06 §4.1 / §4.2 |
| D-3 | 禁止每帧每实体跨边界调用（路径的跨边界次数不得与「实体数 × 帧数」成正比） | 06 §4.3 |
| D-4 | 代理是实现细节：调用方仍拿 `ITransport*` / `ISession*`，由代理序列化；`IGameplay` 不需要代理 | 06 §7.1 |
| D-5 | 可跨边界类型**白名单**：`Error` / `Health` / 定长整数 / `const char*`（长度受限）/ `span<const uint8_t>`；**指针与引用一律不可跨边界** | 06 §7.1 / §7.2 |
| D-6 | 辅助进程内的模块**不得**用 `find_service<T>()` 找其他进程的模块；依赖由 IPC 路由表解析 | 06 §7.2 |
| D-7 | 生命周期状态机：`starting → ready → stale → terminating → gone`；「已死」以操作系统可等待状态为准，通知只作快速路径；心跳只判「卡住」 | REF-001 A4/A5/A7 |
| D-8 | 心跳节奏必须与卡住阈值 **N 明确对齐**（N 暂定 3000ms，实现写成具名常量并标「暂定」） | 07 §1.4；REF-001 A5 |
| D-9 | 终止：Kernel 只发请求；执行分「优雅停止 → 宽限期（暂定 1000ms）→ 硬杀」；按作业对象/进程组整组回收；**不按 PID** | REF-001 A1/A2/A3 |
| D-10 | 重启：指数退避 + 封顶 + 抖动 + 稳定窗口后重置 + 单位时间启动上限；达到上限进入显式失败态 | REF-001 A6 |
| D-11 | 日志回传走 IPC；每条记录带**可丢 / 不可丢**类别；诊断可丢并计数，审计类（含 `[SECURITY]`）不静默丢 | K-7；REF-002 B3/B4 |
| D-12 | 两个产物（host / aux）**握手核对 IPC 协议版本**，不匹配 fail-closed（拒绝启动） | 07 §2 方案 B |
| D-13 | 通道不可用时，代理**立刻**返回 `Unavailable`，不得阻塞 | 07 §1.4 |

## 3. 进程与目标结构（与 T-009 的接口）

```text
bloodcoop-host（游戏进程）
├─ Kernel（模块宿主；只发请求、只读心跳计数）
├─ 进程内模块（11）
├─ T-021：IPC 宿主侧（代理 / 桩 / 通道管理 / 生命周期监督者 / 日志接收）
└─ mod.game_hook（运行时插件入口 bbgame_init）
bloodcoop-aux（辅助进程）
├─ T-021：IPC 辅助侧（真实模块实现 / 桩的反向）
└─ mod.transport / discovery / relay / crypto / session_core / observability
```

- 目标结构按 K-10：**机制**（创建/终止/作业对象/受限令牌）收在 T-021 内部的窄平台适配层，
  `src/platform/` 有任务卡与所有权条目时再迁移，接口不变；
- T-009 按 `placement`（`config/modules.yaml`）把模块分派到两个目标；
  两侧版本不匹配时 **fail-closed**。

## 4. 协议（控制面 + 数据面）

### 4.1 控制通道

| 消息 | 方向 | 说明 |
|---|---|---|
| `Hello { protocol_version, host_build_hash }` | aux → host | 启动握手；版本不匹配 → host 拒绝并终止 |
| `Ready { module_ids[] }` | aux → host | 模块初始化完成，进入 `ready` |
| `Heartbeat { counter }` | aux → host | 单调计数；**节奏 ≤ N/3**（N 暂定 3000ms） |
| `CallResult { call_id, error, payload_len }` | aux → host | 数据面完成通知 |
| `LogRecord { durability, module_id, level, ts, payload }` | aux → host | 见 §6 |
| `Stop { grace_ms }` | host → aux | 请求优雅停止 |
| `Kill` | host → aux | 宽限期后仍未退出时的强杀（由平台适配执行） |

### 4.2 数据面

- 共享内存环形缓冲：单生产者/单消费者，固定容量（**容量数值待定**，由 T-021 按实测与背压策略定）；
- 每条调用带 `call_id`（单调递增）+ 长度前缀；批量提交（≤ 每帧一次）；
- 配额与背压：缓冲满是可判定的失败（返回 `Unavailable` 或排队到下一批，具体由 T-021 定，但**不得阻塞调用方 tick**）。

### 4.3 可跨边界类型（白名单）

- 允许：`Error` / `Health` 枚举、定长整数、长度受限的 `const char*`、`span<const uint8_t>`；
- 禁止：裸指针、引用、`std::function`、STL 容器、结构体含指针（序列化层必须显式拒绝并在测试中覆盖）。

## 5. 生命周期与监督

### 5.1 状态机

```text
starting ──Ready──▶ ready ──心跳停滞──▶ stale ──请求终止──▶ terminating ──退出──▶ gone
    │                  ▲                                                     
    └──就绪超时────────┴──────────── 启动失败（fail-closed，报告 reason）
```

- **就绪超时**：任务书未给数值 → 暂定（T-021 定），超时判启动失败，不是「卡住」；
- **卡住判定**：观察到第一次心跳变化之后才武装；N 暂定 3000ms（与心跳节奏对齐，见 5.2）；
- **幂等**：通道关闭与心跳卡住归约到同一个「标不可用（带 reason）」函数，不得重复计数 / 日志 / 事件；
- **原因字段**：自检报告必须能区分 `channel_closed` / `heartbeat_stale` / `init_failed`（07 §1.5）。

### 5.2 心跳与被监督者职责

- 辅助进程只发心跳与阶段事件，不判定自己是否卡住；
- 心跳节奏 ≤ N/3（给调度抖动留余量）；节奏与 N 的关系必须写进本规格与实现常量；
- host 侧每 tick 读共享内存计数（零 IPC 成本），不轮询进程句柄。

### 5.3 终止（Kernel 只请求，T-021 执行）

1. host 收到 `requestTerminate(channel)` → 状态置 `terminating`；
2. 发送 `Stop { grace_ms }`；宽限期暂定 1000ms；
3. 宽限期后仍存活 → 平台适配层按**作业对象 / 进程组**整组回收（不用 PID；Windows 用 Job Object + `KILL_ON_JOB_CLOSE` 防孤儿）；
4. 「已死」以可等待的作业/进程状态为准；completion port/通道关闭通知只作快速路径；
5. 终止失败要重试并记录；模块保持 `Unavailable`，不得静默。

### 5.4 重启（显式失败优先）

- Kernel **不自动重启**；重启策略归 T-021；
- 有界重启：首次立即，之后指数退避（10s / 20s / 40s… 封顶 300s），**抖动**，稳定运行 10 分钟后重置；
- 单位时间启动上限（`StartLimit` 语义）；达到上限 → 进入显式失败态并上报；
- 必须定义「哪些退出算失败」（避免把用户主动退出也重启）。

## 6. 日志回传（辅助进程零文件权限）

- 辅助进程**一行文件操作都不写**；日志记录经 IPC 送回 host，由 **Kernel 的日志设施**落盘（K-7）；
- 记录头带 `durability`：
  - `diagnostic`：队列满时可丢，**必须计数**（`dropped_records` 暴露给自检/指标）；
  - `audit`：**不静默丢**（短背压或显式失败态），`[SECURITY]` 强制归此类；
- IPC 记录结构要能承载：`module_id`、级别、时间、消息、可选结构化字段；不得包含隐私数据（C-016）；
- host 是唯一格式化/落盘者；换行与控制字符必须转义（防日志注入，REF-002 B6）。

## 7. 与 Kernel 的接口（IAuxLiveness）

按 `07-kernel-seam.md` §1.2 的形状，由 T-021 提供实现、测试提供假实现：

```text
class IAuxLiveness {
    using ChannelClosedFn = void (*)(void* user, ChannelId channel) noexcept;
    Error watch(ChannelId channel, ChannelClosedFn fn, void* user) noexcept;
    std::uint64_t heartbeatCounter(ChannelId channel) const noexcept;  // 本地读
    void requestTerminate(ChannelId channel) noexcept;                 // 只发请求
};
```

- `requestTerminate` 非阻塞、幂等；Kernel 不假设进程立刻消失；
- 接口放 `src/kernel/` 的内部头文件（不进 `src/interfaces/`，冻结接口不需要改）。

## 8. 验收（可判定）

| # | 判据 | 对应 |
|---|---|---|
| A-1 | `kill` 辅助进程 → 游戏进程存活、自检报告 Layer 0 全 `Unavailable`，且 `unavailable_reason=channel_closed` | 06 §5.4；T-013 |
| A-2 | 冻结心跳 → 判定为 `heartbeat_stale`，触发终止请求，且**只触发一次**（幂等） | D-7/D-9 |
| A-3 | 跨边界调用次数不与「实体数 × 帧数」成正比（统计计数断言） | 06 §4.3 |
| A-4 | 通道不可用时，代理调用**立即**返回 `Unavailable`（不阻塞 tick） | 07 §1.4 |
| A-5 | 版本不匹配 → 拒绝启动（fail-closed），日志给出两侧版本 | D-12 |
| A-6 | 队列满时：`diagnostic` 丢并计数，`audit` 不丢（或进入显式失败态） | D-11 |
| A-7 | 辅助进程崩溃前记录的事件能在 host 日志中找到（证明日志走了 IPC） | 06 §5.4 |

## 9. 待定 / 未核实

| 项 | 现状 | 归属 |
|---|---|---|
| 环形缓冲容量与背压参数 | 无数据 | T-021 / T-013 |
| 卡住阈值 N（暂定 3000ms）与心跳节奏 | 任务书未给 | T-021（本规格给关系：节奏 ≤ N/3） |
| 就绪超时 | 任务书未给 | T-021 |
| 优雅停止宽限期（暂定 1000ms） | 任务书未给 | T-021 |
| 重启退避/上限的具体数值 | 任务书未给 | T-021 |
| 运行时插件入口（`BB_GAME_LIB`）在 Windows 端口的落地 | 设计输入见 `scripts/m0/r2_probe/LOADER-PATCH.md`，未实测 | M0-R2 |
| 跨边界调用的性能上限 | 任务书 §3.5 只给总量 | T-013 实测 |

## 10. 依据

- `docs/architecture/06-process-boundary.md`、`07-kernel-seam.md`
- `docs/references/REF-001`（进程监督与终止）、`REF-002`（日志管线）
- `docs/architecture/01-kernel.md`（K-7 / K-10 裁决）
- Issue #35（T-021 卡）、`docs/standards/项目复盘与风险清单.md`（M0）、`docs/standards/许可与合规.md`
- 任务书 §3.5（性能预算）、§3.6、§3.7、§41.3 全局 Gate 第 9 项
