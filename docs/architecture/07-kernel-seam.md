# Kernel 与 IPC 层的接缝规格

> **这份文件回答一个问题**：Kernel 怎么知道进程外的辅助进程还活着、以及它需要 IPC 层提供什么。
>
> **它为什么在这里而不是 `src/kernel/README.md`**：
> `src/kernel/` 属 `task/*` 分支的范围（那里要走 T-005 的任务卡），
> 而这份是**跨任务的接缝设计**，属于架构文档。
> 放在 `src/kernel/` 里会无法在框架类分支上提交。
>
> **前提**：`docs/architecture/06-process-boundary.md`（Human Owner 裁定的进程边界）。
> **最后更新**：2026-10

---

Kernel **只负责**：加载、配置、日志、事件总线、健康巡检、熔断。
**不含任何业务逻辑**（任务书 §3.6 原则 1）。

边界与「不做」清单见 [`docs/architecture/01-kernel.md`](../../docs/architecture/01-kernel.md)；
进程边界（哪些模块在进程外、辅助进程的权限边界）见
[`docs/architecture/06-process-boundary.md`](../../docs/architecture/06-process-boundary.md)。
实现见 T-005。

> **状态**：`src/kernel/` 目前还没有实现。按「全部模块在进程内」写的第一版草稿在分支
> `wip/kernel-draft`（提交 `82bfd7f`），它与 06 的前提冲突，**不可直接合并**；
> 按 06 §7，注册表 / 生命周期 / 事件总线 / 健康巡检 / 熔断 / 自检这几块在进程内照样要。
> 本文件是**重写 T-005 之前的设计输入**，不是已实现的行为。

---

## 1. 待接缝：Kernel 从哪里知道「辅助进程还活着」

**结论先行：两个来源，都不需要新增检测周期。**

| 失效方式 | 事实从哪来 | Kernel 侧的动作 |
|---|---|---|
| 辅助进程**死了**（崩溃 / 被 kill） | IPC 通道关闭 —— 操作系统给的信号 | 立刻把该通道上的模块标为不可用 |
| 辅助进程**卡住但没死** | 心跳计数在 N 毫秒内没有变化 | 标为不可用，并**请求**终止该辅助进程 |

N 的具体数值**任务书没有给**（06 §9 原本把它记成「辅助进程的崩溃检测周期」待定）。
**暂定 3000ms**，实现时必须写成具名常量并在注释里标注「暂定」。
**不要**把它写成任务书规定 —— 它与 `tick` 超时阈值、健康巡检周期一样属于待定数值。

为什么杀掉而不是等：卡住的进程无法自愈，留着只占资源，而且它对同一批不可信输入的
处理结果不可信。杀掉之后通道会关，于是退回第一种失效方式，路径收敛。

### 1.1 Kernel 眼里只有一种模块，两种来源

```text
ModuleSource
├── Local   —— 进程内，Kernel 直接持有 IModule*
└── Remote  —— 进程外，Kernel 持有代理；存活事实由 IPC 层经回调驱动
```

`IModule` 不变（06 §7 已确认 v1 头文件不动）。健康巡检与熔断**对两者走同一条代码路径**，
唯一差别是：Remote 模块的「不可用」可以由外部事件设置，而不只能由 `tick` 返回错误。

### 1.2 Kernel 与 IPC 层之间需要的那一小块接口

Kernel 只依赖下面三件事，**不依赖 IPC 的内部实现**（共享内存布局、通知方式都属 T-021）：

| 需要 | 为什么这样切 |
|---|---|
| ① 通道关闭的回调 | 死亡由操作系统通知，Kernel 不轮询 |
| ② 一个可**本地读**的心跳计数 | 每 tick 读共享内存，零 IPC 成本、无系统调用 |
| ③「请终止这条通道的辅助进程」 | Kernel 决定**何时**杀；执行是 IPC / 平台层的事。Kernel 不碰进程句柄，否则会把平台细节带进 Kernel（01-kernel.md K-10） |

形状（示意，命名待定；实现时放 `src/kernel/` 的内部头文件，**不进 `src/interfaces/`**，
因为冻结接口不需要改，且这块只有 Kernel 与 IPC 层关心）：

