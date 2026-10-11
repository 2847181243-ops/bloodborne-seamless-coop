// T-021：桩实现（08-ipc.md D-4）。
// owner: AI-00（T-021）。

#include "bbcoop/ipc/stub.h"

#include "bbcoop/ipc/wire.h"

namespace bbcoop::ipc {

Error TransportStub::handle(CallOp op, std::span<const std::byte> args,
                            std::span<std::byte> out, std::size_t& outSize) noexcept {
    outSize = 0;
    switch (op) {
        case CallOp::TransportConnected: {
            if (out.empty()) {
                return Error::InvalidArgument;
            }
            wire::writeBool(out, m_impl.connected());
            outSize = 1;
            return Error::Ok;
        }
        case CallOp::TransportSend: {
            if (out.empty()) {
                return Error::InvalidArgument;
            }
            const auto* data = reinterpret_cast<const std::uint8_t*>(args.data());
            const Error e = m_impl.send(data, args.size());
            out[0] = static_cast<std::byte>(static_cast<std::uint8_t>(e));
            outSize = 1;
            return Error::Ok;
        }
        case CallOp::TransportRtt: {
            if (out.size() < wire::kU32Size) {
                return Error::InvalidArgument;
            }
            wire::writeU32(out, m_impl.rtt_ms());
            outSize = wire::kU32Size;
            return Error::Ok;
        }
        default:
            return Error::InvalidArgument;
    }
}

Error SessionStub::handle(CallOp op, std::span<const std::byte> args,
                          std::span<std::byte> out, std::size_t& outSize) noexcept {
    (void)args;
    outSize = 0;
    switch (op) {
        case CallOp::SessionActive: {
            if (out.empty()) {
                return Error::InvalidArgument;
            }
            wire::writeBool(out, m_impl.active());
            outSize = 1;
            return Error::Ok;
        }
        case CallOp::SessionMemberCount: {
            if (out.size() < wire::kU32Size) {
                return Error::InvalidArgument;
            }
            wire::writeU32(out, m_impl.member_count());
            outSize = wire::kU32Size;
            return Error::Ok;
        }
        case CallOp::SessionIsHost: {
            if (out.empty()) {
                return Error::InvalidArgument;
            }
            wire::writeBool(out, m_impl.is_host());
            outSize = 1;
            return Error::Ok;
        }
        default:
            return Error::InvalidArgument;
    }
}

}  // namespace bbcoop::ipc
