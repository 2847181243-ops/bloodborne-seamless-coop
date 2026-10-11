# 09 Kernel / Module Host 规格（T-005）

> **用途**：把 `01-kernel.md` §2 的六项职责落成可判定的接口、状态机与数值，作为 T-005 实现的依据。
> **前提**：`docs/architecture/01-kernel.md`（边界与不做清单）、`06-process-boundary.md`（进程边界，
> Human Owner 裁定）、`07-kernel-seam.md`（Kernel 与 IPC 的接缝，**已定**）、`08-ipc.md`（T-021，已实现）。
> **依赖的冻结接口**：`src/interfaces/imodule.h`、`module_context.h`、`errors.h`、`version.h`（**未改动一个字节**）。
> **状态**：v2，与实现一致（v1 是设计草案，其中几处与既有裁决冲突，见 §11.2 的改动记录）。
> **最后更新**：2026-10

---

## 1. 目标与非目标

**目标**

1. **加载**：模块表 → 实例 → `init`；单项失败**不阻断**其他模块；
2. **熔断**：`tick` 超时、连续失败、进程外通道失效，三种都归约到「标不可用（带原因）」；
3. **健康巡检与聚合**，自检能区分「为什么不可用」；
4. **事件总线**：定死投递语义；
5. **宿主能力**：`ILogger`、`IServiceRegistry` 的实现（含 owner 归属与 shutdown 撤销）；
6. `bloodcoop-host`，`--self-test` 输出 JSON；
7. 给 T-006（夹具与用例）与 T-014（字段冻结、`scripts/self_test.sh`、CI 解析）留下接缝。

**非目标**

- 15 个模块的实现（T-007）；`config/*.yaml` 的内容与解析（T-008）；
- 两个可执行目标、按 `placement` 分派、模块隔离断言（T-009，`07-kernel-seam.md` §2 已裁决方案 B）；
- 拉起辅助进程（T-021 的窄平台适配层 + T-009 启动器）；
- `--self-test` 字段冻结（K-9 归 T-014）；
- 任何游戏机制、Hook、密码学、传输。

---

## 2. 决策摘要

| # | 决策 | 依据 |
|---|---|---|
| D-1 | Kernel 眼里只有一种模块（`IModule`），两个**来源**：`local`（进程内，持有 `IModule*`）与 `remote`（进程外，只有 ID + 通道） | 07 §1.1（已定） |
| D-2 | 只依赖 `IAuxLiveness` 三件事：通道关闭回调、本地读心跳计数、请求终止。**不碰进程句柄** | 07 §1.2；K-10（已裁决） |
| D-3 | **粘性熔断**：一旦不可用，本会话不自动恢复；唯一恢复手段是显式 `resetCircuit()`，且对通道关闭/心跳停滞**拒绝** | K-1/K-3；任务书 §41.1 T-005「停止调用它」 |
| D-4 | 生命周期状态与模块自报 `Health` **分开存**：`Unavailable` 既可能是「没实现」也可能是「被熔断」，自检必须能分 | K-4；`imodule.h` 的 `Health` 是冻结值域 |
| D-5 | **静态链接 + 编译期注册表**，不做运行时动态加载 | K-5；任务书只给 CMake 目标结构 |
| D-6 | 事件总线：`publish` **同步直接投递**（不排队、不切线程），单线程不加锁；**重入被拒绝并计数**；处理器异常不传播到其他订阅者 | K-8 |
| D-7 | 日志由 Kernel 提供**机制**（`ILogger` 实现 + 可插拔 sink），内容与策略归 `mod.observability` | K-7（已裁决） |
| D-8 | 进程内模块的 `tick` 超时**只能在返回后判定**（Kernel 无法抢占）；进程内段错误**拦不住**，不得声称「Gate 第 9 项已满足」 | K-6；06 §6 |
| D-9 | 自检每个模块带 `source` 与 `unavailable_reason`；`unavailable_reason` 的 `None` 输出成 JSON `null`（不是 `"none"`） | 07 §1.5 |
| D-10 | 配置缺失（T-008 未产出 `modules.yaml`）时：宿主给 Kernel 一张**空表**，自检 `overall = degraded`、退出码 2 —— **不假装读过配置，也不假装通过** | §41.1 的卡序（T-005 不能依赖 T-008） |
| D-11 | 「心跳停滞」的**判定归 Kernel**：Kernel 是 T-021 `ChannelWatchdog` 唯一的生产调用方。IPC 层只提供判定逻辑与心跳计数，**自己不判** | 07 §1.2②；见 §5.2 |