```text
class IAuxLiveness {                        // 由 T-021 提供实现；测试提供假实现
    using ChannelClosedFn = void (*)(void* user, ChannelId channel) noexcept;

    Error watch(ChannelId channel, ChannelClosedFn fn, void* user) noexcept;
    std::uint64_t heartbeatCounter(ChannelId channel) const noexcept;  // 本地读
    void requestTerminate(ChannelId channel) noexcept;                 // 只发请求
};
```

用函数指针 + `void*` 而不是 `std::function`：回调注册发生在 `noexcept` 边界上，
`std::function` 会分配内存，分配失败在 `noexcept` 函数里逃出去就是 `std::terminate`，
而那正是 §3.6 原则 5 要避免的。

### 1.3 三条必须写进实现的状态机规则（否则会误判或重复处理）

1. **不能一启动就判卡住**：辅助进程要花时间映射共享内存、初始化。
   所以「卡住」判定在**观察到第一次心跳变化之后**才武装起来。
   「辅助进程从来没写过心跳」是启动失败，不是卡住，由 IPC 的握手 / 就绪消息判定
   —— 就绪超时的数值任务书同样没有给，**属 T-021，不在这里填**。
2. **杀进程之后通道也会关**：通道关闭与心跳卡住必须**幂等** ——
   同一个模块第二次被标不可用时，不得重复计数、不得重复记日志、不得重复发事件。
3. **Kernel 不自动重启辅助进程**：06 §9 把「重启后能否恢复会话」记为待定
   （依赖会话状态机）。Kernel 只标不可用；重启策略由 T-021 / T-013 定。

### 1.4 这给 T-021 提了两条必须写进协议的要求

- **心跳写入节奏必须与 N 明确对齐**：若辅助进程自己的心跳间隔大于 N，
  健康进程会被误判成卡住。这个关系由 T-021 写清楚；Kernel 只负责比较「计数是否变化」。
- **代理不得阻塞**：06 §4 已经要求跨边界调用批量、不每帧查询。对 Kernel 还有一条：
  通道不可用时，代理要**立刻**返回 `Unavailable`，不能在那里等 ——
  否则 Kernel 的一次 tick 会被远程调用拖住。`tick` 超时熔断对进程内模块成立，
  对代理也必须成立。

### 1.5 自检报告要能区分「为什么不可用」

T-013 与 06 §5.4 的验收是「kill 掉辅助进程后，自检报告 Layer 0 全部 Unavailable」。
只看状态无法核对「是不是真的因为通道关闭」，所以每个模块的条目要有：

| 字段 | 取值 |
|---|---|
| `source` | `local` / `remote` |
| `unavailable_reason` | `init_failed` / `tick_timeout` / `tick_failures` / `channel_closed` / `heartbeat_stale` / null |

### 1.6 没有真实的 T-021 也要能测（T-006 的前提）

`IAuxLiveness` 的**假实现**（放 `tests/fixtures/`）要能：手工触发「通道关闭」、
把心跳计数冻住或推进、记录「请求终止」的次数。
否则 T-006 的故障隔离用例只能依赖真实 IPC 与真实进程 —— 不可确定性、慢、且无法注入时序。
真进程的验证留给 T-013 的故障注入（kill 辅助进程），两者不重复。

### 1.7 两处需要裁决（我不自行决定）

1. **「终止辅助进程」这个动作归谁**：T-021 的 IPC 层，还是 `src/platform/`？
   Kernel 侧只发请求（见 1.2 ③），不写任何平台调用。
2. **游戏进程这一侧的日志落盘归谁**：06 §5.2 要求辅助进程的日志经 IPC 回传给游戏进程写。
   那么接收并写文件的是谁 —— Kernel 的 `ILogger` 实现（任务书 §3.6 原则 1 把「日志」
   列为 Kernel 职责），还是 `mod.observability` 留在进程内的那一半？
   06 §1 把 `mod.observability` 整个放在进程外，所以「它的进程内半边」目前不存在。
   这与 01-kernel.md K-7（Kernel 与 observability 的日志分工）是同一个问题，
   只是现在多了一个进程边界的前提。

