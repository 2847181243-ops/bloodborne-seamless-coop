// Kernel 的运行策略常量（T-005 产出）。
//
// 本文件属于 Kernel 内部接口：模块**不得** include（src/README.md 的依赖方向是
// modules/* -> interfaces/ <- kernel/，任务书 §3.6 硬性禁止跨模块 include）。
// 使用方只有 kernel/ 自身、bloodcoop-host 与 tests/。
//
// ── 数值来源：全部「待定」，不是任务书规定 ──────────────────────────────────
//
// 任务书 §41.1 T-005 只写了「tick 超时熔断」与「健康巡检」两句话，
// **没有给阈值、周期、失败次数或恢复策略**；docs/architecture/01-kernel.md 把它们
// 记为待定项 K-1 / K-2 / K-3，docs/architecture/03-fault-isolation.md 记为 F-1 / F-3。
// 所以下面的 kDefault* 全是**暂用值**：没有任务书依据，也没有实测依据。
// 它们只是让 v1 能跑起来并让超时/熔断可以被观察到，不代表任何已验证的结论。
// 裁决后只需要改本文件：Kernel 的默认策略由这些常量构成。

#pragma once

#include <cstddef>
#include <cstdint>

namespace bbcoop::kernel {

// 单次 tick 允许的时长上限（毫秒）。
// 待定（K-1）：任务书没有给阈值。暂用 50 —— 它明显大于单帧预算，
// 又远小于任务书 §36 的 500ms 网络延迟上限，便于在本地观察到超时。
inline constexpr std::uint64_t kDefaultTickTimeoutMs = 50;

// 连续失败多少次之后熔断。超时也算失败。
// 待定（K-1 / K-3）：任务书既没有给次数，也没有区分「连续」与「累计」。
// 暂用 3 —— 一次抖动不该停掉模块，三次说明它已经不稳定。
inline constexpr std::uint32_t kDefaultFailureThreshold = 3;

// 健康巡检周期（毫秒）。
// 待定（K-2）：任务书只写「周期性取得各模块的健康状态」，没有给周期。暂用 1000。
inline constexpr std::uint64_t kDefaultHealthSweepPeriodMs = 1000;

// ── 「熔断后自动恢复」为什么**没有**这个开关 ────────────────────────────────
//
// 待定项 K-3（恢复策略）在 T-005 里定为：**不自动恢复**。理由有两条：
//   1. 任务书 §41.1 T-005 的验收原文是「标为不可用，**停止调用它**」，
//      自动恢复与这句话直接冲突；
//   2. 草稿里曾有一个 `autoRecovery` 字段，但**没有任何代码读它** ——
//      一个声明了却不生效的开关比没有这个开关更糟：它会让读代码的人以为
//      「设成 true 就会自动恢复」。
// 唯一的恢复手段是显式 `Kernel::resetCircuit(id)`（且对通道关闭 / 心跳停滞**拒绝**，
// 因为那两种情况下对端进程已经没了）。进程外模块的重启策略归 T-021 / T-013。

// 进程外模块的「卡住」阈值 N **不在这里**：它是 IPC 协议的一部分
// （`bbcoop/ipc/protocol.h` 的 kHeartbeatStaleMs，暂定 3000ms），
// 由 ipc 的 ChannelWatchdog 判定。Kernel 只消费它的结论。
// 为什么不在 Kernel 里再写一份：两个数值各写一份，改一个就会漏另一个。

// ── 容量上限：实现约束，不是任务书数值 ─────────────────────────────────────
//
// 固定容量而不是动态容器，有两个原因：
//   1. 冻结接口里的 publish / find / log 都是 noexcept，动态扩容失败会抛
//      std::bad_alloc，而异常不能穿过这条边界（任务书 §3.6 原则 5）。
//   2. 固定容量让「满了怎么办」成为一条可以测的行为（返回 Refused 并记日志），
//      而不是一个偶发的崩溃点。
// 32 对 17 个模块留了余量；服务与订阅的 64 同理。
inline constexpr std::size_t kMaxModules = 32;
inline constexpr std::size_t kMaxServiceEntries = 64;
inline constexpr std::size_t kMaxEventSubscriptions = 64;

// Kernel 的运行策略。默认值即上面那些暂用值。
// 测试与故障注入传入别的值（例如把超时压到 1ms），因此它不是编译期常量。
struct KernelPolicy {
    std::uint64_t tickTimeoutMs = kDefaultTickTimeoutMs;
    std::uint32_t failureThreshold = kDefaultFailureThreshold;
    std::uint64_t healthSweepPeriodMs = kDefaultHealthSweepPeriodMs;
};

}  // namespace bbcoop::kernel
