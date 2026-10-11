// T-021：代理与传输之间的最小通道接口（08-ipc.md D-1/D-2/D-13）。
// owner: AI-00（T-021）。
//
// 真实实现是共享内存环形缓冲 + 控制通道；这里只固化「代理怎么用它」：
//   * 两个发送方法都不阻塞、不分配；满/不可用返回 false；
//   * available() 为 false 时，代理必须立刻返回 Unavailable（绝不等待）；
//   * 组帧（op + payload 的长度前缀）由通道实现负责，代理只管语义。
//
// 关于耐久性：记录的耐久性必须**随记录一起过边界**（D-11），所以发送端接口带上
// 它，而不是让接收端猜。通道本身不替调用方决定「能不能丢」——它只如实返回
// 「送出去了没有」；可丢/不可丢的判断在调用方（辅助侧的日志路径）。

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "bbcoop/ipc/protocol.h"

namespace bbcoop::ipc {

class ICallChannel {
public:
    ICallChannel() = default;
    virtual ~ICallChannel() = default;
    ICallChannel(const ICallChannel&) = delete;
    ICallChannel& operator=(const ICallChannel&) = delete;

    // 通道当前是否可用。
    virtual bool available() const noexcept = 0;

    // 发送一条跨边界调用：op + 参数负载；返回 false 表示未送达（满/不可用）。
    // 成功时 callId 是该调用的单调编号（§4.2），辅助侧要在 CallResult 里回带它。
    virtual bool trySendCall(CallOp op, std::span<const std::byte> payload,
                             std::uint32_t& callId) noexcept = 0;

    // 发送一条控制面消息（Heartbeat/Stop/Kill/LogRecord 等）。
    virtual bool trySendMessage(MessageKind kind, Durability durability,
                                std::span<const std::byte> payload) noexcept = 0;
};

}  // namespace bbcoop::ipc