---

## 3. 模块的来源与状态

### 3.1 三个维度分开存（D-4）

| 维度 | 取值 | 谁决定 |
|---|---|---|
| `source` | `local` / `remote` | 模块表（T-008/T-009 填；T-005 为空表） |
| `initialized` / `circuitOpen` | 两个布尔 | Kernel |
| 模块自报 `Health` | `Ok` / `Degraded` / `Unavailable`（冻结值域） | 模块自己（`IModule::health`） |
| `unavailable_reason` | 见 §3.2 | Kernel |

**为什么不合成一个 `state` 枚举**：草拟本规格时曾写过
`registered → initializing → ready → unavailable → shutdown`，但那与冻结接口的语义重叠
（`Health::Unavailable` 的定义已经涵盖「未初始化 / 已熔断 / 尚未实现」），
两套状态并存就一定会出现「谁说了算」的问题。最终按 07 §1.5 要求的字段落地：
`source` + `unavailable_reason` 两个新字段，其余沿用既有布尔。

### 3.2 `unavailable_reason`

**直接 `using UnavailableReason = bbcoop::ipc::UnavailableReason`** ——
取值与 `08-ipc.md` 逐字一致，不另写一份枚举（两处各写一份必然漂移）。

| 取值 | 含义 | 由谁设置 |
|---|---|---|
| `None`（JSON `null`） | 可用，或**尚未就绪**（remote 还没完成握手） | — |
| `init_failed` | `init` 返回失败（或工厂返回 `nullptr`） | 加载 |
| `tick_timeout` | 单次 `tick` 实测耗时超过阈值 | 熔断 |
| `tick_failures` | 连续 `tick` 返回失败达到阈值 | 熔断 |
| `channel_closed` | 进程外模块的通道关闭（含辅助进程被杀） | IPC 回调 |
| `heartbeat_stale` | 心跳计数在 N 内没有变化 | 巡检 + 终止请求 |

### 3.3 状态与幂等

```text
local :  未初始化 ──init ok──▶ 已初始化 ──tick 失败/超时达阈值──▶ 熔断（粘性）
             └──init 失败──▶ unavailable(init_failed)，永不 tick

remote:  已登记(未就绪) ──aux 握手 Ready──▶ 就绪 ──通道关闭 / 心跳停滞──▶ 熔断（粘性）
```

- 进入「不可用」**幂等**：重复的通道关闭回调不重复计数、不重复记日志
  （证明在 `ModuleStatus::unavailableMarks` 上：只有**真实迁移**才加一）。
- 熔断后**不再调用**该模块的 `tick`（任务书 §41.1 T-005 验收原文）。
- `shutdown` 幂等，且 `init` 曾经失败的模块也要收到它（冻结接口要求）。

---

## 4. 生命周期

```text
Kernel kernel(policy);
kernel.setLiveness(ipc);        // 可选：没有 IPC 也能跑（remote 只登记不判卡住）
kernel.loadTable(table);        // 逐项 create + init；remote 项走 addRemote
kernel.tick(nowMs);             // local 逐个 tick → remote 心跳判定 → 按周期巡检
kernel.selfTestReport();        // 先巡检，再快照
kernel.shutdownAll();           // 后进先出；撤销各模块发布的服务
```

- `addModule(unique_ptr<IModule>, required)` 与 `loadTable` 走**同一条**状态机
  （测试与故障注入需要留住可观察的实例）。
- 表里的 ID 与实例 `id()` **必须逐字相等**，否则拒绝该项并报错 ——
  表将来是生成的，名字对不上说明生成物或命名约定坏了，不能让「表里一个名字、跑起来另一个」静默通过。
- `tick` 顺序 = 注册表顺序（确定性）；单个模块失败不中断循环。

---

## 5. 熔断与心跳

### 5.1 三个触发源

| 触发 | 判定 | 暂定数值 |
|---|---|---|
| 单次 `tick` 超时 | 调用**返回后**测得的耗时 > 阈值；模块返回 `Ok` 也不能免 | `kDefaultTickTimeoutMs = 50` |
| 连续 `tick` 失败 | 连续返回非 `Ok` 达到阈值（成功一次清零） | `kDefaultFailureThreshold = 3` |
| 通道关闭 | IPC 回调（`watch` 注册，**不轮询进程句柄**） | 无数值 |
| 心跳停滞 | 计数在 N 内无变化 | N = `ipc::kHeartbeatStaleMs`（3000，暂定）；Kernel 不再写第二份 |

