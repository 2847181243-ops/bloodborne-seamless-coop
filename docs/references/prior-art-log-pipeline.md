# REF-002 日志管线（先例核对）

> **用途**：为 **K-7**（Kernel 日志职责 vs `mod.observability`）与 **T-005 / T-021** 的日志路径提供外部先例对照。
> **它不是要求**：与任务书、`MAINTAINERS.md`、`AI_POLICY.md` 冲突时以后者为准；先例只用来回答「还有没有更优做法」。
> **最后更新**：2026-10
> **证据等级**：Confirmed = 实际读到一手文件；Likely = 多份一手材料交叉推断；Unverified = 未找到一手来源。
> **引用格式**：`<owner>/<repo>@<默认分支>:<path>`。

## 1. 结论摘要

| # | 先例的通行做法 | 与我们的关系 |
|---|---|---|
| 1 | 低信任 / 沙箱进程不直接写文件：日志经宿主通道（journald socket、容器 stdout、父进程复制句柄）交给有权限的宿主 | 与 06 §5.2 一致；我们的「IPC 回传 + 零文件权限」更严格 |
| 2 | 多生产者 → 单写者：有界队列 + 专用写线程 / 单 backend worker | 与 K-7 的「Kernel 只做机制」一致 |
| 3 | 队列满：阻塞或丢弃；丢弃要计数（quill / journald） | 采用 + 补强：诊断丢弃 + 计数 |
| 4 | 审计日志与诊断日志分离：独立策略、独立后端；诊断可采样 / 丢弃 | 部分采用：记录头要带「可丢 / 不可丢」类别 |
| 5 | 结构化记录 + 处理器侧脱敏 + 可信字段由宿主采集 | 采用；官方没有「禁止字段清单」这样的标准 |
| 6 | 日志注入防护：值可含二进制、多行编码进单字段、宿主是唯一格式化者 | 采用 |
| 7 | 故障注入的可判定性：进程死亡用可等待状态、卡住用心跳（与 REF-001 A4 / A5 同源） | 采用 |

## 2. 逐条先例

### B1 沙箱 / 低信任进程如何写日志 [Confirmed]

- **做法**：三条一手路径 —— (a) systemd 把服务 stdout / stderr 接到 journald（`StandardOutput=journal`），由有权限的 journald 统一落盘；(b) K8s 容器 runtime 重定向 stdout / stderr，kubelet 负责日志轮转；(c) Chromium 沙箱子进程不能打开日志文件，日志句柄由父进程复制后交给它。
- **来源**：`systemd/systemd@main:man/systemd.exec.xml`、`man/systemd-journald.service.xml`；`kubernetes/website@main:content/en/docs/concepts/cluster-administration/logging.md`；`chromium/chromium@main:chrome/common/logging_chrome.cc`
- **关键词**："Sandboxed processes cannot open log files"；"Child processes ... can log to a handle duplicated from the parent"；"kubelet is responsible for rotating container logs"。
- **映射**：**采用（更严格）**。继承句柄更快，但给了子进程写能力；systemd 的 socket / 守护进程模式更接近我们「辅助进程零文件权限 + IPC 回传宿主」。代价是记录要过 IPC，必须长连接 + 批量 + 有界队列，不能每条同步往返。

### B2 异步：有界队列 + 专用写线程 [Confirmed]

- **做法**：spdlog 异步 logger 用固定线程池（示例 8192 槽、1 个后台线程）；quill 每个前端线程一个无锁 SPSC 队列，由单个 backend worker 统一格式化并写 sink；OTel `BatchLogRecordProcessor` 有 `maxQueueSize`（默认 2048）、定时导出与批量大小；journald 本身是中心化守护进程。
- **来源**：`gabime/spdlog@v1.x:README.md`；`odygrd/quill@master:README.md`；`open-telemetry/opentelemetry-specification@main:specification/logs/sdk.md`；systemd-journald.service.xml
- **关键词**："queue with 8k items and 1 backing thread"；"A single backend worker drains all queues"；"maxQueueSize ... After the size is reached logs are dropped"。
- **映射**：**采用**。Kernel 内「接收 → 有界队列 → 单写线程 → 落盘」与先例一致；格式化与写盘只允许发生在单写者线程。

