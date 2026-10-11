// T-021：跨边界状态的本地缓存（08-ipc.md D-2）。
// owner: AI-00（T-021）。
//
// 设计要点：游戏进程**不每帧跨边界查询**；辅助进程周期性推送更新，
// 这里的读操作都是本地原子读。没有缓存 = 不可用（fail-closed 的默认值）。

#pragma once

#include <atomic>
#include <cstdint>

namespace bbcoop::ipc {

struct TransportCache {
    std::atomic<bool> connected{false};
    std::atomic<std::uint32_t> rttMs{0};

    void set(bool isConnected, std::uint32_t rtt) noexcept {
        connected.store(isConnected, std::memory_order_release);
        rttMs.store(rtt, std::memory_order_release);
    }
};

struct SessionCache {
    std::atomic<bool> active{false};
    std::atomic<std::uint32_t> memberCount{0};
    std::atomic<bool> isHost{false};

    void set(bool isActive, std::uint32_t members, bool host) noexcept {
        active.store(isActive, std::memory_order_release);
        memberCount.store(members, std::memory_order_release);
        isHost.store(host, std::memory_order_release);
    }
};

}  // namespace bbcoop::ipc
