# Kernel（模块宿主）

Kernel **只负责**：加载、配置、日志、事件总线、健康巡检、熔断。
**不含任何业务逻辑**（任务书 §3.6 原则 1）。

边界与「不做」清单见 [`docs/architecture/01-kernel.md`](../../docs/architecture/01-kernel.md)；
规格（决策、状态机、暂定数值、验收）见 [`docs/architecture/09-kernel-host.md`](../../docs/architecture/09-kernel-host.md)。
实现见 T-005。

## 目录

```text
src/kernel/
├── include/bbcoop/kernel/   policy / module_table / kernel / host_services / event_bus / self_test
├── src/                     上面这些的实现
└── host/main.cpp            bloodcoop-host 的可执行入口（--self-test）
```

本目录属于 Kernel 内部接口：**模块不得 include**（模块只能通过 `src/interfaces/` 拿能力）。

## 隔离的边界：进程内拦不住崩溃（别在这里写成「已隔离」）

- **进程外的 6 个模块**由**进程隔离**兜住：辅助进程崩了，游戏进程活着，
  IPC 通道关闭 → Kernel 把该通道上的模块标为不可用（`onChannelClosed`）。
- **进程内的模块**只有「错误码 + 超时熔断」这一层：段错误与真正卡死不返回的 `tick`
  **会带走整个游戏进程**，Kernel 没有补救手段。「超时」是 `tick` 返回之后才测出来的。
- 任务书 §41.3 全局 Gate 第 9 项对进程外的 6 个成立，对进程内的模块**不成立**。
  **禁止**在任何报告或自检输出里写「Gate 第 9 项已满足」。
  依据：`docs/architecture/06-process-boundary.md` §6、`07-kernel-seam.md` §3。

为什么 `try/catch` 兜不住：冻结接口里的 `init` / `tick` 都是 `noexcept`，
在 `noexcept` 函数边界上抛出的异常会直接 `std::terminate`，调用方的 `catch` 看不到它。

## 依赖方向

```text
kernel -> ipc -> interfaces      （单向；反了会得到 CMake 环）
```

Kernel 通过 `bbcoop/ipc/aux_liveness.h` 的 `IAuxLiveness` 了解进程外模块的存活，
**不碰进程句柄、不做平台调用**（K-10）。
