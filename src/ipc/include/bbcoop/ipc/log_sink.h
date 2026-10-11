// T-021：日志回传的接收端队列（08-ipc.md §6 / K-7）。
// owner: AI-00（T-021）。
//
// 分级规则（D-11）：
//   Diagnostic —— 队列满即丢，计入 droppedDiagnostic()；
//   Audit      —— 队列满**不丢**：submit 返回 false，调用方必须显式处理
//                 （短背压或进入失败态）；计入 auditBackpressure()。
// 辅助进程永远不写文件；本队列的另一端是 Kernel 的日志设施（T-005）。

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>

#include "bbcoop/ipc/protocol.h"

namespace bbcoop::ipc {

class LogSink {
public:
    static constexpr std::size_t kCapacity = 64;
    static constexpr std::size_t kMaxRecordBytes = 512;

    struct Record {
        Durability durability = Durability::Diagnostic;
        std::uint32_t size = 0;
        std::byte payload[kMaxRecordBytes]{};
    };

    LogSink() noexcept = default;

    // 提交一条记录。返回 true 表示已入队/已丢弃（诊断类），false 表示：
    //   * Audit 且队列满（未丢弃，调用方需背压/失败处理），或
    //   * 记录超过 kMaxRecordBytes（两类都不入队；诊断类计入 dropped，审计类计入 backpressure）。
    bool submit(Durability durability, std::span<const std::byte> record) noexcept;

    // 取出最早的一条记录；队列空返回 false。
    bool take(Record& out) noexcept;

    std::size_t size() const noexcept;
    std::size_t droppedDiagnostic() const noexcept {
        return m_droppedDiagnostic.load(std::memory_order_relaxed);
    }
    std::size_t auditBackpressure() const noexcept {
        return m_auditBackpressure.load(std::memory_order_relaxed);
    }

private:
    Record m_records[kCapacity];
    std::atomic<std::size_t> m_head{0};
    std::atomic<std::size_t> m_tail{0};
    std::atomic<std::size_t> m_droppedDiagnostic{0};
    std::atomic<std::size_t> m_auditBackpressure{0};
};

}  // namespace bbcoop::ipc
