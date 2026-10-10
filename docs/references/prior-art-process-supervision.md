# REF-001 进程监督与终止（先例核对）

> **用途**：为 **K-10**（`src/platform/` 与 Kernel 的分工）与 **T-021**（辅助进程生命周期）提供外部先例对照。
> **它不是要求**：与任务书、`MAINTAINERS.md`、`AI_POLICY.md` 冲突时以后者为准；先例只用来回答「还有没有更优做法」。
> **最后更新**：2026-10
> **证据等级**：Confirmed = 实际读到一手文件；Likely = 多份一手材料交叉推断；Unverified = 未找到一手来源。
> **引用格式**：`<owner>/<repo>@<默认分支>:<path>`。

## 1. 结论摘要

| # | 先例的通行做法 | 与我们的关系 |
|---|---|---|
| 1 | 控制面只发「请求终止」、记录意图；执行面（每平台监督者）负责优雅停止 → 超时 → 强杀 | 与 K-10 裁决一致（Kernel 只发请求） |
| 2 | 执行面是集中的、可替换的每平台机制（systemd service manager、kubelet + container runtime、Windows 作业对象） | 支持「收在 T-021 内的窄适配层，将来可迁 `src/platform/`」 |
| 3 | 杀「整组」（cgroup / Job Object），不按单个 PID | 采用：辅助进程若再派生进程必须整组回收 |
| 4 | 「已死」必须以可等待的作业 / 进程状态为准；OS 通知可能丢失，只作快速路径；心跳只判「卡住」 | 修正：不能只依赖通道关闭回调 |
| 5 | 有界重启：指数退避、封顶、抖动、成功窗口重置、单位时间启动上限 | 补空白：原倾向未覆盖 |
| 6 | starting / ready / liveness 三者语义分开；ready 失败不重启 | 采用：进入 Kernel 的模块状态机 |
| 7 | 父进程退出不留孤儿：作业对象 + 父死子不孤 | 采用 + 需实验（nested job 风险） |

## 2. 逐条先例

### A1 优雅停止 → 超时 → 硬杀 [Confirmed]

- **做法**：先发终止信号（SIGTERM / STOPSIGNAL），等待停止超时（systemd `TimeoutStopSec`；K8s `terminationGracePeriodSeconds` 默认 30 秒），到点强杀（SIGKILL）。
- **来源**：`systemd/systemd@main:man/systemd.kill.xml`、`systemd/systemd@main:man/systemd.service.xml`；`kubernetes/website@main:content/en/docs/concepts/workloads/pods/pod-lifecycle.md`
- **关键词**：`KillMode` / `TimeoutStopSec` / `SendSIGKILL`；"forcibly terminated by SIGKILL"；"Once the grace period has expired, the KILL signal is sent"。
- **映射**：**采用**。Kernel 发请求 → 平台层等待 → 硬杀；`requested` / `timed-out` / `killed` 三个时点都要留可判定记录。

### A2 按进程组 / 作业对象杀，而不是按 PID [Confirmed]

- **做法**：Linux 用 cgroup，`KillMode=control-group` 时 SIGTERM 给主进程、SIGKILL 给控制组内剩余进程；Windows 用 Job Object，`TerminateJobObject` 终止作业内全部进程（含嵌套子作业）；K8s 对 Pod 内残留进程发 SIGKILL。
- **来源**：systemd.kill.xml；`MicrosoftDocs/sdk-api@docs:sdk-api-src/content/jobapi2/nf-jobapi2-terminatejobobject.md`；pod-lifecycle.md
- **关键词**："all remaining processes in the control group"；"Terminates all processes currently associated with the job ... and all of its child jobs"；"SIGKILL to any processes still running in any container"。
- **映射**：**采用**。辅助进程若再 fork（子工具 / 注入器）必须整组回收；创建作业 / 控制组的代码属于执行面，不在 Kernel。

### A3 父进程退出不留孤儿 [Confirmed]

