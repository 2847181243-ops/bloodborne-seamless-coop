// T-021：调用分发与结果编码的实现（08-ipc.md D-4/§4.1）。
// owner: AI-00（T-021）。

#include "bbcoop/ipc/call_dispatch.h"

#include "bbcoop/ipc/wire.h"

namespace bbcoop::ipc {

CallDispatchResult dispatchCall(TransportStub* transport, SessionStub* session,
                                ByteSpan callPayload, MutableByteSpan out) noexcept {
    CallDispatchResult result;
    result.callId = 0;
    result.error = Error::InvalidArgument;
    result.outSize = 0;

    if (callPayload.size() < kCallPrefixBytes) {
        return result;  // 连前缀都不够：不是一条合法调用
    }
    result.callId = wire::readU32(callPayload.subspan(0, kCallIdBytes));
    const auto op = static_cast<CallOp>(
        static_cast<std::uint8_t>(callPayload[kCallIdBytes]));
    const ByteSpan args = callPayload.subspan(kCallPrefixBytes);

    switch (op) {
        case CallOp::TransportConnected:
        case CallOp::TransportSend:
        case CallOp::TransportRtt:
            if (transport == nullptr) {
                result.error = Error::Unavailable;
                return result;
            }
            result.error = transport->handle(op, args, out, result.outSize);
            return result;

        case CallOp::SessionActive:
        case CallOp::SessionMemberCount:
        case CallOp::SessionIsHost:
            if (session == nullptr) {
                result.error = Error::Unavailable;
                return result;
            }
            result.error = session->handle(op, args, out, result.outSize);
            return result;
    }
    return result;  // 未知 op：保持 InvalidArgument
}

std::size_t encodeCallResult(std::uint32_t callId, Error error, ByteSpan result,
                             MutableByteSpan out) noexcept {
    if (result.size() > out.size() || out.size() < kCallResultPrefixBytes) {
        return 0;
    }
    wire::writeU32(out.subspan(0, kCallIdBytes), callId);
    out[kCallIdBytes] = static_cast<std::byte>(static_cast<std::uint8_t>(error));
    for (std::size_t i = 0; i < result.size(); ++i) {
        out[kCallResultPrefixBytes + i] = result[i];
    }
    return kCallResultPrefixBytes + result.size();
}

bool decodeCallResult(ByteSpan payload, std::uint32_t& callId, Error& error,
                      ByteSpan& result) noexcept {
    if (payload.size() < kCallResultPrefixBytes) {
        return false;
    }
    callId = wire::readU32(payload.subspan(0, kCallIdBytes));
    error = static_cast<Error>(static_cast<std::uint8_t>(payload[kCallIdBytes]));
    result = payload.subspan(kCallResultPrefixBytes);
    return true;
}

}  // namespace bbcoop::ipc