### B3 队列满：阻塞 vs 丢弃 + 丢弃计数 [Confirmed]

- **做法**：spdlog 提供 `block`（生产者等待）与 `overrun_oldest`（丢最旧）两种溢出策略；quill 提供 bounded / unbounded + blocking / dropping，并监控丢弃条数；OTel 队列满即丢（规范未要求计数）；journald 按 `RateLimitBurst` / `RateLimitIntervalSec` 限速丢消息，并生成一条「丢了多少条」的计数消息。
- **来源**：spdlog README 与 `gabime/spdlog@v1.x:include/spdlog/async.h`；quill README；OTel sdk.md；`systemd/systemd@main:man/journald.conf.xml`
- **关键词**："Queue Overflow Policy: block / overrun"；"monitoring on dropped messages"；"A message about the number of dropped messages is generated"。
- **映射**：**采用 + 补强**。硬约束「不阻塞主线程」排除了纯 block；诊断日志用「有界丢弃 + 丢弃计数」，且计数必须能被故障注入 / 压测读到。

### B4 审计 / 安全日志 vs 诊断日志分级 [Likely]

- **做法**：K8s 审计是独立管线：独立策略（记录什么、级别、阶段）+ 独立后端（log 文件 / webhook），与组件诊断日志分离；OTel 对所有 LogRecord 统一施加 minimum severity 与 trace-based 丢弃，没有审计豁免；journald 把内核审计记录作为来源之一中心化收集。
- **来源**：`kubernetes/website@main:content/en/docs/tasks/debug/debug-cluster/audit.md`；OTel sdk.md；systemd-journald.service.xml
- **关键词**："The policy determines what's recorded and the backends persist the records"；"Log backend ... Webhook backend"；"the log record MUST be dropped"。
- **映射**：**部分采用（倾向需补）**。「ILogger 管机制、observability 管内容」不排斥审计分级，但「可丢 / 不可丢」必须成为机制层可见的记录类别；否则队列满时审计会被静默丢弃。建议：诊断 = 可丢 + 计数；审计 = 不静默丢（短时背压，或显式进入降级 / 失败态并上报）。

### B5 结构化记录 + 脱敏 [Confirmed]

- **做法**：journald 原生协议支持结构化字段（字段名可不唯一、值可含二进制）；OTel 定义 Timestamp / SeverityNumber / Body / Attributes / Resource 等字段；OTel 指南给出处理器在记录进入后端前把 `token` 属性替换为 `REDACTED` 的示例；systemd 提供 `LogFilterPatterns=` 按正则允许 / 拒绝 MESSAGE。
- **来源**：`systemd/systemd@main:man/systemd.journal-fields.xml`、systemd.exec.xml；`open-telemetry/opentelemetry-specification@main:specification/logs/data-model.md`、`.../logs/supplementary-guidelines.md`
- **关键词**："field values that may include binary data"；"redacts values from attributes containing token"；"denied patterns ... discarded immediately"。
- **映射**：**采用**。IPC 上就传结构化键值（长度前缀），宿主再序列化为行；脱敏属于内容策略层（observability），同时辅助进程默认不采集敏感字段（纵深防御）。

### B6 日志注入 / 伪造防护 [Confirmed]

- **做法**：journald 字段值可含换行与二进制；多行消息被编码进单个 `MESSAGE=` 字段，消除「行 = 记录」歧义；下划线前缀字段是可信字段，由 journald 采集，客户端不可伪造。OTel 用结构化 Attributes 承载上下文与 trace 关联。
- **来源**：systemd.journal-fields.xml；OTel data-model.md
- **关键词**："encode them as a single MESSAGE= field"；"Fields prefixed with an underscore are trusted fields"。
- **映射**：**采用**。宿主是唯一格式化 / 落盘者；时间、PID、进程角色等可信元数据由宿主采集，不采信辅助进程自报；输出时限制值长度并转义换行 / 控制字符。