---

## 2. 构建形态：一个可执行文件 + 运行期角色，还是两个独立 target

6 个进程外模块必须在**另一个进程**里运行，于是工程里至少要出现第二个可执行目标
（或者同一份映像的第二个角色）。两种做法的差异如下。

| | 方案 A：一个可执行文件 + 运行期角色 | 方案 B：两个独立 target |
|---|---|---|
| 目标结构 | `bbcoop_kernel` + `bloodcoop-host`（含全部 17 个模块） | `bbcoop_kernel` + `bloodcoop-host`（Kernel + 11 个进程内模块 + 远程代理）+ `bloodcoop-aux`（6 个进程外模块 + IPC 桩） |
| 角色怎么定 | **运行期**：`--role=aux`，宿主用 `CreateProcess` 重新拉起自己并继承通道句柄 | **链接期**：哪些模块进哪个映像由 CMake 决定 |
| 加一个模块要改哪里 | 1) `config/modules.yaml` 加一项（含 `placement`）2) 建模块目录 3) 模块目标创建处加一行 4) 若 remote，T-021 侧补代理。两个角色共用同一张表，靠 `placement` 分流 | 1) `config/modules.yaml` 加一项（含 `placement`）2) 建模块目录 3) `bbcoop_add_module(<id> <dir> local\|remote)` 加一行；CMake 的 placement 与 YAML 不一致时**配置阶段 FATAL_ERROR** 4) 若 remote，T-021 侧补代理 |
| 静态可验证性 | 只能核对表里的 `placement`。**二进制里无法证明**辅助进程不会执行到进程内模块的代码 —— 静态初始化器在映像加载时就跑，与角色无关 | 配置阶段可断言「`bloodcoop-aux` 的链接闭包不含任何 local 模块」；CI 还可用符号表核对 |
| 06 §5「零文件权限」的主防线 | 靠运行期角色约定 —— 与「层出口条件必须转成可判定项」的习惯相反 | 可以做成配置阶段断言 + 符号核对 |
| 版本配对 | 不可能错配（同一份映像） | 两个产物要握手核对 IPC 协议版本，不匹配必须 fail-closed（拒绝启动） |
| 自检（T-014） | 同一二进制两个角色都能 `--self-test` | 两个二进制各自 `--self-test`；host 侧要覆盖进程外模块，必须把 aux 起起来并完成握手 |
| 打包 | 一个可执行文件 | 两个可执行文件（Mod 本来就是多文件分发） |
| 构建成本 | 改一个模块就重编那一个大二进制 | 两侧各自构建；aux 侧不含 hook / 存档代码，映像更小 |
| 加错位置的表现 | 角色选错 → 模块在错误的进程里运行，可能静默 | 配置阶段 FATAL_ERROR，或链接期 undefined symbol |

**我的建议：方案 B。** 决定性的理由不是性能，而是 06 §5.3 把「辅助进程一行文件操作都不写」
定为**代码层面的主防线**：只有 B 能把它变成配置阶段的断言（可判定项），
A 只能把它留成运行期约定。A 的具体风险是真实的：`mod.save`、`mod.game_hook` 这类
进程内模块的静态初始化器会在辅助进程映像加载时执行，与「谁扮演什么角色」无关。

B 的唯一真实代价是两个产物的版本配对，用一次 fail-closed 的握手核对就能兜住；
CMake 侧的重复也不大 —— `bbcoop_add_module()` 多接受一个 `local|remote` 参数、
两个可执行目标各消费一次，模块库目标本身两侧共用。

两种方案下第 1 节的接缝**完全相同**，所以这个选择不阻塞接缝设计。

**已裁决：方案 B**（Lead，2026-10）。T-009 的验收里要落两件事：
① 按 `placement` 把模块分派到两个可执行目标；
② 两侧的 IPC 协议版本握手核对 —— 不匹配必须 fail-closed（拒绝启动），
因为 B 的代价正是两个产物可能版本错配。

---

## 3. 草稿（`wip/kernel-draft`）里可复用的与必须改的

