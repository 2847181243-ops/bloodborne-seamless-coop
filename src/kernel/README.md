# Kernel（模块宿主）

Kernel **只负责**：加载、配置、日志、事件总线、健康巡检、熔断。
**不含任何业务逻辑**（任务书 §3.6 原则 1）。

边界与「不做」清单见 [`docs/architecture/01-kernel.md`](../../docs/architecture/01-kernel.md)。
实现见 T-005。
