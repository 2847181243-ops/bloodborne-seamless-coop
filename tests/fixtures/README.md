# 测试夹具

可复用的测试数据与桩。套件归属与运行方式见任务书 §41.3 的模块 Gate。

## Kernel 的 3 个夹具（T-006）

任务书 §41.1 T-006 要求「3 个测试夹具」。它们都在 `fixture_modules.h` 里，
**进程外模块的假实现也在同一个头文件**（`07-kernel-seam.md` §1.6 要求它放这里，
好让故障注入 T-013 复用同一套，而不是各写各的）：

| # | 夹具 | 注入什么 | 用来验收 |
|---|---|---|---|
| 1 | `NormalModule` | 什么都不注入；`reportedHealth` 可改 | 正常加载、健康聚合、其余用例的对照组 |
| 2 | `InitFailModule` | `init` 返回指定错误码，且 `health()` **故意自报 Ok** | 「init 失败不阻断其他模块」；以及 Kernel 的覆盖规则（未初始化/已熔断必须压成 Unavailable，不能被模块自报骗过） |
| 3 | `TickFixtureModule` | `TickMode`（一直失败 / 自报超时 / 前几次慢再恢复）+ `stallTicks` / `stallMs` | 「tick 超时熔断」「连续失败熔断」「熔断后不再调用」「故障不传播」 |

另外两个只在加载路径用的小夹具：`WrongIdModule`（自报 ID 与表里不一致 → 该项必须被拒绝）、
`createNullModule`（工厂返回 nullptr → 该项按不可用处理，不影响其他模块）。

### `FakeLiveness`（`IAuxLiveness` 的假实现）

进程外模块的存活判定必须能**确定性地注入时序**，否则那条测试只能依赖真实的 IPC 与真实进程
（慢、不可复现、无法注入「心跳冻住」这种状态）。它提供：

| 方法 | 用途 |
|---|---|
| `watch(...)` | 与真实实现一致：同一通道重复注册返回 `InvalidArgument` |
| `advanceHeartbeat(channel)` | 推进心跳计数（对应辅助进程正常在跑） |
| `closeChannel(channel)` | 手工触发一次「通道关闭」回调（对应辅助进程被杀）；重复调用不再回调 |
| `terminateRequests(channel)` | 断言 Kernel **只发一次**终止请求 |

⚠️ 真进程的验证留给 T-013 的故障注入（kill 掉真的辅助进程），两者不重复：
这里验的是 Kernel 的判定逻辑，那里验的是端到端链路。
