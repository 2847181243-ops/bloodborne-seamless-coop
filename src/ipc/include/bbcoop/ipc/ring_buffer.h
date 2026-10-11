// T-021 数据面：单生产者/单消费者环形缓冲（08-ipc.md §4.2）。
// owner: AI-00（T-021）。
//
// 设计取舍：
//   * 缓冲内存由调用方提供（不分配），容量固定；
//   * 记录带 4 字节长度前缀；满时 tryPush 返回 false 并计数，绝不阻塞；
//   * head/tail 放在**调用方提供的状态块**里，而不是留在本对象内部 ——
//     跨进程数据面要求索引本身也在共享内存里（T-021 共享内存通道）。
//     把索引留在各自进程的对象里，两个进程就只是在各自的缓冲上自说自话。

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>

namespace bbcoop::ipc {

// 写入/读取的字节视图类型。用 std::byte 表达「原始字节」，避免 char 的有符号歧义。
using ByteSpan = std::span<const std::byte>;
using MutableByteSpan = std::span<std::byte>;

// 环形缓冲的共享状态。跨进程时这块内存位于共享内存映射内，两侧进程各构造一个
// SpscRingBuffer 指向**同一份** state 与 storage。
//
// 约束：必须是 lock-free 原子 —— 带锁的实现放进共享内存后，锁只在写入者进程内有意义。
//
// 刻意**不加** `alignas(64)`：MSVC 对「因对齐说明符而补齐的结构」报 C4324（级别 4），
// 而本仓库的编译标准是 /W4 /WX。缓存行隔离属于 T-013 的实测范围，等有数据再决定
// 用什么形式（显式补齐 vs 分开分配），不在没有测量时先付出「门禁变红」的代价。
struct RingBufferState {
    std::atomic<std::size_t> head{0};  // 生产者的写入位置（单调递增，对容量取模）
    std::atomic<std::size_t> tail{0};  // 消费者的读取位置（单调递增，对容量取模）
    std::atomic<std::size_t> dropped{0};
    std::atomic<std::size_t> oversizeDrops{0};
};

static_assert(std::atomic<std::size_t>::is_always_lock_free,
              "RingBufferState 必须是 lock-free：带锁的状态放进共享内存没有跨进程语义");

class SpscRingBuffer {
public:
    static constexpr std::size_t kLengthPrefixSize = 4;

    // state 与 storage 都由调用方持有：进程内用栈/成员，跨进程用共享内存。
    // storage 必须至少有 kLengthPrefixSize + 1 字节。
    SpscRingBuffer(RingBufferState& state, MutableByteSpan storage) noexcept;

    SpscRingBuffer(const SpscRingBuffer&) = delete;
    SpscRingBuffer& operator=(const SpscRingBuffer&) = delete;

    // 尝试写入一条记录。成功返回 true；空间不足（或记录本身大于容量）返回 false，
    // 并计入 droppedCount()。绝不阻塞、不分配。
    bool tryPush(ByteSpan record) noexcept;

    // 同 tryPush，但记录由两段拼成（先 first 后 second），视为**一条**记录。
    // 目的是让「头 + 负载」不必先拷贝到一块连续暂存区就能入队。
    bool tryPushParts(ByteSpan first, ByteSpan second) noexcept;

    // 尝试读出一条记录，写入 out；返回写入 out 的字节数。
    // 返回 0 表示当前没有完整记录（或 out 太小 —— 后者计入 oversizeDrops()）。
    std::size_t tryPop(MutableByteSpan out) noexcept;

    bool empty() const noexcept;
    std::size_t droppedCount() const noexcept {
        return m_state.dropped.load(std::memory_order_relaxed);
    }
    std::size_t oversizeDrops() const noexcept {
        return m_state.oversizeDrops.load(std::memory_order_relaxed);
    }
    std::size_t capacity() const noexcept { return m_storage.size(); }

private:
    RingBufferState& m_state;
    MutableByteSpan m_storage;
};

}  // namespace bbcoop::ipc
