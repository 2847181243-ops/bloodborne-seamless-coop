# 源码目录

Kernel + 模块宿主的结构。**一个模块 = 一个所有权单元**（任务书 §48-c）。

```text
src/
├── kernel/       加载、配置、日志、事件总线、健康巡检、熔断（T-005）
├── ipc/          进程外模块的通信、代理与桩、辅助进程生命周期（T-021）
├── interfaces/   冻结的 v1 接口 —— 模块间唯一的通信方式
├── modules/      15 个模块 + 2 个贯穿性辅助
└── platform/     平台适配（不在 15 个模块内）
```

## 依赖方向（单向）

```text
modules/*  ->  interfaces/  <-  kernel/  ->  ipc/  ->  interfaces/
```

- 模块**可以** include `interfaces/` 里的头文件。
- 模块**不可以** include `kernel/` 的头文件（内核通过 `interfaces/` 暴露能力）。
- 模块**不可以** include 另一个模块的任何头文件，也不可以互相链接
  （任务书 §3.6 硬性禁止）。
- `ipc/` 不是 15 个模块之一：它是 Kernel 与进程外模块之间的基础设施
  （`docs/architecture/06-process-boundary.md`）。`kernel -> ipc` 的方向是单向的，
  写成反的会得到 CMake 环（`docs/architecture/08-ipc.md` §7）。

CMake 会拒绝非法的模块间链接（T-009）。

## 模块清单

见任务书 **§3.6** —— 那是模块 ID 与层级的唯一来源。
本目录下的 17 个子目录与之逐字对应（15 个模块 + 2 个贯穿性辅助）。