巡检周期 `kDefaultHealthSweepPeriodMs = 1000`（K-2，暂定）。

### 5.2 心跳判定的 owner（D-11）

- **判定在 Kernel**：每个 remote 槽位持有一个 `ipc::ChannelWatchdog`，Kernel 每 tick
  用 `IAuxLiveness::heartbeatCounter()` 的本地读喂它，拿到 `HeartbeatStale` 后
  **标记 + 请求终止一次**。
- **IPC 层不自己判**：`ChannelWatchdog` 与 `LivenessRegistry::markStale` 是逻辑与状态，
  没有自走的定时器。这样只有一条路径会调 `requestTerminate` ——
  两边都判会导致重复终止与重复日志，而且第二条路径只表现为「判定无效」，很难查。
- 武装规则沿用 IPC 的既有实现：**观察到第一次心跳变化之后**才可能判「卡住」；
  从未变过的计数是「启动失败」，不是「卡住」（07 §1.3 规则 1）。
- 终止的执行不在这里：Kernel 只发请求（K-10）。

### 5.3 进程内拦不住什么（D-8，必须原样保留）

进程内模块的 `tick` 是普通函数调用，Kernel **无法抢占**：一个真正卡死不返回的模块会把
游戏进程一起拖住，「超时」只是**事后**判定。段错误同理，`try/catch` 拦不住
（`noexcept` 边界上抛异常会直接 `std::terminate`）。
进程外的 6 个模块由**进程隔离**兜住，进程内的 11 个不行。
**禁止**在任何报告或自检输出里写「Gate 第 9 项已满足」。

---

## 6. 事件总线（K-8）

```text
subscribe(topic, handler, user)      handler = void(*)(void*, const Event&)，不写 noexcept
unsubscribe(topic, handler, user)    没订阅过返回 Refused（不静默成功）
publish(topic, payload, nowMs)       同步投递；返回 Internal 表示至少一个处理器失败
```

| 语义 | 取值 |
|---|---|
| 投递 | **同步直接调用**订阅者，不排队、不切线程 |
| 线程 | 单线程，不加锁；跨线程使用属未定义行为 |
| 重入 | 处理器内再 `publish` 返回 `Refused` 并计数（无穷递归是内核级故障） |
| 处理器失败 | 单个处理器失败不影响其他订阅者（故障不传播） |
| 订阅上限 | `kMaxEventSubscriptions = 64`，满了 `Refused` |
| 重复订阅 | 同一 `(topic, handler, user)` → `Refused`（避免同一条事件投两次） |

**已知缺口**：冻结接口**没有**给模块的事件入口（`module_context.h` 只有 `ILogger` 与
`IServiceRegistry`），所以总线目前只有 Kernel 与宿主能用。要开放给模块必须走
任务书 §52-c 的冻结接口变更流程，T-005 不擅自改 `src/interfaces/`。

---

## 7. 日志（K-7）

**已实现**：`KernelLogger` 实现冻结的 `ILogger`；最小级别过滤；行在固定缓冲区里
拼好（`noexcept` 路径上不分配）；sink 可注入（测试用内存 sink，宿主默认 stderr）；
记账 `writtenCount()` / `droppedCount()`；**stdout 留给自检 JSON**，日志一律走 stderr。

**未实现（K-7 的机制面还没做完）**：

| K-7 要求 | 现状 | 为什么现在不做 |
|---|---|---|
| 有界队列 | 无（同步写 sink） | 还没有文件 sink；先加一个没人消费的队列只是摆设 |
| 单写线程 | 无 | 同上；主循环单线程，引入线程就要一并解决主循环模型 |
| 落盘 | 无文件实现 | 落盘目标与辅助进程日志的接收端（`ipc::LogSink`）要在 T-014 接线时才连起来 |
| 可丢/不可丢分类 | 无 | 与队列同时落地才有意义 |

这一条**记在 §10**，不由「已经实现了日志」含混过去。辅助进程日志经 IPC 回传到宿主落盘
（06 §5.2）也还没接线 —— `ipc::LogSink` 注释里写的「另一端是 Kernel 的日志设施」目前是空的。

---

## 8. `--self-test` 输出与退出码

`bloodcoop-host --self-test` 输出 JSON（stdout），日志走 stderr：

```json
{
  "interface_version": 100,
  "overall": "degraded",
  "modules": [
    {"id": "mod.normal", "source": "local", "health": "Ok", "initialized": true,
     "circuit_open": false, "required": true, "last_error": "Ok",
     "unavailable_reason": null, "tick_calls": 2, "tick_timeouts": 0}
  ],
  "skipped": [{"id": "mod.save", "reason": "not_implemented"}],
  "version": "0.1.0"
}
```

