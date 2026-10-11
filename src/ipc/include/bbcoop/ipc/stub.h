// T-021：辅助进程侧的桩（08-ipc.md D-4）。
// owner: AI-00（T-021）。
//
// 桩把跨边界调用翻译成对真实模块实现的调用；它不持有指针以外的状态。
// 输出用调用方提供的缓冲区，避免分配；out 太小/op 未知 → InvalidArgument。

#pragma once

#include <cstddef>
#include <span>

#include "bbcoop/ipc/protocol.h"
#include "isession.h"
#include "itransport.h"

namespace bbcoop::ipc {

class TransportStub {
public:
    explicit TransportStub(ITransport& impl) noexcept : m_impl(impl) {}

    Error handle(CallOp op, std::span<const std::byte> args,
                 std::span<std::byte> out, std::size_t& outSize) noexcept;

private:
    ITransport& m_impl;
};

class SessionStub {
public:
    explicit SessionStub(ISession& impl) noexcept : m_impl(impl) {}

    Error handle(CallOp op, std::span<const std::byte> args,
                 std::span<std::byte> out, std::size_t& outSize) noexcept;

private:
    ISession& m_impl;
};

}  // namespace bbcoop::ipc
