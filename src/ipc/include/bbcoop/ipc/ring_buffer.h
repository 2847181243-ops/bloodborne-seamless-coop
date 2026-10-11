// T-021 数据面：单生产者/单消费者环形缓冲（08-ipc.md §4.2）。
// owner: AI-00（T-021）。
//
// 设计取舍：
//   * 缓冲内存由调用方提供（不分配），容量固定；
//   * 记录带 4 字节长度前缀；满时 tryPush 返回 false 并计数，绝不阻塞；
//   * 头尾用原子索引，SPSC 下无需锁。

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>

namespace bbcoop::ipc {

// 写入/读取的字节视图类型。用 std::byte 表达「原始字节」，避免 char 的有符号歧义。
using ByteSpan = std::span<const std::byte>;
using MutableByteSpan = std::span<std::byte>;

class SpscRingBuffer {
public:
    static constexpr std::size_t kLengthPrefixSize = 4;

    // storage 必须至少 kLengthPrefixSize + 1 字节；实际可用容量略小于 storage.size()。
    explicit SpscRingBuffer(MutableByteSpan storage) noexcept;

    SpscRingBuffer(const SpscRingBuffer&) = delete;
    SpscRingBuffer& operator=(const SpscRingBuffer&) = delete;

    // 尝试写入一条记录。成功返回 true；空间不足（或记录本身大于容量）返回 false，
    // 并计入 droppedCount()。绝不阻塞、不分配。
    bool tryPush(ByteSpan record) noexcept;

    // 尝试读出一条记录，写入 out；返回写入 out 的字节数。
    // 返回 0 表示当前没有完整记录（或 out 太小 —— 后者计入 oversizeDrops()）。
    std::size_t tryPop(MutableByteSpan out) noexcept;

    bool empty() const noexcept;
    std::size_t droppedCount() const noexcept { return m_dropped.load(std::memory_order_relaxed); }
    std::size_t oversizeDrops() const noexcept { return m_oversizeDrops.load(std::memory_order_relaxed); }
    std::size_t capacity() const noexcept { return m_storage.size(); }

private:
    MutableByteSpan m_storage;
    std::atomic<std::size_t> m_head{0};  // 生产者的写入位置（单调递增，对容量取模）
    std::atomic<std::size_t> m_tail{0};  // 消费者的读取位置（单调递增，对容量取模）
    std::atomic<std::size_t> m_dropped{0};
    std::atomic<std::size_t> m_oversizeDrops{0};
};

}  // namespace bbcoop::ipc
