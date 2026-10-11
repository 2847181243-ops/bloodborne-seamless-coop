// T-021：ChannelWatchdog 实现（08-ipc.md §5.1 / D-8；A-2）。
// owner: AI-00（T-021）。

#include "bbcoop/ipc/watchdog.h"

namespace bbcoop::ipc {

void ChannelWatchdog::onStarted(std::uint64_t nowMs) noexcept {
    m_startedAtMs = nowMs;
    m_lastChangeMs = nowMs;
    m_lastCounter = 0;
    m_ready = false;
    m_armed = false;
    m_reported = false;
}

void ChannelWatchdog::onReady(std::uint64_t nowMs) noexcept {
    m_ready = true;
    m_startedAtMs = nowMs;
    m_lastChangeMs = nowMs;
    m_reported = false;
}

WatchdogVerdict ChannelWatchdog::poll(std::uint64_t nowMs,
                                     std::uint64_t heartbeatCounter) noexcept {
    if (!m_ready) {
        // 就绪超时：判定为**启动失败**，与「跑起来之后卡住」是两种原因（§5.1）。
        if (m_reported || nowMs - m_startedAtMs < m_policy.readyTimeoutMs) {
            return WatchdogVerdict::None;
        }
        m_reported = true;
        return WatchdogVerdict::ReadyTimeout;
    }

    if (heartbeatCounter != m_lastCounter) {
        // 第一次心跳变化才武装：没有变化过的计数不能作为「卡住」的证据。
        m_lastCounter = heartbeatCounter;
        m_lastChangeMs = nowMs;
        m_armed = true;
        m_reported = false;
        return WatchdogVerdict::None;
    }

    if (!m_armed || m_reported) {
        return WatchdogVerdict::None;
    }
    if (nowMs - m_lastChangeMs < m_policy.heartbeatStaleMs) {
        return WatchdogVerdict::None;
    }
    m_reported = true;
    return WatchdogVerdict::HeartbeatStale;
}

}  // namespace bbcoop::ipc
