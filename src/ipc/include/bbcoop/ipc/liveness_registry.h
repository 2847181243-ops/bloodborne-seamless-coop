// T-021：IAuxLiveness 的宿主侧实现（08-ipc.md §5、§7）。
// owner: AI-00（T-021）。
//
// 本类只做状态与计数，不做任何平台调用（进程句柄/作业对象在平台适配里）。
// 线程约定：watch/requestTerminate/onChannelClosed 由控制线程调用；
// heartbeat 计数是唯一的跨线程字段（辅助侧写、宿主侧读）。

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "bbcoop/ipc/aux_liveness.h"
#include "bbcoop/ipc/protocol.h"

namespace bbcoop::ipc {

class LivenessRegistry final : public IAuxLiveness {
public:
    // 通道数量上界：16 个足够覆盖当前模块清单，先定死避免分配。
    static constexpr std::size_t kMaxChannels = 16;

    LivenessRegistry() noexcept = default;

    Error watch(ChannelId channel, ChannelClosedFn fn, void* user) noexcept override;
    std::uint64_t heartbeatCounter(ChannelId channel) const noexcept override;
    void requestTerminate(ChannelId channel) noexcept override;

    // —— 以下由 T-021 的传输/测试驱动，不属于 IAuxLiveness ——

    // 记录一次心跳（辅助侧写入的等价物；测试用假实现走这里）。
    void noteHeartbeat(ChannelId channel) noexcept;

    // 心跳停滞判定（由 ChannelWatchdog 驱动）：把通道标为 stale。
    // 只从 starting / ready 迁移；幂等（重复标记不改变状态、不计日志）。
    void markStale(ChannelId channel) noexcept;

    // 通道关闭事件（操作系统信号 / 测试注入）。回调**只触发一次**；
    // 与 requestTerminate 的先后顺序不影响计数（幂等，08-ipc.md D-7）。
    void onChannelClosed(ChannelId channel) noexcept;

    ChannelState state(ChannelId channel) const noexcept;
    std::uint32_t terminateRequests(ChannelId channel) const noexcept;
    std::uint32_t channelClosures(ChannelId channel) const noexcept;

private:
    struct Entry {
        ChannelId channel{};
        bool used = false;
        ChannelState state = ChannelState::Starting;
        ChannelClosedFn fn = nullptr;
        void* user = nullptr;
        std::atomic<std::uint64_t> heartbeat{0};
        std::atomic<std::uint32_t> terminateRequests{0};
        std::atomic<std::uint32_t> closures{0};
    };

    Entry* find(ChannelId channel) noexcept;
    const Entry* find(ChannelId channel) const noexcept;

    Entry m_entries[kMaxChannels];
};

}  // namespace bbcoop::ipc