**`overall` 判定**（写死，避免各处理解不同）：

| 取值 | 条件 |
|---|---|
| `fail` | 至少一个 **required** 模块不是 Ok（未初始化 / health 非 Ok / 已熔断） |
| `degraded` | 没有 required 失败，但至少一个已加载模块不是 Ok，**或一个模块都没加载** |
| `pass` | 所有已加载模块都是 Ok，**且至少有一个模块** |

「空表也算 pass」是被显式排除掉的：空注册表若算 pass，T-014 的 Gate 6 与
T-012 的「禁用非核心模块后自检退出码 0」在框架期就恒真 —— 那正是 L1e 要防的假通过。

**退出码**沿用仓库既有约定（`tools/bbcoop.py`、`tools/preci/README.md`、
`docs/standards/重构对照-保留与重建.md` §2.4）：

| 码 | 含义 |
|---|---|
| 0 | `overall = pass` |
| 1 | `overall = fail`（required 失败；任务书 §41.1 T-014 的验收） |
| 2 | `overall = degraded`：**没有失败，但不等于通过** |
| 3 | 用法错误 / 无法启动 |

`required` 的来源：核心模块清单任务书没有给（`01-kernel.md` §4），
**在裁决之前不自作主张地区分**：表里给了 `requiredFlags` 就用它，没给就一律为 `false`
（这样「模块没实现」不会把 CI 判红），remote 项一律为 `true`（辅助进程没起来时整体不可用是真失败）。

> ⚠️ 字段名与大小写**尚未冻结**（K-9 归 T-014）：本规格给的是 T-005 实际输出的形状；
> `.github/ISSUE_TEMPLATE/fault_injection.yml` 里的示例是小写且写死 `degraded`，
> 两者不一致的部分由 T-014 定稿时统一。

---

## 9. 验收（可判定）与覆盖

| # | 判据 | 本轮（T-005） |
|---|---|---|
| A-1 | 一个模块 `init` 失败 → 其余全部完成初始化，失败者 `init_failed` 且**永不 tick** | **是**（单测 + 负例②） |
| A-2 | `tick` 耗时超过阈值 → 熔断，其余模块继续 | **是**（单测 + 负例③） |
| A-3 | 连续失败达阈值 → `tick_failures`，且**只标记一次** | **是** |
| A-4 | 通道关闭 → 该通道上**全部** remote 模块 `channel_closed`，且幂等 | **是**（单测 + 负例④⑤） |
| A-5 | 心跳冻结超过 N → `heartbeat_stale` + **恰好一次**终止请求 | **是** |
| A-6 | 健康聚合正确（三种 `Health` + 两种来源都能区分） | **是** |
| A-7 | `--self-test` 输出可解析 JSON；required 失败退出码非 0 | **是**（JSON 由单测校验 + 人工跑宿主核对） |
| A-8 | 事件总线：同步投递、重入被拒绝并计数、订阅上限 | **是** |
| A-9 | 日志：级别过滤与计数 | **是**（K-7 的队列/分类/落盘**未实现**，见 §7、§10） |
| A-10 | Kernel 内无业务逻辑（无死亡/世界重置/敌人/掉落/PvP/存档机制） | **人工审计 + 评审** |
| A-11 | 不得出现「Gate 第 9 项已满足」的说法 | **是**（代码与文档均无此说法） |

---

## 10. 待定 / 未落地

| 项 | 现状 | 归属 |
|---|---|---|
| `tick` 超时阈值（暂定 50ms） | 任务书未给数值 | T-013 实测后定 |
| 巡检周期（暂定 1000ms）、连续失败阈值（暂定 3） | 任务书未给 | T-013 |
| 熔断后的恢复（本规格取粘性、不自动恢复） | 任务书未给 | T-005 已定；要改需建 Issue |
| 心跳停滞 N（3000ms）与就绪超时（5000ms） | 属 IPC 协议 | T-013 / T-021 |
| **K-7 的日志机制面**：有界队列 / 单写线程 / 落盘 / 可丢不可丢分类 | **未实现** | T-014（与 `ipc::LogSink` 的接收端一起接线） |
| 辅助进程日志经 IPC 回传到宿主落盘（06 §5.2） | **未接线** | T-014 |
| 谁拉起辅助进程 | `IAuxLiveness` 没有「启动」；`IProcessHost::start` 在 T-021 | T-009 启动器 |
| remote 模块「未就绪」时永远进不了 `pass` | 上一条的直接后果：现在跑 `--self-test` 就是 `degraded`（退出码 2） | T-007/T-008/T-009 落地后自然变 pass |
| `config/modules.yaml` 的字段（含 `placement`） | 由 T-008 产出 | T-008 |
| 两个可执行目标与模块隔离断言 | 方案 B 已裁决 | T-009 |
| `--self-test` 字段冻结（K-9） | 本规格给形状 | T-014 |
| 核心模块清单（O-1） | 任务书提到「非核心模块」但没有清单 | **需要裁决** |
| 06 §6.1 建议的注入方式「tick 抛异常」**不可实现**（`tick` 是 noexcept） | 文档与实现冲突 | 建议建 Issue 修正 06 §6.1 |

