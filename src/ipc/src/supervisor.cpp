// T-021：RestartSupervisor 实现（08-ipc.md §5.4）。
// owner: AI-00（T-021）。

#include "bbcoop/ipc/supervisor.h"

namespace bbcoop::ipc {
namespace {

std::uint64_t minU64(std::uint64_t a, std::uint64_t b) noexcept {
    return (a < b) ? a : b;
}

}  // namespace

void RestartSupervisor::recordStart(std::uint64_t nowMs) noexcept {
    m_startTimes[m_startCursor] = nowMs;
    m_startCursor = (m_startCursor + 1) % kMaxStartRecords;
    if (m_startCount < kMaxStartRecords) {
        ++m_startCount;
    }
}

std::uint32_t RestartSupervisor::startsInWindow(std::uint64_t nowMs) const noexcept {
    std::uint32_t count = 0;
    for (std::size_t i = 0; i < m_startCount; ++i) {
        const std::uint64_t t = m_startTimes[i];
        if (nowMs >= t && (nowMs - t) < m_policy.windowMs) {
            ++count;
        }
    }
    return count;
}

void RestartSupervisor::onStarted(std::uint64_t nowMs) noexcept {
    recordStart(nowMs);
}

void RestartSupervisor::onReady(std::uint64_t nowMs) noexcept {
    m_readyAtMs = nowMs;
}

void RestartSupervisor::poll(std::uint64_t nowMs) noexcept {
    if (m_readyAtMs != 0 && nowMs >= m_readyAtMs + m_policy.stableWindowMs) {
        resetBackoff();
        m_readyAtMs = 0;
    }
}

void RestartSupervisor::resetBackoff() noexcept {
    m_consecutiveFailures = 0;
    m_currentDelayMs = 0;
    m_nextRestartAtMs = 0;
    m_exhausted = false;
}

std::uint64_t RestartSupervisor::applyJitter(std::uint64_t delayMs) noexcept {
    if (m_policy.jitterPermille == 0 || delayMs == 0) {
        return delayMs;
    }
    // 简单 LCG：确定性、无需分配；只用于打散重启时刻（REF-001 A6）。
    m_jitterState = (m_jitterState * 1664525U) + 1013904223U;
    const std::uint64_t spread = (delayMs * m_policy.jitterPermille) / 1000U;
    if (spread == 0) {
        return delayMs;
    }
    const std::uint64_t jitter = m_jitterState % (spread + 1U);
    return delayMs + jitter;
}

bool RestartSupervisor::onChannelClosed(std::uint64_t nowMs) noexcept {
    ++m_consecutiveFailures;
    if (startsInWindow(nowMs) >= m_policy.maxStartsPerWindow) {
        m_exhausted = true;
        m_nextRestartAtMs = 0;
        return false;
    }
    if (m_currentDelayMs == 0) {
        m_currentDelayMs = m_policy.initialDelayMs;
    } else {
        m_currentDelayMs = minU64(m_currentDelayMs * 2U, m_policy.maxDelayMs);
    }
    const std::uint64_t delay = applyJitter(m_currentDelayMs);
    m_nextRestartAtMs = nowMs + delay;
    m_readyAtMs = 0;
    return true;
}

}  // namespace bbcoop::ipc
