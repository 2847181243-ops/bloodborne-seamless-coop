// T-021 单元测试：环形缓冲 / 生命周期注册表 / 重启监督 / 日志队列。
// owner: AI-00（T-021）。
//
// 这些用例对应 docs/architecture/08-ipc.md 的 D-1~D-13 与 A-1/A-2/A-6：
//   * 环形缓冲：满时丢并计数、绝不阻塞（D-1/D-2）；
//   * 注册表：通道关闭幂等、终止请求幂等（D-7/D-9）；
//   * 监督：指数退避 + 封顶 + 稳定窗口重置 + 启动强度上限（D-10）；
//   * 日志队列：诊断丢 + 计数，审计不静默丢（D-11）。

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#include "bbcoop/ipc/liveness_registry.h"
#include "bbcoop/ipc/log_sink.h"
#include "bbcoop/ipc/protocol.h"
#include "bbcoop/ipc/ring_buffer.h"
#include "bbcoop/ipc/supervisor.h"
#include "test_channel.h"
#include "test_framework.h"

namespace {

using bbcoop::ipc::ByteSpan;
using bbcoop::ipc::ChannelId;
using bbcoop::ipc::ChannelState;
using bbcoop::ipc::Durability;
using bbcoop::ipc::LivenessRegistry;
using bbcoop::ipc::LogSink;
using bbcoop::ipc::MutableByteSpan;
using bbcoop::ipc::RestartPolicy;
using bbcoop::ipc::RestartSupervisor;
using bbcoop::ipc::RingBufferState;
using bbcoop::ipc::SpscRingBuffer;

std::vector<std::byte> bytes(const char* text) {
    const std::size_t n = std::strlen(text);
    std::vector<std::byte> out(n);
    for (std::size_t i = 0; i < n; ++i) {
        out[i] = static_cast<std::byte>(static_cast<unsigned char>(text[i]));
    }
    return out;
}

void testRingBufferRoundTrip() {
    std::array<std::byte, 64> storage{};
    RingBufferState state;
    SpscRingBuffer ring(state, MutableByteSpan(storage.data(), storage.size()));
    CHECK(ring.empty());

    const auto a = bytes("alpha");
    const auto b = bytes("beta");
    CHECK(ring.tryPush(ByteSpan(a.data(), a.size())));
    CHECK(ring.tryPush(ByteSpan(b.data(), b.size())));
    CHECK_EQ(ring.droppedCount(), static_cast<std::size_t>(0));

    std::array<std::byte, 32> out{};
    const std::size_t n1 = ring.tryPop(MutableByteSpan(out.data(), out.size()));
    CHECK_EQ(n1, a.size());
    CHECK_EQ(std::memcmp(out.data(), a.data(), n1), 0);
    const std::size_t n2 = ring.tryPop(MutableByteSpan(out.data(), out.size()));
    CHECK_EQ(n2, b.size());
    CHECK(ring.empty());
    CHECK_EQ(ring.tryPop(MutableByteSpan(out.data(), out.size())), static_cast<std::size_t>(0));
}

void testRingBufferDropsAndNeverBlocks() {
    std::array<std::byte, 32> storage{};
    RingBufferState state;
    SpscRingBuffer ring(state, MutableByteSpan(storage.data(), storage.size()));
    const auto r = bytes("0123456789");  // 10 + 4 字节头

    std::size_t accepted = 0;
    for (int i = 0; i < 8; ++i) {
        if (ring.tryPush(ByteSpan(r.data(), r.size()))) {
            ++accepted;
        }
    }
    CHECK(accepted > 0);
    CHECK(accepted < 8);
    CHECK_EQ(accepted + ring.droppedCount(), static_cast<std::size_t>(8));

    // 超大记录：直接拒绝并计数，不影响已有内容。
    const auto huge = bytes("this-record-is-way-too-large-for-the-buffer");
    CHECK(!ring.tryPush(ByteSpan(huge.data(), huge.size())));
    CHECK_EQ(ring.oversizeDrops(), static_cast<std::size_t>(1));
}

void testRingBufferWrapAround() {
    std::array<std::byte, 24> storage{};
    RingBufferState state;
    SpscRingBuffer ring(state, MutableByteSpan(storage.data(), storage.size()));
    const auto r = bytes("wrap");
    std::array<std::byte, 8> out{};
    for (int round = 0; round < 20; ++round) {
        CHECK(ring.tryPush(ByteSpan(r.data(), r.size())));
        const std::size_t n = ring.tryPop(MutableByteSpan(out.data(), out.size()));
        CHECK_EQ(n, r.size());
    }
    CHECK_EQ(ring.droppedCount(), static_cast<std::size_t>(0));
}

// 两段拼成一条记录：跨边界调用的「记录头 + 负载」就是这样过环的（零拷贝）。
void testRingBufferTwoPartRecord() {
    std::array<std::byte, 64> storage{};
    RingBufferState state;
    SpscRingBuffer ring(state, MutableByteSpan(storage.data(), storage.size()));

    const auto head = bytes("HEADER");
    const auto body = bytes("payload");
    CHECK(ring.tryPushParts(ByteSpan(head.data(), head.size()),
                            ByteSpan(body.data(), body.size())));

    // 一条记录，不是两条。
    std::array<std::byte, 32> out{};
    const std::size_t n = ring.tryPop(MutableByteSpan(out.data(), out.size()));
    CHECK_EQ(n, head.size() + body.size());
    CHECK_EQ(std::memcmp(out.data(), "HEADERpayload", n), 0);
    CHECK(ring.empty());
    CHECK_EQ(ring.tryPop(MutableByteSpan(out.data(), out.size())), static_cast<std::size_t>(0));
}

void testLivenessIdempotency() {
    LivenessRegistry reg;
    const ChannelId ch{7};

    int callbacks = 0;
    void* user = &callbacks;
    const auto cb = [](void* u, ChannelId) noexcept {
        ++(*static_cast<int*>(u));
    };

    CHECK_EQ(static_cast<int>(reg.watch(ch, cb, user)), static_cast<int>(bbcoop::Error::Ok));
    CHECK_EQ(static_cast<int>(reg.watch(ch, cb, user)),
             static_cast<int>(bbcoop::Error::InvalidArgument));

    CHECK_EQ(reg.heartbeatCounter(ch), static_cast<std::uint64_t>(0));
    reg.noteHeartbeat(ch);
    reg.noteHeartbeat(ch);
    CHECK_EQ(reg.heartbeatCounter(ch), static_cast<std::uint64_t>(2));
    CHECK(reg.state(ch) == ChannelState::Ready);

    reg.requestTerminate(ch);
    reg.requestTerminate(ch);  // 幂等
    CHECK_EQ(reg.terminateRequests(ch), static_cast<std::uint32_t>(1));
    CHECK(reg.state(ch) == ChannelState::Terminating);

    reg.onChannelClosed(ch);
    reg.onChannelClosed(ch);  // 幂等
    CHECK_EQ(callbacks, 1);
    CHECK_EQ(reg.channelClosures(ch), static_cast<std::uint32_t>(1));
    CHECK(reg.state(ch) == ChannelState::Gone);
}

void testLivenessTableFull() {
    LivenessRegistry reg;
    for (std::uint32_t i = 0; i < LivenessRegistry::kMaxChannels; ++i) {
        CHECK_EQ(static_cast<int>(reg.watch(ChannelId{i}, nullptr, nullptr)),
                 static_cast<int>(bbcoop::Error::Ok));
    }
    CHECK_EQ(static_cast<int>(reg.watch(ChannelId{999}, nullptr, nullptr)),
             static_cast<int>(bbcoop::Error::Unavailable));
    CHECK(reg.state(ChannelId{999}) == ChannelState::Gone);
}

void testSupervisorBackoff() {
    RestartPolicy policy;  // 全部为暂定值：10s 起、300s 封顶
    RestartSupervisor sup(policy);

    CHECK(sup.onChannelClosed(1000));
    const std::uint64_t first = sup.currentDelayMs();
    CHECK(first >= policy.initialDelayMs);
    CHECK(first <= policy.initialDelayMs + (policy.initialDelayMs * policy.jitterPermille) / 1000);

    CHECK(sup.onChannelClosed(2000));
    const std::uint64_t second = sup.currentDelayMs();
    CHECK(second >= policy.initialDelayMs * 2);
    CHECK(second <= (policy.initialDelayMs * 2) + (policy.initialDelayMs * 2 * policy.jitterPermille) / 1000);
    CHECK_EQ(sup.consecutiveFailures(), static_cast<std::uint32_t>(2));

    for (int i = 0; i < 8; ++i) {
        CHECK(sup.onChannelClosed(3000));
    }
    CHECK_EQ(sup.currentDelayMs() <= policy.maxDelayMs +
             (policy.maxDelayMs * policy.jitterPermille) / 1000, true);
}

void testSupervisorStableWindowResets() {
    RestartPolicy policy;
    RestartSupervisor sup(policy);
    CHECK(sup.onChannelClosed(0));
    sup.onReady(1000);
    sup.poll(1000 + policy.stableWindowMs);
    CHECK_EQ(sup.consecutiveFailures(), static_cast<std::uint32_t>(0));
    CHECK_EQ(sup.nextRestartAtMs(), static_cast<std::uint64_t>(0));
}

void testSupervisorStartLimit() {
    RestartPolicy policy;
    policy.maxStartsPerWindow = 3;
    RestartSupervisor sup(policy);
    for (std::uint32_t i = 0; i < policy.maxStartsPerWindow; ++i) {
        sup.onStarted(i * 10);
    }
    CHECK(!sup.onChannelClosed(100));
    CHECK(sup.exhausted());
    CHECK_EQ(sup.nextRestartAtMs(), static_cast<std::uint64_t>(0));
}

void testLogSinkDurability() {
    LogSink sink;
    const auto r = bytes("hello");

    for (std::size_t i = 0; i < LogSink::kCapacity; ++i) {
        CHECK(sink.submit(Durability::Diagnostic, ByteSpan(r.data(), r.size())));
    }
    // 诊断类：满时丢弃但报告成功，并计数。
    CHECK(sink.submit(Durability::Diagnostic, ByteSpan(r.data(), r.size())));
    CHECK_EQ(sink.droppedDiagnostic(), static_cast<std::size_t>(1));

    // 审计类：满时返回失败且不丢（不静默丢，D-11）。
    CHECK(!sink.submit(Durability::Audit, ByteSpan(r.data(), r.size())));
    CHECK_EQ(sink.auditBackpressure(), static_cast<std::size_t>(1));

    LogSink::Record out{};
    CHECK(sink.take(out));
    CHECK_EQ(out.size, static_cast<std::uint32_t>(r.size()));
    CHECK(out.durability == Durability::Diagnostic);
    CHECK_EQ(sink.size(), LogSink::kCapacity - 1);
}

void testLogSinkOversizeAudit() {
    LogSink sink;
    std::vector<std::byte> big(LogSink::kMaxRecordBytes + 1, std::byte{0x41});
    CHECK(!sink.submit(Durability::Audit, ByteSpan(big.data(), big.size())));
    CHECK_EQ(sink.auditBackpressure(), static_cast<std::size_t>(1));
    CHECK(sink.submit(Durability::Diagnostic, ByteSpan(big.data(), big.size())));
    CHECK_EQ(sink.droppedDiagnostic(), static_cast<std::size_t>(1));
}

}  // namespace

int main(int argc, char** argv) {
    // 本可执行文件同时充当「辅助进程」：跨进程用例用真实进程边界验证通道
    // （见 test_channel.cpp）。参数是共享内存映射名。
    if (argc >= 3 && std::strcmp(argv[1], "--aux-child") == 0) {
        return bbcoop::test::runAuxChild(argv[2]);
    }

    testRingBufferRoundTrip();
    testRingBufferDropsAndNeverBlocks();
    testRingBufferWrapAround();
    testRingBufferTwoPartRecord();
    testLivenessIdempotency();
    testLivenessTableFull();
    testSupervisorBackoff();
    testSupervisorStableWindowResets();
    testSupervisorStartLimit();
    testLogSinkDurability();
    testLogSinkOversizeAudit();
    bbcoop::test::runChannelTests();
    bbcoop::test::runCrossProcessChannelTests();

    std::printf("checks=%d failures=%d\n", bbcoop::test::g_checks, bbcoop::test::g_failures);
    return (bbcoop::test::g_failures == 0) ? 0 : 1;
}