### B7 单写者 vs 多写者 [Confirmed]

- **做法**：journald、spdlog 后台单线程、quill 单 backend worker、OTel processor 串行调用 exporter，本质都是「多生产者 → 单写者」。
- **来源**：systemd-journald.service.xml；spdlog README；quill README；OTel sdk.md
- **关键词**："A single backend worker drains all queues"；"MUST synchronize calls to ... Export"。
- **映射**：**采用**。Kernel 的单写线程是唯一落盘者；辅助进程与 T-021 都只是生产者。

### B8 辅助进程被 kill 的故障注入可判定性 [Confirmed]

- **做法**：Windows 用作业状态 signaled / completion port 事件判定进程死亡；systemd 用 watchdog 判定「卡住」、用单元 / cgroup 状态判定结束；K8s kubelet 观察到容器终止后按 restartPolicy 处理并重置 backoff。
- **来源**：job-objects.md、associate_completion_port.md；sd_notify.xml；pod-lifecycle.md（完整路径见 REF-001 §5 与本文 §5）
- **关键词**："state of a job object is set to signaled when all of its processes are terminated"；"restarts them with an exponential backoff"。
- **映射**：**采用**。故障注入直接 kill 辅助进程 → 宿主用 OS 等待 / 通知判死 → Kernel 记录非正常退出 → 有界退避重启或进入失败态；心跳超时则判「卡住」并走 REF-001 A1 流程。

## 3. 与我们的倾向的差异（含代价）

1. **审计分级要落到机制层（durability class）**：IPC 记录头带「可丢 / 不可丢」；诊断可丢 + 计数；审计不静默丢（短背压或显式失败态）。测试必须覆盖「队列满时审计不丢」。
2. **丢弃计数**：需要计数器与周期性 suppressed 记录，并暴露给 observability；好处是压测 / 故障注入能给出「丢了多少、丢哪类」的确定结论。
3. **满队列 block 与「不阻塞主线程」冲突**：诊断只能丢并计数；审计要么短时背压，要么把「审计不可用」作为显式失败上报。
4. **Chromium 继承句柄模型 vs 零权限 IPC**：我们守住最小权限，代价是逐条 IPC 开销与崩溃窗口内的日志丢失（接受有界丢失，不退回逐条同步 RPC）。

## 4. 未核实

- Chromium 是否「逐条日志走 IPC 回传」：只读到沙箱子进程不能开文件、使用父进程复制的句柄；未找到逐条 IPC 的一手文档。 — Unverified
- systemd / journald 是否对审计记录豁免限速 / 丢弃：未找到明文条款。 — Unverified
- OTel 是否要求为「队列满丢弃」提供计数器：规范只写 "After the size is reached logs are dropped"，未找到计数要求。 — Unverified
- 「可追踪标识」（IP / 账号 ID / 硬件指纹）的官方禁止字段清单：只有脱敏示例与过滤规则，没有规范清单。 — Unverified

## 5. 依据（来源清单）

- `systemd/systemd@main:man/systemd.exec.xml`、`man/systemd-journald.service.xml`、`man/journald.conf.xml`、`man/systemd.journal-fields.xml`
- `kubernetes/website@main:content/en/docs/concepts/cluster-administration/logging.md`、`.../tasks/debug/debug-cluster/audit.md`
- `chromium/chromium@main:chrome/common/logging_chrome.cc`
- `gabime/spdlog@v1.x:README.md`、`include/spdlog/async.h`
- `odygrd/quill@master:README.md`
- `open-telemetry/opentelemetry-specification@main:specification/logs/sdk.md`、`logs/data-model.md`、`logs/supplementary-guidelines.md`
