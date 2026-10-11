// T-021：LivenessRegistry 实现（08-ipc.md §5、§7）。
// owner: AI-00（T-021）。

#include "bbcoop/ipc/liveness_registry.h"

namespace bbcoop::ipc {

LivenessRegistry::Entry* LivenessRegistry::find(ChannelId channel) noexcept {
    for (std::size_t i = 0; i < kMaxChannels; ++i) {
        if (m_entries[i].used && m_entries[i].channel == channel) {
            return &m_entries[i];
        }
    }
    return nullptr;
}

const LivenessRegistry::Entry* LivenessRegistry::find(ChannelId channel) const noexcept {
    for (std::size_t i = 0; i < kMaxChannels; ++i) {
        if (m_entries[i].used && m_entries[i].channel == channel) {
            return &m_entries[i];
        }
    }
    return nullptr;
}

Error LivenessRegistry::watch(ChannelId channel, ChannelClosedFn fn, void* user) noexcept {
    if (find(channel) != nullptr) {
        return Error::InvalidArgument;  // 重复注册
    }
    for (std::size_t i = 0; i < kMaxChannels; ++i) {
        Entry& e = m_entries[i];
        if (!e.used) {
            e.channel = channel;
            e.used = true;
            e.state = ChannelState::Starting;
            e.fn = fn;
            e.user = user;
            e.heartbeat.store(0, std::memory_order_relaxed);
            e.terminateRequests.store(0, std::memory_order_relaxed);
            e.closures.store(0, std::memory_order_relaxed);
            return Error::Ok;
        }
    }
    return Error::Unavailable;  // 表满
}

std::uint64_t LivenessRegistry::heartbeatCounter(ChannelId channel) const noexcept {
    const Entry* e = find(channel);
    return (e == nullptr) ? 0 : e->heartbeat.load(std::memory_order_acquire);
}

void LivenessRegistry::requestTerminate(ChannelId channel) noexcept {
    Entry* e = find(channel);
    if (e == nullptr) {
        return;
    }
    // 幂等：已在终止流程或已消失时不再计数（08-ipc.md D-7）。
    if (e->state == ChannelState::Terminating || e->state == ChannelState::Gone) {
        return;
    }
    e->state = ChannelState::Terminating;
    e->terminateRequests.fetch_add(1, std::memory_order_relaxed);
}

void LivenessRegistry::noteHeartbeat(ChannelId channel) noexcept {
    Entry* e = find(channel);
    if (e == nullptr) {
        return;
    }
    e->heartbeat.fetch_add(1, std::memory_order_release);
    if (e->state == ChannelState::Starting) {
        e->state = ChannelState::Ready;
    }
}

void LivenessRegistry::markStale(ChannelId channel) noexcept {
    Entry* e = find(channel);
    if (e == nullptr) {
        return;
    }
    // 只有已经跑起来（ready）的通道才谈得上「卡住」：starting 阶段的超时是
    // 就绪超时（启动失败），走另一条路。已被判终止 / 已消失的通道也不回退。
    // 状态机因此只往前走：starting -> ready -> stale -> terminating -> gone。
    if (e->state == ChannelState::Ready) {
        e->state = ChannelState::Stale;
    }
}

void LivenessRegistry::onChannelClosed(ChannelId channel) noexcept {
    Entry* e = find(channel);
    if (e == nullptr) {
        return;
    }
    // 先改状态再回调：即使回调里再次 onChannelClosed 也不会重复触发。
    if (e->state == ChannelState::Gone) {
        return;
    }
    e->state = ChannelState::Gone;
    e->closures.fetch_add(1, std::memory_order_relaxed);
    if (e->fn != nullptr) {
        e->fn(e->user, channel);
    }
}

ChannelState LivenessRegistry::state(ChannelId channel) const noexcept {
    const Entry* e = find(channel);
    return (e == nullptr) ? ChannelState::Gone : e->state;
}

std::uint32_t LivenessRegistry::terminateRequests(ChannelId channel) const noexcept {
    const Entry* e = find(channel);
    return (e == nullptr) ? 0 : e->terminateRequests.load(std::memory_order_relaxed);
}

std::uint32_t LivenessRegistry::channelClosures(ChannelId channel) const noexcept {
    const Entry* e = find(channel);
    return (e == nullptr) ? 0 : e->closures.load(std::memory_order_relaxed);
}

}  // namespace bbcoop::ipc
