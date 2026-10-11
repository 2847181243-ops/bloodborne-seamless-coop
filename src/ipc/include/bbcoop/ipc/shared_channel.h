// T-021：共享内存通道（08-ipc.md D-1/D-12/D-13、§4.2、§5.2）。
// owner: AI-00（T-021）。
//
// 本类是**两侧共用**的一段逻辑：宿主侧与辅助侧各构造一个实例，指向同一块
// SharedChannelLayout；谁是哪一侧由 ChannelRole 决定，方向随之确定。
//
// 数据面（调用）走 host->aux 环；结果、日志、心跳走 aux->host 环。
// 心跳计数与关闭标志直接读共享内存，不产生系统调用（§5.2）。

#pragma once

#include <cstddef>
#include <cstdint>

#include "bbcoop/ipc/channel.h"
#include "bbcoop/ipc/protocol.h"
#include "bbcoop/ipc/ring_buffer.h"
#include "bbcoop/ipc/shm_layout.h"
#include "errors.h"

namespace bbcoop::ipc {

enum class ChannelRole : std::uint8_t {
    Host = 0,  // 游戏进程侧：代理在这里
    Aux = 1,   // 辅助进程侧：桩在这里
};

class SharedChannel final : public ICallChannel {
public:
    SharedChannel(SharedChannelLayout& layout, ChannelRole role) noexcept;

    SharedChannel(const SharedChannel&) = delete;
    SharedChannel& operator=(const SharedChannel&) = delete;

    // —— ICallChannel ——
    // available() = 双方已完成握手 且 没有任何一侧标记关闭。
    bool available() const noexcept override;
    bool trySendCall(CallOp op, ByteSpan payload, std::uint32_t& callId) noexcept override;
    bool trySendMessage(MessageKind kind, Durability durability, ByteSpan payload) noexcept override;

    // —— 握手（D-12）——
    // 写自己那一块：魔数 + 协议版本 + ready。ready 用 release 最后写。
    void publishHello() noexcept;
    // 读对端那一块。NotInitialized = 对端还没发布；InvalidArgument = 版本/魔数不符
    // （fail-closed：调用方必须拒绝启动，不得「先连上再说」）。
    Error checkPeerHandshake() const noexcept;
    bool handshakeComplete() const noexcept;

    // —— 接收 ——
    // 取出一条记录。返回 false 表示当前没有完整记录。
    // 成功时 payload 指向**本对象内部**的暂存区，有效期到下一次 tryReceive。
    // 零分配：暂存区是成员数组，尺寸上界 = kMaxRecordBytes。
    bool tryReceive(RecordHeader& header, ByteSpan& payload) noexcept;

    // —— 心跳（§5.2：辅助侧写、宿主侧读）——
    void noteHeartbeat() noexcept;
    std::uint64_t heartbeatCounter() const noexcept;

    // —— 关闭（D-7：幂等）——
    void markClosed() noexcept;
    bool closed() const noexcept;
    bool closedByPeer() const noexcept;

    ChannelRole role() const noexcept { return m_role; }

    // 自检计数：成功送达 / 被拒（不可用或环满）。A-3 的调用次数断言用这两个。
    std::size_t sentRecords() const noexcept { return m_sent; }
    std::size_t refusedRecords() const noexcept { return m_refused; }

    // 下一条调用用的编号（单调递增，§4.2）。
    std::uint32_t nextCallId() const noexcept { return m_nextCallId; }

private:
    const HandshakeBlock& localBlock() const noexcept;
    const HandshakeBlock& peerBlock() const noexcept;
    SpscRingBuffer& outbound() noexcept;
    SpscRingBuffer& inbound() noexcept;

    SharedChannelLayout& m_layout;
    ChannelRole m_role;
    SpscRingBuffer m_hostToAux;  // 宿主侧写、辅助侧读
    SpscRingBuffer m_auxToHost;  // 辅助侧写、宿主侧读
    std::byte m_inboundFrame[kMaxRecordBytes];
    std::size_t m_sent = 0;
    std::size_t m_refused = 0;
    std::uint32_t m_nextCallId = 1;  // 调用编号从 1 起，0 留给「无效」
};

}  // namespace bbcoop::ipc