- **做法**：Windows Job Object 默认把子进程纳入同一作业（除非显式 breakaway）；配合 `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`，最后一个作业句柄关闭时终止全部关联进程。systemd 用 cgroup 保证 stop 后不遗留进程，并明确不推荐 `KillMode=process/none`。
- **来源**：`MicrosoftDocs/win32@docs:desktop-src/ProcThread/job-objects.md`；`MicrosoftDocs/sdk-api@docs:sdk-api-src/content/winnt/ns-winnt-jobobject_basic_limit_information.md`；systemd.kill.xml
- **关键词**："child processes it creates ... are also associated with the job"；"terminate when the last handle to the job is closed"；"processes ... escape the service manager's lifecycle"。
- **映射**：**采用 + 需实验**。Windows 上游戏可能已被启动器放进作业对象；nested job 与 breakaway 限制会改变行为（见 §3 第 4 条）。

### A4 「死了」用 OS 可等待状态，通知只作快速路径 [Confirmed]

- **做法**：Job Object 可绑定 I/O completion port 接收事件；作业在所有进程结束后进入 signaled 状态，可 `WaitForSingleObject` 等待。官方明示 completion port 的通知**不保证送达**（受限通知类除外）。
- **来源**：`MicrosoftDocs/sdk-api@docs:sdk-api-src/content/winnt/ns-winnt-jobobject_associate_completion_port.md`；job-objects.md
- **关键词**："delivery to the completion port is not guaranteed"；"state of a job object is set to signaled when all of its processes are terminated"。
- **映射**：**采用（修正）**。「已死」的判定必须以可等待句柄 / 作业状态为准，通知只作快速路径（即使丢了也能兜住）。这支持把「等待 + 硬杀」隔离成可替换的平台 seam，而不是写进 Kernel。

### A5 「卡住」用心跳 [Confirmed]

- **做法**：systemd `WatchdogSec=` 要求服务定期发 `WATCHDOG=1`（keep-alive ping），超时判失败并按策略重启；服务发 `STOPPING=1` 表示进入退出流程，停止超时后仍未退出则杀掉单元内剩余进程。K8s liveness 探针专门用于「进程还活着但没有进展」（死锁）。
- **来源**：`systemd/systemd@main:man/sd_notify.xml`、`systemd/systemd@main:man/systemd.service.xml`；`kubernetes/website@main:content/en/docs/concepts/workloads/pods/probes.md`
- **关键词**："regularly with WATCHDOG=1 (i.e. the keep-alive ping)"；"doesn't terminate within the stop timeout, all remaining processes ... will be killed"；liveness "catch a deadlock"。
- **映射**：**采用**。辅助进程只发心跳与阶段事件；卡住判定与终止决定在宿主侧，不依赖辅助进程自报「我还好」。

### A6 重启策略：有界 + 退避 + 强度限制 [Confirmed]

- **做法**：K8s `restartPolicy` + CrashLoopBackOff：首次立即重启，之后 10s / 20s / 40s… 指数退避，封顶 300s，稳定运行 10 分钟后重置。systemd 有 `Restart=`、`RestartSec=`、`RestartSteps` / `RestartMaxDelaySec`（几何退避）、`RestartRandomizedDelaySec`（抖动），以及 `StartLimitIntervalSec` / `StartLimitBurst`（单位时间启动上限）。
- **来源**：pod-lifecycle.md；`systemd/systemd@main:man/systemd.service.xml`、`systemd/systemd@main:man/systemd.unit.xml`
- **关键词**："exponential backoff delay (10s, 20s, 40s, …) ... capped at 300 seconds"、"resets the restart backoff timer"；"not permitted to start any more"。
- **映射**：**采用（补空白）**。辅助进程重启必须有上限、退避、抖动与成功窗口重置；达到强度限制后进入可判定的失败态并上报，而不是无限拉起。同时要定义哪些退出算失败（避免把「用户主动退出」也重启）。

