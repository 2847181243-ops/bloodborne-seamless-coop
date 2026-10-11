// T-021：LogSink 实现（08-ipc.md §6）。
// owner: AI-00（T-021）。

#include "bbcoop/ipc/log_sink.h"

#include <cstring>

namespace bbcoop::ipc {

bool LogSink::submit(Durability durability, std::span<const std::byte> record) noexcept {
    if (record.size() > kMaxRecordBytes) {
        if (durability == Durability::Audit) {
            m_auditBackpressure.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        m_droppedDiagnostic.fetch_add(1, std::memory_order_relaxed);
        return true;  // 诊断类：丢弃是允许的，已计数
    }

    const std::size_t head = m_head.load(std::memory_order_relaxed);
    const std::size_t tail = m_tail.load(std::memory_order_acquire);
    if (head - tail >= kCapacity) {
        if (durability == Durability::Audit) {
            m_auditBackpressure.fetch_add(1, std::memory_order_relaxed);
            return false;  // 审计类绝不静默丢
        }
        m_droppedDiagnostic.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    Record& slot = m_records[head % kCapacity];
    slot.durability = durability;
    slot.size = static_cast<std::uint32_t>(record.size());
    if (!record.empty()) {
        std::memcpy(slot.payload, record.data(), record.size());
    }
    m_head.store(head + 1, std::memory_order_release);
    return true;
}

bool LogSink::take(Record& out) noexcept {
    const std::size_t tail = m_tail.load(std::memory_order_relaxed);
    const std::size_t head = m_head.load(std::memory_order_acquire);
    if (tail == head) {
        return false;
    }
    out = m_records[tail % kCapacity];
    m_tail.store(tail + 1, std::memory_order_release);
    return true;
}

std::size_t LogSink::size() const noexcept {
    const std::size_t head = m_head.load(std::memory_order_acquire);
    const std::size_t tail = m_tail.load(std::memory_order_acquire);
    return head - tail;
}

}  // namespace bbcoop::ipc
