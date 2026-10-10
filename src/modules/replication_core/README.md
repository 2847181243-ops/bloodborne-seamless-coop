# `mod.replication_core`

实体复制基础

| 项 | 内容 |
|---|---|
| 模块 ID | `mod.replication_core` |
| 路径 | `src/modules/replication_core/` |
| 层级 | 见任务书 §3.6 |

## 职责边界

**做什么**与**不做什么**都要写清楚 —— 后半部分同样重要。
实现时按任务书 §48-c 的所有权表填写，见 T-007。

## 对外接口

依赖 `src/interfaces/` 里的哪个冻结接口、暴露哪个能力。
实现时填写。

## 失败行为

本模块失败时：进程是否存活？哪些功能降级？用户看到什么？
**这条必须回答**（任务书 §3.6 原则 5 要求故障不传播）。
降级行为见 [`docs/architecture/04-degradation-matrix.md`](../../../docs/architecture/04-degradation-matrix.md)。