### A7 探针语义：starting / ready ≠ liveness [Confirmed]

- **做法**：K8s 三种探针分工 —— startup 成功前不跑其他探针；readiness 失败只把 Pod 从流量端点摘除、不重启；liveness 失败才重启。systemd 用 `READY=1` 表示启动完成（`Type=notify`），用 `WATCHDOG=1` 表示存活，两者独立。
- **来源**：probes.md；sd_notify.xml
- **关键词**："startup probe ... Kubernetes does not execute liveness or readiness probes"；readiness "removes the Pod's IP address from the EndpointSlices"；"READY=1 ... service startup is finished"。
- **映射**：**采用**。区分 `starting / ready`（能力能否使用）与存活（是否重启）；ready 失败不硬杀，只标不可用。

### A8 控制面（决定）与执行面（执行）分离 [Confirmed]

- **做法**：K8s API server 记录并跟踪宽限期与强制删除意图；kubelet 向容器运行时发起停止，运行时异步执行信号；强制删除时 API server 立即移除对象、不等节点确认。
- **来源**：pod-lifecycle.md
- **关键词**："cluster records and tracks the intended grace period ... the kubelet attempts graceful shutdown"；force delete "does not wait for confirmation"。
- **映射**：**采用**。Kernel 只发「请求终止」；由超时、重试、退避、强度限制组成的监督状态机必须归属一个明确的执行者（平台监督者），不能散落在 Kernel 与 T-021 两侧。

## 3. 与我们的倾向的差异（含代价）

1. **重启退避、抖动、成功窗口与启动强度上限**：倾向未覆盖。代价是多一个小的退避状态机与可配置上限；不做则可能出现崩溃 — 重启风暴。
2. **「已死」必须以可等待状态为准**：只依赖 completion port / 回调通知在官方文档中不保证送达。代价是需要等待线程或周期查询；这也是「平台监督者」存在的理由。
3. **作业对象 / 控制组的所有者与 `KILL_ON_JOB_CLOSE`**：Kernel 不碰句柄，但「整组回收 + 父退不留孤儿」必须有人持有作业句柄。建议平台适配器在创建时持句柄，Kernel 只请求与观察状态。
4. **nested job 风险（需实验）**：Windows 上游戏可能已被启动器放进作业；`CREATE_BREAKAWAY_FROM_JOB` 可能失败，需要实验与降级路径。
5. **信号语义要写清**：`requested` / `timed-out` / `killed` / `exited-normally` 要能区分，否则自检报告无法核对「是不是真的因为通道关闭」。

## 4. 未核实

- Windows 无窗口 / 无控制台进程没有统一的「优雅停止」API：只找到 `TerminateProcess`（立即、无清理机会）、console control handler、私有窗口消息三种应用级方式。 — Unverified
- K8s 探针默认阈值只确认 `periodSeconds` 默认 10 秒、grace 默认 30 秒、退避封顶 300 秒；`failureThreshold` 等未逐项核实。 — Unverified
- spdlog `overrun_oldest` 是否提供丢弃计数：README 未写，未继续读实现。 — Unverified

## 5. 依据（来源清单）

- `systemd/systemd@main:man/systemd.kill.xml`、`man/systemd.service.xml`、`man/systemd.unit.xml`、`man/sd_notify.xml`
- `kubernetes/website@main:content/en/docs/concepts/workloads/pods/pod-lifecycle.md`、`.../pods/probes.md`
- `MicrosoftDocs/win32@docs:desktop-src/ProcThread/job-objects.md`
- `MicrosoftDocs/sdk-api@docs:sdk-api-src/content/jobapi2/nf-jobapi2-terminatejobobject.md`
- `MicrosoftDocs/sdk-api@docs:sdk-api-src/content/winnt/ns-winnt-jobobject_basic_limit_information.md`
- `MicrosoftDocs/sdk-api@docs:sdk-api-src/content/winnt/ns-winnt-jobobject_associate_completion_port.md`
