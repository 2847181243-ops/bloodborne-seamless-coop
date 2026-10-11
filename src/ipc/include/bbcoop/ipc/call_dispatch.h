// T-021：辅助侧的调用分发与结果编码（代理的对端，08-ipc.md D-4/§4.1）。
// owner: AI-00（T-021）。
//
// 代理把「接口调用」变成一条 Call 记录；本文件把它翻译回对真实模块实现的调用，
// 并把返回值编成 CallResult 记录。两侧共用 protocol.h 里的负载布局常量。

#pragma once

#include <cstddef>
#include <cstdint>

#include "bbcoop/ipc/protocol.h"
#include "bbcoop/ipc/ring_buffer.h"
#include "bbcoop/ipc/stub.h"
#include "errors.h"

namespace bbcoop::ipc {

// 一条 Call 记录分发的产物。
struct CallDispatchResult {
    std::uint32_t callId = 0;
    Error error = Error::InvalidArgument;  // 桩的返回值；分发本身失败也是它
    std::size_t outSize = 0;               // 写进 out 的结果字节数
};

// 把一条 Call 记录的负载（call_id + op + 参数）分发到对应的桩。
// transport / session 允许为 nullptr（辅助进程没提供该模块）→ Unavailable。
CallDispatchResult dispatchCall(TransportStub* transport, SessionStub* session,
                                ByteSpan callPayload, MutableByteSpan out) noexcept;

// 把结果编成 CallResult 记录的负载。返回写入 out 的字节数；out 太小返回 0。
std::size_t encodeCallResult(std::uint32_t callId, Error error, ByteSpan result,
                             MutableByteSpan out) noexcept;

// 解析 CallResult 记录的负载。返回 false 表示负载不自洽（长度不够）。
bool decodeCallResult(ByteSpan payload, std::uint32_t& callId, Error& error,
                      ByteSpan& result) noexcept;

}  // namespace bbcoop::ipc
