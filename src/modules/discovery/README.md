# `mod.discovery`

房间码、Peer 发现

| 项 | 内容 |
|---|---|
| 模块 ID | `mod.discovery`（与任务书 §3.6 注册表逐字一致） |
| 路径 | `src/modules/discovery/` |
| 层级 | Layer 0 |
| 职责 | 房间码、Peer 发现 |
| 归属 Agent | AI-B2 |
| 状态 | **骨架**（T-007）：一切返回 `Unavailable`，不发布服务，不做任何事 |


## 骨架阶段的行为（可执行契约）

`tests/contract_test.cpp` 逐条断言下列约定，`ctest` 里能跑到（`discovery_contract`）：

- `id()` 返回 §3.6 的 ID，且指针在整个生命周期内稳定
  （Kernel 只存指针、不复制字符串，见 `src/kernel/include/bbcoop/kernel/kernel.h`）；
- `init` 返回 `Error::Unavailable`、**不崩溃**（连 `ctx.host == nullptr` 也不崩），
  且**不发布任何服务** —— 先发布空壳会让真正的实现再也发布不上去；
- `tick` 返回 `Unavailable` 且**不记日志**（tick 每帧调用，记日志会把日志刷爆，任务书 §34）；
- `shutdown` 幂等：重复调用、`init` 从未成功时调用都安全。

## 职责边界

**做什么**与**不做什么**都要写清楚，后半部分同样重要。
完整任务书归 **T-010**（`docs/modules/discovery.md`）；本文件只承载 §3.6 给的那一行职责。

## 对外接口

依赖 `src/interfaces/` 里的哪个冻结接口、暴露哪个能力：**尚未确定**（T-010）。
骨架阶段**不发布任何服务**，所以其他模块 `find()` 拿到的必然是 `nullptr` ——
调用方必须处理这种情况（§3.6 原则 5）。

## 失败行为

本模块失败时：进程是否存活？哪些功能降级？用户看到什么？

骨架阶段：`init` 一律返回 `Unavailable`，Kernel 只把本模块标记为不可用，
**不影响其他模块**（这条已由 Kernel 侧的单元测试验收）。真实降级行为待 T-010；
总表见 [`docs/architecture/04-degradation-matrix.md`](../../../docs/architecture/04-degradation-matrix.md)。
