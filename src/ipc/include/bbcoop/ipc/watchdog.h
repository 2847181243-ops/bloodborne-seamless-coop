// T-021：通道的时间判据（08-ipc.md §5.1 / D-8；A-2）。
// owner: AI-00（T-021）。
//
// 只做两件有时间的判断，纯逻辑 + 可注入时钟（不碰进程句柄、不读真实时间）：
//   * 就绪超时：`starting` 起算，超时判**启动失败**（不是「卡住」）；
//   * 心跳停滞：**观察到第一次心跳变化之后才武装**（D-7），N 内计数没变判「卡住」。
//
// 为什么单独一个类：LivenessRegistry 管状态与计数，本类管时间；两者都不做平台调用，
// 因此都能在单元测试里用假时钟跑到边界值。

#pragma once

#include <cstdint>

#include "bbcoop/ipc/protocol.h"

namespace bbcoop::ipc {

struct WatchdogPolicy {
    // 就绪超时：任务书未给数值 → **暂定** 5000ms（08-ipc.md §9）。
    std::uint64_t readyTimeoutMs = 5000;
    // 心跳停滞阈值 N：与 protocol.h 的 kHeartbeatStaleMs 同源，同标「暂定」。
    std::uint64_t heartbeatStaleMs = kHeartbeatStaleMs;
};

// 判定结果。None 表示本 tick 没有新结论。
enum class WatchdogVerdict : std::uint8_t {
    None = 0,
    ReadyTimeout = 1,    // 启动失败：fail-closed，报告 reason
    HeartbeatStale = 2,  // 卡住：触发终止请求
};

constexpr const char* to_string(WatchdogVerdict v) noexcept {
    switch (v) {
        case WatchdogVerdict::None: return "none";
        case WatchdogVerdict::ReadyTimeout: return "ready_timeout";
        case WatchdogVerdict::HeartbeatStale: return "heartbeat_stale";
    }
    return "unknown";
}

class ChannelWatchdog {
public:
    ChannelWatchdog() noexcept = default;
    explicit ChannelWatchdog(WatchdogPolicy policy) noexcept : m_policy(policy) {}

    // 通道建立：进入 starting，就绪超时从这里算。
    void onStarted(std::uint64_t nowMs) noexcept;

    // 收到 Ready：状态机进入 ready。**不**因此武装卡住判定（D-7 要求先看到心跳变化）。
    void onReady(std::uint64_t nowMs) noexcept;

    // 每 tick 调用：nowMs 是单调时钟，heartbeatCounter 是从共享内存读到的计数。
    // 同一个结论**只返回一次**（幂等，A-2）。
    WatchdogVerdict poll(std::uint64_t nowMs, std::uint64_t heartbeatCounter) noexcept;

    bool ready() const noexcept { return m_ready; }
    bool armed() const noexcept { return m_armed; }
    bool verdictReported() const noexcept { return m_reported; }

private:
    WatchdogPolicy m_policy{};
    std::uint64_t m_startedAtMs = 0;
    std::uint64_t m_lastChangeMs = 0;
    std::uint64_t m_lastCounter = 0;
    bool m_ready = false;
    bool m_armed = false;
    bool m_reported = false;
};

}  // namespace bbcoop::ipc
