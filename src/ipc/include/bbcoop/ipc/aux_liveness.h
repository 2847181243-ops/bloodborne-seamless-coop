// T-021 与 Kernel 的接缝（08-ipc.md §7；形状来自 docs/architecture/07-kernel-seam.md §1.2）。
// owner: AI-00（T-021）。
//
// 为什么用函数指针 + void* 而不是 std::function：
// 回调注册发生在 noexcept 边界上，std::function 会分配内存，分配失败在 noexcept
// 函数里逃出去就是 std::terminate —— 那正是任务书 §3.6 原则 5 要避免的。

#pragma once

#include <cstdint>

#include "bbcoop/ipc/protocol.h"
#include "errors.h"

namespace bbcoop::ipc {

class IAuxLiveness {
public:
    using ChannelClosedFn = void (*)(void* user, ChannelId channel) noexcept;

    IAuxLiveness() = default;
    virtual ~IAuxLiveness() = default;
    IAuxLiveness(const IAuxLiveness&) = delete;
    IAuxLiveness& operator=(const IAuxLiveness&) = delete;

    // 注册通道关闭回调。重复注册同一通道返回 InvalidArgument。
    virtual Error watch(ChannelId channel, ChannelClosedFn fn, void* user) noexcept = 0;

    // 本地读心跳计数（每 tick 读共享内存，零 IPC 成本）。
    virtual std::uint64_t heartbeatCounter(ChannelId channel) const noexcept = 0;

    // 只发「请求终止」：非阻塞、幂等。执行由 T-021 的平台适配完成，
    // Kernel 不碰进程句柄（01-kernel.md K-10）。
    virtual void requestTerminate(ChannelId channel) noexcept = 0;
};

}  // namespace bbcoop::ipc
