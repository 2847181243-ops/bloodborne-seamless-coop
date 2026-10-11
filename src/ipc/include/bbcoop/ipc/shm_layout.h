// T-021：共享内存通道的布局（08-ipc.md D-1/D-12、§4.2、§5.2）。
// owner: AI-00（T-021）。
//
// 这块布局是**两个进程共同解释**的内存：
//   * 只放 POD 与 lock-free 原子，不放指针、不放 STL（D-5 的同一条理由）；
//   * 双方各自构造自己的 SpscRingBuffer/SharedChannel 视图，指向同一份内存；
//   * 创建者写 header（魔数 / 布局版本 / 布局尺寸），打开者**校验后**才使用，
//     不匹配一律 fail-closed（D-12）——这是防「两个不同版本的产物互相说话」的闸门。
//
// 注意：布局一旦发布就不能改字段顺序/尺寸，只能升 kLayoutVersion。

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>

#include "bbcoop/ipc/protocol.h"
#include "bbcoop/ipc/ring_buffer.h"

namespace bbcoop::ipc {

inline constexpr std::uint32_t kLayoutMagic = 0x42424331;  // 'B','B','C','1'
inline constexpr std::uint32_t kLayoutVersion = 1;

// 每个方向的环形缓冲容量。**暂定**：任务书未给数值，待 T-013 实测后定
// （08-ipc.md §9「环形缓冲容量与背压参数」）。
inline constexpr std::size_t kRingCapacity = 16 * 1024;

// 一条记录（含 12 字节 RecordHeader）能占用的最大字节数。
inline constexpr std::size_t kMaxRecordBytes = kRingCapacity - SpscRingBuffer::kLengthPrefixSize;

// 一侧的握手块：**自己写、对端读**。
//
// 与 RingBufferState 同样刻意不加 `alignas`：MSVC 的 C4324（级别 4）会因对齐说明符
// 造成的补齐而报错，而本仓库是 /W4 /WX。缓存行隔离待 T-013 实测后再定。
struct HandshakeBlock {
    std::atomic<std::uint32_t> magic;
    std::atomic<std::uint32_t> protocolVersion;
    std::atomic<std::uint32_t> ready;
    std::atomic<std::uint32_t> reserved;
};

// 映射头：创建者写，打开者校验。
struct HandshakeHeader {
    std::atomic<std::uint32_t> magic;
    std::atomic<std::uint32_t> layoutVersion;
    std::atomic<std::uint64_t> layoutSize;
};

struct SharedChannelLayout {
    HandshakeHeader header;
    HandshakeBlock host;
    HandshakeBlock aux;

    // 心跳计数（§5.2）：辅助侧写、宿主侧每 tick 本地读，零 IPC 成本。
    std::atomic<std::uint64_t> auxHeartbeat;

    // 通道关闭标志：任一侧置位，幂等（D-7）。
    std::atomic<std::uint32_t> closedByHost;
    std::atomic<std::uint32_t> closedByAux;

    // 数据面：两个方向各一条 SPSC 环。
    RingBufferState hostToAuxRing;
    RingBufferState auxToHostRing;
    std::byte hostToAuxStorage[kRingCapacity];
    std::byte auxToHostStorage[kRingCapacity];
};

static_assert(std::is_trivially_destructible<SharedChannelLayout>::value,
              "共享内存里的布局不能有非平凡析构：没有任何一个进程负责销毁它");
static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
              "心跳计数必须 lock-free，否则跨进程读到的不是同一份状态");

inline void resetHandshakeBlock(HandshakeBlock& block) noexcept {
    block.magic.store(0, std::memory_order_relaxed);
    block.protocolVersion.store(0, std::memory_order_relaxed);
    block.ready.store(0, std::memory_order_relaxed);
    block.reserved.store(0, std::memory_order_relaxed);
}

inline void resetRingBufferState(RingBufferState& state) noexcept {
    state.head.store(0, std::memory_order_relaxed);
    state.tail.store(0, std::memory_order_relaxed);
    state.dropped.store(0, std::memory_order_relaxed);
    state.oversizeDrops.store(0, std::memory_order_relaxed);
}

// 由**创建者**调用一次：写头部标识并清零全部状态。
// 打开者不调用（它只校验，见 ChannelMapping::open）。
inline void initSharedLayout(SharedChannelLayout& layout) noexcept {
    layout.header.magic.store(kLayoutMagic, std::memory_order_relaxed);
    layout.header.layoutVersion.store(kLayoutVersion, std::memory_order_relaxed);
    layout.header.layoutSize.store(static_cast<std::uint64_t>(sizeof(SharedChannelLayout)),
                                   std::memory_order_relaxed);
    resetHandshakeBlock(layout.host);
    resetHandshakeBlock(layout.aux);
    layout.auxHeartbeat.store(0, std::memory_order_relaxed);
    layout.closedByHost.store(0, std::memory_order_relaxed);
    layout.closedByAux.store(0, std::memory_order_relaxed);
    resetRingBufferState(layout.hostToAuxRing);
    resetRingBufferState(layout.auxToHostRing);
}

// 打开者侧的校验：魔数 / 布局版本 / 布局尺寸三者任一不符即拒绝使用。
// 返回 true 表示这块映射确实是本版本产物，可以按 SharedChannelLayout 解释。
inline bool sharedLayoutMatches(const SharedChannelLayout& layout) noexcept {
    return layout.header.magic.load(std::memory_order_relaxed) == kLayoutMagic &&
           layout.header.layoutVersion.load(std::memory_order_relaxed) == kLayoutVersion &&
           layout.header.layoutSize.load(std::memory_order_relaxed) ==
               static_cast<std::uint64_t>(sizeof(SharedChannelLayout));
}

}  // namespace bbcoop::ipc