> 说明：这份清单是按草稿代码逐处重新定位的结果，**与上一轮口头报告的那五条编号可能不同**
> （那份原文已不在我的上下文里）。

**可复用**（按 06 §7，进程内照样要）：

- `src/kernel/include/bbcoop/kernel/` 下的 `policy.h`（待定数值集中在一处）、
  `event_bus.h`、`module_table.h`、`host_services.h`（`ILogger` / `IServiceRegistry` 实现）、
  `self_test.h` 与对应 `.cpp`：注册表、生命周期、事件总线、健康汇总、熔断判定、
  定长缓冲（`noexcept` 路径上不分配内存）、自检 JSON 渲染。
- `tests/fixtures/` 的 3 个夹具（正常 / init 失败 / tick 卡住）与 `tests/unit/` 的 4 个用例骨架。
- `src/kernel/CMakeLists.txt` 里的 `bbcoop_add_module()` 与模块隔离断言
  （模块之间互相 `target_link_libraries` 时配置阶段直接 FATAL_ERROR）。
  ⚠️ **但断言现在的覆盖面有洞，不能照搬**：`bbcoop_assert_module_isolation()`
  只在 `bbcoop_add_module()` 返回前扫一遍全局目标清单，
  **最后一次扫描之后加的链接不会被检查** —— 实测：注入一条非法的模块间链接，
  配置阶段不报错。所以 T-009 的验收不能写成「配置阶段有隔离断言」，必须是
  「**注入非法链接后配置阶段必须失败**」，且用例覆盖两种注入位置
  （模块自己的 `CMakeLists.txt` / 上层 `CMakeLists.txt`，都在 `bbcoop_add_module()` 之后）。
  修法方向：用 `cmake_language(DEFER ...)` 把清扫推迟到配置阶段末尾，
  或让每个消费方链接之后再扫一遍。
  判据见 `docs/standards/交接-当前进度与下一步.md` §5 的 L1e 破坏性反向验证：
  **只在自己顺利路径上成立的断言，和没有断言是一回事。**

**必须改**（落到具体点）：

1. `Kernel::loadTable()` 只认「进程内实例」。要加 Local / Remote 分派：
   `ModuleStatus` 增 `source`；Remote 项**不** `addModule`，改为绑定通道与代理。
2. 新增两个入口：`onChannelClosed(channel)` 与 tick 内的 `pollRemoteLiveness(now)`，
   两者归约到同一个内部函数「把该通道上的 Remote 模块标不可用（附原因）」。
3. 熔断原因目前只有 `tickTimeouts` / `consecutiveFailures`，需加
   `UnavailableReason`（含 `channel_closed` / `heartbeat_stale`）并输出到自检 JSON（§1.5）。
4. `bloodcoop_host` 目前**无条件链接全部模块**：草稿 `bbcoop_add_module()` 里的
   `target_link_libraries(bloodcoop_host PRIVATE ${_target})` 对每个模块都执行，
   要按 `placement` 分派到两个目标（§2）。
5. `host/main.cpp` 目前只有 `--self-test` / `--help` / `--version`，需要角色参数（方案 A）
   或第二个入口（方案 B）；自检要覆盖进程外模块，否则 6 个 remote 永远是
   Unavailable，`overall` 上不了 `pass`（T-014 的 Gate 6 会红）。
6. `module_table.h` 的 `ModuleTable` 要带 `placement` 与通道信息，
   生成器 `tools/gen_module_table.py` 要一起产出（它已经在解析 YAML）。
7. 夹具要加 `IAuxLiveness` 的假实现（§1.6）。
8. 草稿注释里「进程内隔离到此为止，段错误拦不住」要按 06 §6 改写成：
   进程外的 6 个由进程隔离兜住，进程内的 11 个拦不住段错误 ——
   **不得**出现「Gate 第 9 项已满足」的说法。

---

## 附：与 `06-process-boundary.md` 的交叉核对

本文件写作时提出的两处 06 文档问题（§6 / §7 的小标题编号与父节不一致；
§9 待定表两行过期）**已在 `d601ffb` 修复**，06 现在是正确状态，无需再处理。
