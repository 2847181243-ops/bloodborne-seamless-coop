// T-021：TransportProxy 实现（08-ipc.md D-4/D-13）。
// owner: AI-00（T-021）。

#include "bbcoop/ipc/proxy.h"

namespace bbcoop::ipc {

Error TransportProxy::send(const std::uint8_t* data, std::size_t size) noexcept {
    if (!m_channel.available()) {
        return Error::Unavailable;  // 立刻返回，不阻塞 tick（D-13）
    }
    if (data == nullptr && size != 0) {
        return Error::InvalidArgument;
    }
    const auto payload = std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(data), size);
    std::uint32_t callId = 0;  // send 不等结果，编号只用于辅助侧的 CallResult 关联
    if (!m_channel.trySendCall(CallOp::TransportSend, payload, callId)) {
        return Error::Unavailable;  // 通道满：可判定的失败，不排队等待
    }
    return Error::Ok;
}

}  // namespace bbcoop::ipc
