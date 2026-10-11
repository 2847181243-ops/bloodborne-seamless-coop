// T-021：SharedChannel 实现（08-ipc.md D-1/D-11/D-12/D-13）。
// owner: AI-00（T-021）。

#include "bbcoop/ipc/shared_channel.h"

#include <cstring>

#include "bbcoop/ipc/wire.h"

namespace bbcoop::ipc {
namespace {

// 记录头写进暂存区。RecordHeader 是可平凡拷贝的定长结构（protocol.h 有断言）。
void storeHeader(std::byte* out, MessageKind kind, Durability durability,
                 std::uint32_t size) noexcept {
    RecordHeader header{};
    header.kind = kind;
    header.durability = durability;
    header.size = size;
    std::memcpy(out, &header, sizeof(header));
}

}  // namespace

SharedChannel::SharedChannel(SharedChannelLayout& layout, ChannelRole role) noexcept
    : m_layout(layout),
      m_role(role),
      m_hostToAux(layout.hostToAuxRing, MutableByteSpan(layout.hostToAuxStorage, kRingCapacity)),
      m_auxToHost(layout.auxToHostRing, MutableByteSpan(layout.auxToHostStorage, kRingCapacity)) {}

const HandshakeBlock& SharedChannel::localBlock() const noexcept {
    return (m_role == ChannelRole::Host) ? m_layout.host : m_layout.aux;
}

const HandshakeBlock& SharedChannel::peerBlock() const noexcept {
    return (m_role == ChannelRole::Host) ? m_layout.aux : m_layout.host;
}

SpscRingBuffer& SharedChannel::outbound() noexcept {
    return (m_role == ChannelRole::Host) ? m_hostToAux : m_auxToHost;
}

SpscRingBuffer& SharedChannel::inbound() noexcept {
    return (m_role == ChannelRole::Host) ? m_auxToHost : m_hostToAux;
}

void SharedChannel::publishHello() noexcept {
    HandshakeBlock& local = (m_role == ChannelRole::Host) ? m_layout.host : m_layout.aux;
    local.magic.store(kLayoutMagic, std::memory_order_relaxed);
    local.protocolVersion.store(kProtocolVersion, std::memory_order_relaxed);
    local.ready.store(1, std::memory_order_release);  // ready 最后写：对端看到它就说明前面可见
}

Error SharedChannel::checkPeerHandshake() const noexcept {
    const HandshakeBlock& peer = peerBlock();
    if (peer.ready.load(std::memory_order_acquire) == 0) {
        return Error::NotInitialized;
    }
    if (peer.magic.load(std::memory_order_relaxed) != kLayoutMagic) {
        return Error::InvalidArgument;
    }
    if (peer.protocolVersion.load(std::memory_order_relaxed) != kProtocolVersion) {
        return Error::InvalidArgument;
    }
    return Error::Ok;
}

bool SharedChannel::handshakeComplete() const noexcept {
    const HandshakeBlock& local = localBlock();
    const HandshakeBlock& peer = peerBlock();
    return local.ready.load(std::memory_order_acquire) != 0 &&
           peer.ready.load(std::memory_order_acquire) != 0 &&
           local.protocolVersion.load(std::memory_order_relaxed) == kProtocolVersion &&
           peer.protocolVersion.load(std::memory_order_relaxed) == kProtocolVersion &&
           peer.magic.load(std::memory_order_relaxed) == kLayoutMagic;
}

bool SharedChannel::available() const noexcept {
    return handshakeComplete() && !closed();
}

bool SharedChannel::trySendCall(CallOp op, ByteSpan payload, std::uint32_t& callId) noexcept {
    if (!available()) {
        ++m_refused;
        return false;
    }
    if (payload.size() > kMaxRecordBytes - (sizeof(RecordHeader) + kCallPrefixBytes)) {
        ++m_refused;  // 超出单条记录上限：可判定地拒绝，不截断
        return false;
    }

    // 调用前缀：单调编号 + op（§4.2 的 call_id 与 D-4 的 op 一起过边界）。
    std::byte head[sizeof(RecordHeader) + kCallPrefixBytes]{};
    const std::uint32_t id = m_nextCallId++;
    storeHeader(head, MessageKind::Call, Durability::Diagnostic,
                static_cast<std::uint32_t>(kCallPrefixBytes + payload.size()));
    wire::writeU32(MutableByteSpan(head).subspan(sizeof(RecordHeader), wire::kU32Size), id);
    head[sizeof(RecordHeader) + wire::kU32Size] =
        static_cast<std::byte>(static_cast<std::uint8_t>(op));

    if (!outbound().tryPushParts(ByteSpan(head, sizeof(head)), payload)) {
        ++m_refused;
        return false;
    }
    callId = id;
    ++m_sent;
    return true;
}

bool SharedChannel::trySendMessage(MessageKind kind, Durability durability,
                                   ByteSpan payload) noexcept {
    if (!available()) {
        ++m_refused;
        return false;
    }
    if (payload.size() > kMaxRecordBytes - sizeof(RecordHeader)) {
        ++m_refused;
        return false;
    }

    std::byte head[sizeof(RecordHeader)]{};
    storeHeader(head, kind, durability, static_cast<std::uint32_t>(payload.size()));

    if (!outbound().tryPushParts(ByteSpan(head, sizeof(head)), payload)) {
        ++m_refused;
        return false;
    }
    ++m_sent;
    return true;
}

bool SharedChannel::tryReceive(RecordHeader& header, ByteSpan& payload) noexcept {
    const std::size_t size = inbound().tryPop(MutableByteSpan(m_inboundFrame, kMaxRecordBytes));
    if (size < sizeof(RecordHeader)) {
        return false;  // 没有记录，或记录被截断（后者已计入 oversizeDrops）
    }

    std::memcpy(&header, m_inboundFrame, sizeof(header));
    if (header.size != size - sizeof(RecordHeader)) {
        // 帧不自洽（对端是另一个进程，不能假设它说的是真的）：
        // 宁可丢弃整条记录，也不按声明的长度解释共享内存。
        return false;
    }
    payload = ByteSpan(m_inboundFrame + sizeof(RecordHeader), header.size);
    return true;
}

void SharedChannel::noteHeartbeat() noexcept {
    if (m_role != ChannelRole::Aux) {
        return;  // 只有辅助侧写心跳（§5.2）
    }
    m_layout.auxHeartbeat.fetch_add(1, std::memory_order_release);
}

std::uint64_t SharedChannel::heartbeatCounter() const noexcept {
    return m_layout.auxHeartbeat.load(std::memory_order_acquire);
}

void SharedChannel::markClosed() noexcept {
    if (m_role == ChannelRole::Host) {
        m_layout.closedByHost.store(1, std::memory_order_release);
    } else {
        m_layout.closedByAux.store(1, std::memory_order_release);
    }
}

bool SharedChannel::closed() const noexcept {
    return m_layout.closedByHost.load(std::memory_order_acquire) != 0 ||
           m_layout.closedByAux.load(std::memory_order_acquire) != 0;
}

bool SharedChannel::closedByPeer() const noexcept {
    return (m_role == ChannelRole::Host)
               ? m_layout.closedByAux.load(std::memory_order_acquire) != 0
               : m_layout.closedByHost.load(std::memory_order_acquire) != 0;
}

}  // namespace bbcoop::ipc
