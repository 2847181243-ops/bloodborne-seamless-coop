// T-021：辅助进程重启监督策略（08-ipc.md §5.4 / REF-001 A6）。
// owner: AI-00（T-021）。
//
// 纯逻辑 + 可注入时钟：不碰进程句柄（那在平台适配层），便于单元测试与故障注入。
// 所有数值都是**暂定**（任务书未给），集中在 RestartPolicy 里。

#pragma once

#include <cstddef>
#include <cstdint>

namespace bbcoop::ipc {

struct RestartPolicy {
    std::uint64_t initialDelayMs = 10000;    // 暂定
    std::uint64_t maxDelayMs = 300000;       // 暂定（封顶）
    std::uint64_t stableWindowMs = 600000;   // 暂定：稳定运行这么久后重置退避
    std::uint64_t windowMs = 600000;         // 暂定：启动强度统计窗口
    std::uint32_t maxStartsPerWindow = 5;    // 暂定：窗口内启动次数上限
    std::uint32_t jitterPermille = 200;      // 暂定：抖动上限（千分比）
};

class RestartSupervisor {
public:
    static constexpr std::size_t kMaxStartRecords = 32;

    RestartSupervisor() noexcept = default;
    explicit RestartSupervisor(RestartPolicy policy) noexcept : m_policy(policy) {}

    // 记录一次「进程已启动」；用于启动强度统计。
    void onStarted(std::uint64_t nowMs) noexcept;

    // 进入 ready：记录开始时间，稳定窗口从这里算。
    void onReady(std::uint64_t nowMs) noexcept;

    // 每次 tick 调用：稳定窗口到点则重置退避与失败计数。
    void poll(std::uint64_t nowMs) noexcept;

    // 通道关闭 / 进程退出：安排下一次重启。返回 false 表示不再重启（已耗尽）。
    bool onChannelClosed(std::uint64_t nowMs) noexcept;

    std::uint64_t nextRestartAtMs() const noexcept { return m_nextRestartAtMs; }
    std::uint64_t currentDelayMs() const noexcept { return m_currentDelayMs; }
    std::uint32_t consecutiveFailures() const noexcept { return m_consecutiveFailures; }
    bool exhausted() const noexcept { return m_exhausted; }

private:
    std::uint64_t applyJitter(std::uint64_t delayMs) noexcept;
    void recordStart(std::uint64_t nowMs) noexcept;
    std::uint32_t startsInWindow(std::uint64_t nowMs) const noexcept;
    void resetBackoff() noexcept;

    RestartPolicy m_policy{};
    std::uint64_t m_nextRestartAtMs = 0;
    std::uint64_t m_currentDelayMs = 0;
    std::uint64_t m_readyAtMs = 0;
    std::uint64_t m_startTimes[kMaxStartRecords]{};
    std::size_t m_startCount = 0;
    std::size_t m_startCursor = 0;
    std::uint32_t m_consecutiveFailures = 0;
    std::uint32_t m_jitterState = 0x9E3779B9U;
    bool m_exhausted = false;
};

}  // namespace bbcoop::ipc
