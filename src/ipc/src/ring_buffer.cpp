// T-021 数据面：SPSC 环形缓冲实现（08-ipc.md §4.2）。
// owner: AI-00（T-021）。

#include "bbcoop/ipc/ring_buffer.h"

#include <cstring>

namespace bbcoop::ipc {
namespace {

// 把「单调递增的位置」映射到 storage 内的下标；容量不是 2 的幂也能用。
std::size_t wrap(std::size_t pos, std::size_t capacity) noexcept {
    return (capacity == 0) ? 0 : (pos % capacity);
}

}  // namespace

SpscRingBuffer::SpscRingBuffer(MutableByteSpan storage) noexcept
    : m_storage(storage) {}

bool SpscRingBuffer::empty() const noexcept {
    return m_head.load(std::memory_order_acquire) ==
           m_tail.load(std::memory_order_acquire);
}

bool SpscRingBuffer::tryPush(ByteSpan record) noexcept {
    const std::size_t capacity = m_storage.size();
    if (capacity <= kLengthPrefixSize || record.size() > capacity - kLengthPrefixSize) {
        m_oversizeDrops.fetch_add(1, std::memory_order_relaxed);
        m_dropped.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    const std::size_t head = m_head.load(std::memory_order_relaxed);
    const std::size_t tail = m_tail.load(std::memory_order_acquire);
    const std::size_t used = head - tail;
    const std::size_t need = kLengthPrefixSize + record.size();
    if (need > capacity - used) {
        m_dropped.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    const std::uint32_t length = static_cast<std::uint32_t>(record.size());
    const std::size_t prefixPos = wrap(head, capacity);
    for (std::size_t i = 0; i < kLengthPrefixSize; ++i) {
        m_storage[wrap(prefixPos + i, capacity)] =
            static_cast<std::byte>((length >> (8U * i)) & 0xFFU);
    }
    const std::size_t dataPos = wrap(prefixPos + kLengthPrefixSize, capacity);
    for (std::size_t i = 0; i < record.size(); ++i) {
        m_storage[wrap(dataPos + i, capacity)] = record[i];
    }

    m_head.store(head + need, std::memory_order_release);
    return true;
}

std::size_t SpscRingBuffer::tryPop(MutableByteSpan out) noexcept {
    const std::size_t capacity = m_storage.size();
    if (capacity <= kLengthPrefixSize) {
        return 0;
    }

    const std::size_t tail = m_tail.load(std::memory_order_relaxed);
    const std::size_t head = m_head.load(std::memory_order_acquire);
    if (head == tail) {
        return 0;
    }

    const std::size_t prefixPos = wrap(tail, capacity);
    std::uint32_t length = 0;
    for (std::size_t i = 0; i < kLengthPrefixSize; ++i) {
        const std::uint32_t byte = static_cast<std::uint32_t>(
            static_cast<unsigned char>(m_storage[wrap(prefixPos + i, capacity)]));
        length |= byte << (8U * i);
    }

    const std::size_t total = kLengthPrefixSize + length;
    if (head - tail < total) {
        return 0;  // 生产者还没写完这条记录
    }
    if (length > out.size()) {
        m_oversizeDrops.fetch_add(1, std::memory_order_relaxed);
        m_tail.store(tail + total, std::memory_order_release);
        return 0;
    }

    const std::size_t dataPos = wrap(prefixPos + kLengthPrefixSize, capacity);
    for (std::size_t i = 0; i < length; ++i) {
        out[i] = m_storage[wrap(dataPos + i, capacity)];
    }

    m_tail.store(tail + total, std::memory_order_release);
    return length;
}

}  // namespace bbcoop::ipc
