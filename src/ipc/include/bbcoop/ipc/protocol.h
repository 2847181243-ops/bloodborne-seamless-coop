// T-021 IPC 协议常量与可跨边界类型（drafts: docs/architecture/08-ipc.md）。
// owner: AI-00（T-021）。本文件只放 POD 与常量：跨边界类型必须可平凡拷贝。
//
// 约束（08-ipc.md D-5）：指针、引用、std::function、STL 容器一律不可跨边界。

#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace bbcoop::ipc {

// 协议版本：host 与 aux 握手时核对，不匹配 fail-closed（08-ipc.md D-12）。
inline constexpr std::uint32_t kProtocolVersion = 1;

// 心跳卡住阈值 N：任务书未给数值，**暂定** 3000ms（08-ipc.md D-8）。
// 实现必须保留「暂定」字样，不得写成任务书规定。
inline constexpr std::uint64_t kHeartbeatStaleMs = 3000;

// 优雅停止宽限期：任务书未给数值，暂定 1000ms（08-ipc.md D-9）。
inline constexpr std::uint32_t kGracefulStopMs = 1000;

// 心跳写入节奏上限：必须与 N 对齐（08-ipc.md D-8：节奏 <= N/3）。
inline constexpr std::uint64_t kHeartbeatIntervalMs = kHeartbeatStaleMs / 3;

// ChannelId：辅助进程里一条 IPC 通道的稳定标识。
struct ChannelId {
    std::uint32_t value = 0;

    friend constexpr bool operator==(ChannelId a, ChannelId b) noexcept {
        return a.value == b.value;
    }
    friend constexpr bool operator!=(ChannelId a, ChannelId b) noexcept {
        return a.value != b.value;
    }
};

// 通道/模块生命周期状态（08-ipc.md D-7 / §5.1）。
enum class ChannelState : std::uint8_t {
    Starting = 0,
    Ready = 1,
    Stale = 2,
    Terminating = 3,
    Gone = 4,
};

// 模块不可用的原因：自检报告必须能区分（07-kernel-seam §1.5）。
enum class UnavailableReason : std::uint8_t {
    None = 0,
    InitFailed = 1,
    TickTimeout = 2,
    TickFailures = 3,
    ChannelClosed = 4,
    HeartbeatStale = 5,
};

// 日志记录的耐久性类别（K-7 / 08-ipc.md D-11）：
//   Diagnostic —— 队列满时可丢，必须计数；
//   Audit      —— 不静默丢（短背压或显式失败态）。
enum class Durability : std::uint8_t {
    Diagnostic = 0,
    Audit = 1,
};

// 控制面消息种类（08-ipc.md §4.1）。
enum class MessageKind : std::uint32_t {
    Hello = 1,
    Ready = 2,
    Heartbeat = 3,
    CallResult = 4,
    LogRecord = 5,
    Stop = 6,
    Kill = 7,
};

constexpr const char* to_string(ChannelState s) noexcept {
    switch (s) {
        case ChannelState::Starting: return "starting";
        case ChannelState::Ready: return "ready";
        case ChannelState::Stale: return "stale";
        case ChannelState::Terminating: return "terminating";
        case ChannelState::Gone: return "gone";
    }
    return "unknown";
}

constexpr const char* to_string(UnavailableReason r) noexcept {
    switch (r) {
        case UnavailableReason::None: return "none";
        case UnavailableReason::InitFailed: return "init_failed";
        case UnavailableReason::TickTimeout: return "tick_timeout";
        case UnavailableReason::TickFailures: return "tick_failures";
        case UnavailableReason::ChannelClosed: return "channel_closed";
        case UnavailableReason::HeartbeatStale: return "heartbeat_stale";
    }
    return "unknown";
}

constexpr const char* to_string(Durability d) noexcept {
    switch (d) {
        case Durability::Diagnostic: return "diagnostic";
        case Durability::Audit: return "audit";
    }
    return "unknown";
}

// 跨边界记录的最小头（定长、无指针）。
struct RecordHeader {
    MessageKind kind = MessageKind::Heartbeat;
    Durability durability = Durability::Diagnostic;
    std::uint32_t size = 0;  // 负载字节数，不含本头
};

static_assert(sizeof(ChannelId) == 4, "ChannelId must stay 4 bytes");
static_assert(sizeof(RecordHeader) == 12, "RecordHeader must stay 12 bytes");
static_assert(std::is_trivially_copyable<RecordHeader>::value,
              "RecordHeader must be trivially copyable");

}  // namespace bbcoop::ipc