---

## 11. 实现进度（随 `task/T-005-kernel-host` 更新）

### 11.1 落地清单

| 文件 | 内容 |
|---|---|
| `src/kernel/include/bbcoop/kernel/policy.h` | 全部暂定数值集中一处；心跳 N 指向 IPC，不复制 |
| `src/kernel/include/bbcoop/kernel/module_table.h` | 表形状（含 `RemoteEntry`）；**不读配置**（那是 T-008） |
| `src/kernel/include/bbcoop/kernel/kernel.h` + `src/kernel.cpp` | 加载、生命周期、健康、熔断、remote 存活、自检报告 |
| `src/kernel/include/bbcoop/kernel/host_services.h` + `src/host_services.cpp` | `KernelLogger`、`ServiceTable`（owner 归属 + 按 owner 撤销）、`ModuleServiceView` |
| `src/kernel/include/bbcoop/kernel/event_bus.h` + `src/event_bus.cpp` | 事件总线（同步、重入拒绝、处理器隔离） |
| `src/kernel/include/bbcoop/kernel/self_test.h` + `src/self_test.cpp` | 自检 JSON 渲染（含 `source` / `unavailable_reason`） |
| `src/kernel/host/main.cpp` | `bloodcoop-host`：`--self-test` / `--help` / `--version` + 退出码 0/1/2/3 |
| `tests/fixtures/fixture_modules.h` | 夹具模块 + **`IAuxLiveness` 假实现**（07 §1.6 要求） |
| `tests/unit/kernel/test_kernel.cpp` | 12 个用例（112 项检查） |
| `tests/unit/test_framework.h` | 断言框架上移一层：两个套件共用一份 |

来源：草案分支 `wip/kernel-draft`（`82bfd7f`）**改造复用**，按 `07-kernel-seam.md` §3 的
8 条改动逐条落实。没有把草稿的 `CMakeLists.txt`（`bbcoop_add_module()` + 隔离断言）、
`tools/gen_module_table.py`、`config/modules.yaml`、四个用例与三个夹具原样搬过来，理由见 §11.2。

### 11.2 与 v1 草案的差异（都是被既有裁决或实测纠回来的）

| 改动 | 原因 |
|---|---|
| 删掉自造的 `state` 枚举，改用 `source` + `unavailable_reason` | 冻结接口的 `Health` 已覆盖「未初始化/已熔断」，两套状态会打架（§3.1） |
| 事件总线从「有界队列 + 每 tick 分发」改成**同步直接投递** | 草稿的实现更简单且没有队列溢出语义要发明；K-8 只要求「语义定死」 |
| 日志从「有界队列 + 单写线程 + 分类」降级为**同步写 sink**，并把缺口写进 §10 | 还没有文件 sink 与消费者；先加队列等于摆设（§7） |
| 巡检周期取 1000ms（v1 写的 500ms） | 避免与 `policy.h` 出现两个数 |
| `overall` 保留 `pass/degraded/fail` 三值，且**空表算 degraded** | 空表算 pass 会让 Gate 6 与 T-012 的判定恒真（§8） |
| 退出码改成仓库既有约定 0/1/2/3（草稿是 0/1/2） | `2` 在仓库里已经是「有未验证项」的意思 |
| 删掉 `KernelPolicy::autoRecovery` 死字段 | 声明了却没有任何代码读它 —— 比没有这个开关更糟 |
| **新增**：`watch()` 注册通道关闭回调 | 负例⑥实测发现：不注册的话 Kernel 只能靠别的路径知道辅助进程死了，与 07 §1.2① 「死亡由操作系统通知」冲突 |
| **新增**：「标不可用」的幂等性用 `unavailableMarks` 计数 | 幂等是 07 §1.3 规则 2 的要求，必须有可证伪的计数 |
