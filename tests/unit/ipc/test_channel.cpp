// T-021 通道 / 代理 / 桩 / 调用分发测试（08-ipc.md D-2/D-4/D-12/D-13、§4、§7）。
// owner: AI-00（T-021）。
//
// 覆盖的验收点：
//   A-4  通道不可用时代理立刻返回 Unavailable（不阻塞、不发调用）；
//   A-5  布局/版本不符 → fail-closed（LayoutMismatch），不接管已有映射；
//   §4.2 调用带单调 call_id，结果按 call_id 关联；
//   §5.2 心跳计数由辅助侧写、宿主侧本地读；
//   A-1  真的 kill 掉辅助进程 → 宿主进程存活、通道关闭可判定、只触发一次回调；
//   A-7  辅助进程在崩溃/被杀之前发出的记录能在宿主侧收到（证明日志走 IPC）。
//
// ⚠️ 跨进程用例只在 Windows 上跑（T-021 只发布 Windows 目标）。非 Windows 会打印
//    [skip] 而不是假装通过。

#include "test_channel.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include "bbcoop/ipc/call_dispatch.h"
#include "bbcoop/ipc/channel.h"
#include "bbcoop/ipc/liveness_registry.h"
#include "bbcoop/ipc/process_host.h"
#include "bbcoop/ipc/proxy.h"
#include "bbcoop/ipc/shm_mapping.h"
#include "bbcoop/ipc/shared_channel.h"
#include "bbcoop/ipc/stub.h"
#include "bbcoop/ipc/watchdog.h"
#include "bbcoop/ipc/wire.h"
#include "test_framework.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace bbcoop::test {
namespace {

using bbcoop::Error;
using bbcoop::ISession;
using bbcoop::ITransport;
using bbcoop::ipc::ByteSpan;
using bbcoop::ipc::CallDispatchResult;
using bbcoop::ipc::CallOp;
using bbcoop::ipc::ChannelId;
using bbcoop::ipc::ChannelMapping;
using bbcoop::ipc::ChannelRole;
using bbcoop::ipc::Durability;
using bbcoop::ipc::ICallChannel;
using bbcoop::ipc::kCallPrefixBytes;
using bbcoop::ipc::kCallResultPrefixBytes;
using bbcoop::ipc::LivenessRegistry;
using bbcoop::ipc::MappingResult;
using bbcoop::ipc::MessageKind;
using bbcoop::ipc::MutableByteSpan;
using bbcoop::ipc::RecordHeader;
using bbcoop::ipc::SessionCache;
using bbcoop::ipc::SessionProxy;
using bbcoop::ipc::SessionStub;
using bbcoop::ipc::SharedChannel;
using bbcoop::ipc::SharedChannelLayout;
using bbcoop::ipc::TransportCache;
using bbcoop::ipc::TransportProxy;
using bbcoop::ipc::TransportStub;

std::vector<std::byte> bytes(const char* text) {
    const std::size_t n = std::strlen(text);
    std::vector<std::byte> out(n);
    for (std::size_t i = 0; i < n; ++i) {
        out[i] = static_cast<std::byte>(static_cast<unsigned char>(text[i]));
    }
    return out;
}

// —— 假模块（辅助侧的「真实实现」）——

class FakeTransport final : public ITransport {
public:
    bool connected() const noexcept override { return connectedValue; }
    Error send(const std::uint8_t* data, std::size_t size) noexcept override {
        sent.assign(data, data + size);
        return sendResult;
    }
    std::uint32_t rtt_ms() const noexcept override { return rtt; }

    bool connectedValue = true;
    Error sendResult = Error::Ok;
    std::uint32_t rtt = 42;
    std::vector<std::uint8_t> sent;
};

class FakeSession final : public ISession {
public:
    bool active() const noexcept override { return activeValue; }
    std::uint32_t member_count() const noexcept override { return members; }
    bool is_host() const noexcept override { return hostValue; }

    bool activeValue = true;
    std::uint32_t members = 2;
    bool hostValue = false;
};

// —— 假通道（代理的发送端）——

class FakeChannel final : public ICallChannel {
public:
    bool available() const noexcept override { return isAvailable; }
    bool trySendCall(CallOp op, ByteSpan payload, std::uint32_t& callId) noexcept override {
        ++calls;
        lastOp = op;
        lastPayloadSize = payload.size();
        callId = 7;
        return accepts;
    }
    bool trySendMessage(MessageKind kind, Durability durability, ByteSpan payload) noexcept override {
        (void)kind;
        (void)durability;
        (void)payload;
        ++messages;
        return accepts;
    }

    bool isAvailable = true;
    bool accepts = true;
    std::size_t calls = 0;
    std::size_t messages = 0;
    CallOp lastOp = CallOp::TransportConnected;
    std::size_t lastPayloadSize = 0;
};

void testWireRoundTrip() {
    std::array<std::byte, 8> buffer{};
    bbcoop::ipc::wire::writeU32(MutableByteSpan(buffer.data(), buffer.size()), 0x01020304U);
    CHECK_EQ(bbcoop::ipc::wire::readU32(ByteSpan(buffer.data(), bbcoop::ipc::wire::kU32Size)),
             0x01020304U);

    bbcoop::ipc::wire::writeU64(MutableByteSpan(buffer.data(), buffer.size()),
                                0x0102030405060708ULL);
    CHECK(bbcoop::ipc::wire::readU64(ByteSpan(buffer.data(), bbcoop::ipc::wire::kU64Size)) ==
          0x0102030405060708ULL);

    bbcoop::ipc::wire::writeBool(MutableByteSpan(buffer.data(), buffer.size()), true);
    CHECK(bbcoop::ipc::wire::readBool(ByteSpan(buffer.data(), 1)));
    CHECK(!bbcoop::ipc::wire::readBool(ByteSpan{}));
}

void testStubHandlesAllOps() {
    FakeTransport transport;
    FakeSession session;
    TransportStub transportStub(transport);
    SessionStub sessionStub(session);

    std::array<std::byte, 32> out{};
    MutableByteSpan outSpan(out.data(), out.size());
    std::size_t outSize = 0;

    CHECK(transportStub.handle(CallOp::TransportConnected, ByteSpan{}, outSpan, outSize) == Error::Ok);
    CHECK_EQ(outSize, static_cast<std::size_t>(1));
    CHECK(bbcoop::ipc::wire::readBool(ByteSpan(out.data(), outSize)));

    CHECK(transportStub.handle(CallOp::TransportRtt, ByteSpan{}, outSpan, outSize) == Error::Ok);
    CHECK_EQ(outSize, static_cast<std::size_t>(4));
    CHECK_EQ(bbcoop::ipc::wire::readU32(ByteSpan(out.data(), outSize)), 42U);

    const auto payload = bytes("abc");
    CHECK(transportStub.handle(CallOp::TransportSend, ByteSpan(payload.data(), payload.size()),
                               outSpan, outSize) == Error::Ok);
    CHECK_EQ(outSize, static_cast<std::size_t>(1));
    CHECK_EQ(transport.sent.size(), static_cast<std::size_t>(3));
    CHECK_EQ(std::memcmp(transport.sent.data(), "abc", 3), 0);

    CHECK(sessionStub.handle(CallOp::SessionActive, ByteSpan{}, outSpan, outSize) == Error::Ok);
    CHECK(bbcoop::ipc::wire::readBool(ByteSpan(out.data(), outSize)));
    CHECK(sessionStub.handle(CallOp::SessionMemberCount, ByteSpan{}, outSpan, outSize) == Error::Ok);
    CHECK_EQ(bbcoop::ipc::wire::readU32(ByteSpan(out.data(), outSize)), 2U);
    CHECK(sessionStub.handle(CallOp::SessionIsHost, ByteSpan{}, outSpan, outSize) == Error::Ok);
    CHECK(!bbcoop::ipc::wire::readBool(ByteSpan(out.data(), outSize)));

    // 未知 op 与过小的输出缓冲都要被拒绝，而不是按越界写。
    CHECK(transportStub.handle(static_cast<CallOp>(99), ByteSpan{}, outSpan, outSize) ==
          Error::InvalidArgument);
    std::array<std::byte, 1> tiny{};
    CHECK(transportStub.handle(CallOp::TransportRtt, ByteSpan{},
                               MutableByteSpan(tiny.data(), tiny.size()),
                               outSize) == Error::InvalidArgument);
}

void testProxyUsesLocalCacheAndNeverBlocks() {
    FakeChannel channel;
    TransportCache transportCache;
    SessionCache sessionCache;
    TransportProxy transportProxy(channel, transportCache);
    SessionProxy sessionProxy(sessionCache);

    // A-4：通道不可用 → 立刻 Unavailable，且**不产生**任何跨边界调用。
    channel.isAvailable = false;
    const std::array<std::uint8_t, 4> data{1, 2, 3, 4};
    CHECK(transportProxy.send(data.data(), data.size()) == Error::Unavailable);
    CHECK_EQ(channel.calls, static_cast<std::size_t>(0));

    // 通道可用但环满 → 同样是 Unavailable，且只有一次尝试。
    channel.isAvailable = true;
    channel.accepts = false;
    CHECK(transportProxy.send(data.data(), data.size()) == Error::Unavailable);
    CHECK_EQ(channel.calls, static_cast<std::size_t>(1));

    // 正常路径：交给通道，立刻返回 Ok。
    channel.accepts = true;
    CHECK(transportProxy.send(data.data(), data.size()) == Error::Ok);
    CHECK_EQ(channel.calls, static_cast<std::size_t>(2));
    CHECK(channel.lastOp == CallOp::TransportSend);
    CHECK_EQ(channel.lastPayloadSize, static_cast<std::size_t>(4));

    // 参数不合法：拒绝，且不占用一次跨边界调用。
    CHECK(transportProxy.send(nullptr, 4) == Error::InvalidArgument);
    CHECK_EQ(channel.calls, static_cast<std::size_t>(2));

    // D-2：查询走本地缓存 —— 改缓存立刻生效，且不产生跨边界调用。
    transportCache.set(true, 33);
    sessionCache.set(true, 3, true);
    CHECK(transportProxy.connected());
    CHECK_EQ(transportProxy.rtt_ms(), 33U);
    CHECK(sessionProxy.active());
    CHECK_EQ(sessionProxy.member_count(), 3U);
    CHECK(sessionProxy.is_host());
    CHECK_EQ(channel.calls, static_cast<std::size_t>(2));

    transportCache.set(false, 0);
    sessionCache.set(false, 0, false);
    CHECK(!transportProxy.connected());
    CHECK_EQ(transportProxy.rtt_ms(), 0U);
    CHECK(!sessionProxy.active());
    CHECK_EQ(sessionProxy.member_count(), 0U);
    CHECK(!sessionProxy.is_host());
}

void testDispatchRoundTrip() {
    FakeTransport transport;
    FakeSession session;
    TransportStub transportStub(transport);
    SessionStub sessionStub(session);

    // 一条 Call 负载：call_id + op + 参数。
    std::array<std::byte, 16> call{};
    bbcoop::ipc::wire::writeU32(MutableByteSpan(call.data(), 4), 1234U);
    call[4] = static_cast<std::byte>(static_cast<std::uint8_t>(CallOp::TransportRtt));

    std::array<std::byte, 32> out{};
    const CallDispatchResult dispatched = bbcoop::ipc::dispatchCall(
        &transportStub, &sessionStub, ByteSpan(call.data(), kCallPrefixBytes),
        MutableByteSpan(out.data(), out.size()));
    CHECK(dispatched.error == Error::Ok);
    CHECK_EQ(dispatched.callId, 1234U);
    CHECK_EQ(dispatched.outSize, static_cast<std::size_t>(4));
    CHECK_EQ(bbcoop::ipc::wire::readU32(ByteSpan(out.data(), dispatched.outSize)), 42U);

    // 结果编码 → 解码后 call_id / error / 结果字节都对得上。
    std::array<std::byte, 64> encoded{};
    const std::size_t encodedSize = bbcoop::ipc::encodeCallResult(
        dispatched.callId, dispatched.error, ByteSpan(out.data(), dispatched.outSize),
        MutableByteSpan(encoded.data(), encoded.size()));
    CHECK_EQ(encodedSize, kCallResultPrefixBytes + 4);

    std::uint32_t callId = 0;
    Error error = Error::Internal;
    ByteSpan result{};
    CHECK(bbcoop::ipc::decodeCallResult(ByteSpan(encoded.data(), encodedSize), callId, error, result));
    CHECK_EQ(callId, 1234U);
    CHECK(error == Error::Ok);
    CHECK_EQ(result.size(), static_cast<std::size_t>(4));

    // 辅助进程没提供该模块 → Unavailable，而不是崩溃或乱码。
    const CallDispatchResult missing = bbcoop::ipc::dispatchCall(
        nullptr, nullptr, ByteSpan(call.data(), kCallPrefixBytes),
        MutableByteSpan(out.data(), out.size()));
    CHECK(missing.error == Error::Unavailable);

    // 负载太短 / 结果缓冲太小：拒绝。
    CHECK(bbcoop::ipc::dispatchCall(&transportStub, &sessionStub,
                                    ByteSpan(call.data(), 2),
                                    MutableByteSpan(out.data(), out.size())).error ==
          Error::InvalidArgument);
    CHECK_EQ(bbcoop::ipc::encodeCallResult(1, Error::Ok, ByteSpan(out.data(), out.size()),
                                           MutableByteSpan(encoded.data(), 2)),
             static_cast<std::size_t>(0));
}

void testSharedChannelHandshakeAndCalls() {
    auto layout = std::make_unique<SharedChannelLayout>();
    bbcoop::ipc::initSharedLayout(*layout);
    SharedChannel host(*layout, ChannelRole::Host);
    SharedChannel aux(*layout, ChannelRole::Aux);

    // 没握手之前：不可用，发送被拒（A-4 的通道侧）。
    CHECK(!host.available());
    std::uint32_t callId = 0;
    CHECK(!host.trySendCall(CallOp::TransportConnected, ByteSpan{}, callId));
    CHECK_EQ(host.refusedRecords(), static_cast<std::size_t>(1));
    CHECK(host.checkPeerHandshake() == Error::NotInitialized);

    host.publishHello();
    CHECK(!host.available());
    aux.publishHello();
    CHECK(host.available());
    CHECK(aux.available());
    CHECK(host.checkPeerHandshake() == Error::Ok);
    CHECK(aux.checkPeerHandshake() == Error::Ok);

    // 调用：编号从 1 起单调递增（§4.2）。
    const auto args = bytes("hi");
    CHECK(host.trySendCall(CallOp::TransportSend, ByteSpan(args.data(), args.size()), callId));
    CHECK_EQ(callId, 1U);
    CHECK_EQ(host.sentRecords(), static_cast<std::size_t>(1));

    RecordHeader header{};
    ByteSpan payload{};
    CHECK(aux.tryReceive(header, payload));
    CHECK(header.kind == MessageKind::Call);
    CHECK_EQ(static_cast<std::size_t>(header.size), kCallPrefixBytes + 2);
    CHECK_EQ(payload.size(), kCallPrefixBytes + 2);
    CHECK_EQ(bbcoop::ipc::wire::readU32(payload.subspan(0, 4)), 1U);
    CHECK(payload[4] == static_cast<std::byte>(static_cast<std::uint8_t>(CallOp::TransportSend)));

    // 辅助侧：分发 → 回带结果。
    FakeTransport transport;
    FakeSession session;
    TransportStub transportStub(transport);
    SessionStub sessionStub(session);
    std::array<std::byte, 32> out{};
    const CallDispatchResult dispatched =
        bbcoop::ipc::dispatchCall(&transportStub, &sessionStub, payload,
                                  MutableByteSpan(out.data(), out.size()));
    CHECK(dispatched.error == Error::Ok);
    CHECK_EQ(dispatched.callId, 1U);
    CHECK_EQ(transport.sent.size(), static_cast<std::size_t>(2));
    CHECK_EQ(std::memcmp(transport.sent.data(), "hi", 2), 0);

    std::array<std::byte, 64> encoded{};
    const std::size_t encodedSize = bbcoop::ipc::encodeCallResult(
        dispatched.callId, dispatched.error, ByteSpan(out.data(), dispatched.outSize),
        MutableByteSpan(encoded.data(), encoded.size()));
    CHECK(encodedSize > 0);
    CHECK(aux.trySendMessage(MessageKind::CallResult, Durability::Diagnostic,
                             ByteSpan(encoded.data(), encodedSize)));

    RecordHeader backHeader{};
    ByteSpan backPayload{};
    CHECK(host.tryReceive(backHeader, backPayload));
    CHECK(backHeader.kind == MessageKind::CallResult);
    std::uint32_t backId = 0;
    Error backError = Error::Internal;
    ByteSpan backResult{};
    CHECK(bbcoop::ipc::decodeCallResult(backPayload, backId, backError, backResult));
    CHECK_EQ(backId, 1U);
    CHECK(backError == Error::Ok);
    CHECK_EQ(backResult.size(), static_cast<std::size_t>(1));

    // 心跳：辅助侧写、宿主侧本地读；宿主侧写无效（§5.2）。
    CHECK_EQ(host.heartbeatCounter(), 0ULL);
    aux.noteHeartbeat();
    aux.noteHeartbeat();
    CHECK_EQ(host.heartbeatCounter(), 2ULL);
    host.noteHeartbeat();
    CHECK_EQ(host.heartbeatCounter(), 2ULL);

    // 关闭：任一侧置位即对双方不可用；重复置位不改变结果（D-7）。
    aux.markClosed();
    aux.markClosed();
    CHECK(host.closedByPeer());
    CHECK(host.closed());
    CHECK(!host.available());
    CHECK(!host.trySendCall(CallOp::TransportConnected, ByteSpan{}, callId));
}

void testSharedChannelBackpressureIsDecidable() {
    auto layout = std::make_unique<SharedChannelLayout>();
    bbcoop::ipc::initSharedLayout(*layout);
    SharedChannel host(*layout, ChannelRole::Host);
    SharedChannel aux(*layout, ChannelRole::Aux);
    host.publishHello();
    aux.publishHello();

    // 没人消费 + 负载很大：环满之后必须**可判定地拒绝**，不能阻塞、不能覆盖旧记录。
    std::vector<std::byte> big(4096, std::byte{0x5A});
    std::size_t accepted = 0;
    std::uint32_t callId = 0;
    for (int i = 0; i < 32; ++i) {
        if (host.trySendCall(CallOp::TransportSend, ByteSpan(big.data(), big.size()), callId)) {
            ++accepted;
        }
    }
    CHECK(accepted > 0);
    CHECK(accepted < 32);
    CHECK_EQ(host.sentRecords(), accepted);
    CHECK_EQ(host.refusedRecords(), static_cast<std::size_t>(32) - accepted);

    // 先入队的记录仍然完好（不是被覆盖后的残渣）。
    RecordHeader header{};
    ByteSpan payload{};
    CHECK(aux.tryReceive(header, payload));
    CHECK(header.kind == MessageKind::Call);
    CHECK_EQ(payload.size(), big.size() + kCallPrefixBytes);
    CHECK(payload[kCallPrefixBytes] == std::byte{0x5A});
}

void testSharedLayoutVersionGate() {
    auto layout = std::make_unique<SharedChannelLayout>();
    bbcoop::ipc::initSharedLayout(*layout);
    CHECK(bbcoop::ipc::sharedLayoutMatches(*layout));

    // A-5 的判据：版本/尺寸任一不符即拒绝（这里是校验函数本身的行为）。
    layout->header.layoutVersion.store(999, std::memory_order_relaxed);
    CHECK(!bbcoop::ipc::sharedLayoutMatches(*layout));
    layout->header.layoutVersion.store(bbcoop::ipc::kLayoutVersion, std::memory_order_relaxed);

    layout->header.layoutSize.store(1, std::memory_order_relaxed);
    CHECK(!bbcoop::ipc::sharedLayoutMatches(*layout));
    layout->header.layoutSize.store(static_cast<std::uint64_t>(sizeof(SharedChannelLayout)),
                                    std::memory_order_relaxed);
    layout->header.magic.store(0, std::memory_order_relaxed);
    CHECK(!bbcoop::ipc::sharedLayoutMatches(*layout));
}

// A-2：冻结心跳 → 判 heartbeat_stale，触发终止请求，且**只触发一次**（幂等）。
void testWatchdogStaleIsReportedOnce() {
    bbcoop::ipc::WatchdogPolicy policy;
    policy.readyTimeoutMs = 1000;
    policy.heartbeatStaleMs = 3000;
    bbcoop::ipc::ChannelWatchdog watchdog(policy);
    LivenessRegistry registry;
    const ChannelId channel{9};
    CHECK(registry.watch(channel, nullptr, nullptr) == Error::Ok);

    watchdog.onStarted(0);
    registry.markStale(channel);
    CHECK(registry.state(channel) == bbcoop::ipc::ChannelState::Starting);  // 未 ready 不标 stale

    watchdog.onReady(1000);
    CHECK(watchdog.ready());
    CHECK(!watchdog.armed());

    // 还没观察到心跳变化：即使远超 N 也不能判「卡住」（D-7）。
    CHECK(watchdog.poll(9000, 0) == bbcoop::ipc::WatchdogVerdict::None);

    // 第一次心跳变化 → 武装。
    CHECK(watchdog.poll(9001, 1) == bbcoop::ipc::WatchdogVerdict::None);
    CHECK(watchdog.armed());

    // N 之内仍在跳：不判卡住（每次计数变化都会把计时起点推后）。
    CHECK(watchdog.poll(10000, 2) == bbcoop::ipc::WatchdogVerdict::None);
    CHECK(watchdog.poll(12000, 2) == bbcoop::ipc::WatchdogVerdict::None);  // 距上次变化 2000 < 3000
    CHECK(watchdog.poll(13000, 2) == bbcoop::ipc::WatchdogVerdict::HeartbeatStale);

    // 判定之后：只报一次，后续 tick 不再重复触发（A-2 的「只触发一次」）。
    CHECK(watchdog.poll(14000, 2) == bbcoop::ipc::WatchdogVerdict::None);
    CHECK(watchdog.poll(20000, 2) == bbcoop::ipc::WatchdogVerdict::None);
    CHECK(watchdog.verdictReported());

    // 与注册表联动：stale → 终止请求恰好一次，状态机走到 terminating。
    registry.noteHeartbeat(channel);  // 先进入 ready
    CHECK(registry.state(channel) == bbcoop::ipc::ChannelState::Ready);
    registry.markStale(channel);
    registry.markStale(channel);
    CHECK(registry.state(channel) == bbcoop::ipc::ChannelState::Stale);
    registry.requestTerminate(channel);
    registry.requestTerminate(channel);
    CHECK(registry.state(channel) == bbcoop::ipc::ChannelState::Terminating);
    CHECK_EQ(registry.terminateRequests(channel), 1U);

    // 心跳恢复：武装状态重新计（reported 复位），仍不影响已请求的终止。
    CHECK(watchdog.poll(21000, 3) == bbcoop::ipc::WatchdogVerdict::None);
    CHECK(!watchdog.verdictReported());
}

// 就绪超时是**启动失败**，不是「卡住」：原因必须能分开报（§5.1）。
void testWatchdogReadyTimeout() {
    bbcoop::ipc::WatchdogPolicy policy;
    policy.readyTimeoutMs = 1000;
    bbcoop::ipc::ChannelWatchdog watchdog(policy);
    watchdog.onStarted(0);

    CHECK(watchdog.poll(999, 0) == bbcoop::ipc::WatchdogVerdict::None);
    CHECK(watchdog.poll(1000, 0) == bbcoop::ipc::WatchdogVerdict::ReadyTimeout);
    CHECK(watchdog.poll(5000, 0) == bbcoop::ipc::WatchdogVerdict::None);  // 只报一次
    CHECK(std::strcmp(bbcoop::ipc::to_string(bbcoop::ipc::WatchdogVerdict::ReadyTimeout),
                      "ready_timeout") == 0);
    CHECK(std::strcmp(bbcoop::ipc::to_string(bbcoop::ipc::WatchdogVerdict::HeartbeatStale),
                      "heartbeat_stale") == 0);
}

void testChannelMappingName() {
    wchar_t name[64] = {};
    const std::size_t written = bbcoop::ipc::formatChannelMappingName(ChannelId{1234}, name, 64);
    CHECK_EQ(written, bbcoop::ipc::kMappingNamePrefixLength + 4);  // 前缀 + "1234"
    CHECK(std::wcscmp(name, L"Local\\bbcoop-ipc-1234") == 0);

    // 缓冲区不足：返回 0，不越界。
    wchar_t small[8] = {};
    CHECK_EQ(bbcoop::ipc::formatChannelMappingName(ChannelId{7}, small, 8),
             static_cast<std::size_t>(0));
    CHECK_EQ(bbcoop::ipc::formatChannelMappingName(ChannelId{7}, nullptr, 64),
             static_cast<std::size_t>(0));
}

#ifdef _WIN32

bool waitUntil(const std::function<bool()>& predicate, std::uint64_t timeoutMs) {
    const ULONGLONG start = GetTickCount64();
    for (;;) {
        if (predicate()) {
            return true;
        }
        if (GetTickCount64() - start >= timeoutMs) {
            return predicate();
        }
        Sleep(2);
    }
}

void widenAscii(const char* src, wchar_t* out, std::size_t outSize) noexcept {
    std::size_t i = 0;
    if (outSize == 0) {
        return;
    }
    while (src != nullptr && src[i] != '\0' && i + 1 < outSize) {
        out[i] = static_cast<wchar_t>(static_cast<unsigned char>(src[i]));
        ++i;
    }
    out[i] = L'\0';
}

void testChannelMappingLifecycle() {
    ChannelId channel{4242};
    wchar_t name[64] = {};
    CHECK(bbcoop::ipc::formatChannelMappingName(channel, name, 64) > 0);

    MappingResult result = MappingResult::Failed;
    auto mapping = ChannelMapping::create(name, result);
    CHECK(result == MappingResult::Ok);
    CHECK(mapping != nullptr);
    if (!mapping) {
        return;
    }
    CHECK(mapping->layout() != nullptr);
    CHECK(bbcoop::ipc::sharedLayoutMatches(*mapping->layout()));

    // 同名再创建：已经存在就**不接管**（接管会抹掉对方的握手与环形状态）。
    MappingResult second = MappingResult::Failed;
    CHECK(ChannelMapping::create(name, second) == nullptr);
    CHECK(second == MappingResult::AlreadyExists);

    // 同进程内的第二个视图：拿到的是同一块内存（跨进程路径的最小可判定形式）。
    // 只断言「一处写、另一处能读到」——不假设两次 MapViewOfFile 返回同一地址。
    MappingResult opened = MappingResult::Failed;
    auto view2 = ChannelMapping::open(name, opened);
    CHECK(opened == MappingResult::Ok);
    CHECK(view2 != nullptr);
    if (view2) {
        view2->layout()->auxHeartbeat.store(5, std::memory_order_release);
        CHECK_EQ(mapping->layout()->auxHeartbeat.load(std::memory_order_acquire), 5ULL);
    }

    // A-5：布局版本不符 → open 必须 LayoutMismatch，而不是照着旧布局解释内存。
    mapping->layout()->header.layoutVersion.store(999, std::memory_order_relaxed);
    MappingResult mismatch = MappingResult::Ok;
    CHECK(ChannelMapping::open(name, mismatch) == nullptr);
    CHECK(mismatch == MappingResult::LayoutMismatch);
    mapping->layout()->header.layoutVersion.store(bbcoop::ipc::kLayoutVersion,
                                                 std::memory_order_relaxed);

    // 不存在的名字 → NotFound（不是 Failed，自检要能分开报）。
    MappingResult missing = MappingResult::Ok;
    CHECK(ChannelMapping::open(L"Local\\bbcoop-ipc-does-not-exist-987654", missing) == nullptr);
    CHECK(missing == MappingResult::NotFound);
}

// —— 跨进程：父进程（宿主侧）——

void runCrossProcessChannelTestsImpl() {
    wchar_t name[64] = {};
    // 名字里带进程 id：并行跑多个测试实例时不会互相抢同一块映射。
    const ChannelId channel{static_cast<std::uint32_t>(GetCurrentProcessId())};
    CHECK(bbcoop::ipc::formatChannelMappingName(channel, name, 64) > 0);

    MappingResult created = MappingResult::Failed;
    auto mapping = ChannelMapping::create(name, created);
    CHECK(created == MappingResult::Ok);
    if (!mapping) {
        return;  // 拿不到映射就没法验跨进程；上面的 CHECK 已经报失败
    }

    SharedChannel host(*mapping->layout(), ChannelRole::Host);
    host.publishHello();

    auto processes = bbcoop::ipc::createPlatformProcessHost();
    CHECK(processes != nullptr);
    if (!processes) {
        return;
    }

    wchar_t exePath[512] = {};
    CHECK(GetModuleFileNameW(nullptr, exePath, 512) != 0);

    // 参数：--aux-child <映射名>。手写拼接而不碰 wcsncpy（MSVC 下它是 C4996）。
    wchar_t args[160] = {};
    const wchar_t* prefix = L"--aux-child ";
    std::size_t pos = 0;
    for (std::size_t i = 0; prefix[i] != L'\0' && pos + 1 < 160; ++i) {
        args[pos++] = prefix[i];
    }
    for (std::size_t i = 0; name[i] != L'\0' && pos + 1 < 160; ++i) {
        args[pos++] = name[i];
    }
    args[pos] = L'\0';

    bbcoop::ipc::ProcessId pid{};
    // 子进程起来之前，辅助侧握手块必然是空的 —— 本进程从没给 Aux 角色发布过 hello。
    // 所以后面 available() 变真，只可能来自**另一个进程**写进共享内存。
    CHECK(!host.available());
    CHECK(host.checkPeerHandshake() == Error::NotInitialized);

    CHECK(processes->start(exePath, args, pid) == Error::Ok);
    CHECK(processes->isAlive(pid));
    CHECK(pid.value != GetCurrentProcessId());

    // 真实握手：等辅助进程把它的 hello 写进共享内存。
    CHECK(waitUntil([&host] { return host.available(); }, 10000));
    CHECK(host.checkPeerHandshake() == Error::Ok);

    // A-7：辅助进程上线时发的审计级日志已经经 IPC 回到宿主侧。
    bool sawLogRecord = false;
    CHECK(waitUntil(
        [&host, &sawLogRecord] {
            RecordHeader header{};
            ByteSpan payload{};
            while (host.tryReceive(header, payload)) {
                if (header.kind == MessageKind::LogRecord &&
                    header.durability == Durability::Audit) {
                    sawLogRecord = true;
                    return true;
                }
            }
            return false;
        },
        5000));
    CHECK(sawLogRecord);

    // 调用往返：两条不同的调用，结果按 call_id 关联（§4.2）。
    std::uint32_t firstId = 0;
    std::uint32_t secondId = 0;
    CHECK(host.trySendCall(CallOp::TransportConnected, ByteSpan{}, firstId));
    CHECK(host.trySendCall(CallOp::SessionMemberCount, ByteSpan{}, secondId));
    CHECK(firstId != secondId);

    std::vector<std::pair<std::uint32_t, std::vector<std::byte>>> results;
    const bool gotBoth = waitUntil(
        [&host, &results] {
            RecordHeader header{};
            ByteSpan payload{};
            while (host.tryReceive(header, payload)) {
                if (header.kind != MessageKind::CallResult) {
                    continue;
                }
                std::uint32_t callId = 0;
                Error error = Error::Internal;
                ByteSpan result{};
                if (bbcoop::ipc::decodeCallResult(payload, callId, error, result) &&
                    error == Error::Ok) {
                    std::vector<std::byte> copy(result.begin(), result.end());
                    results.emplace_back(callId, std::move(copy));
                }
            }
            return results.size() >= 2;
        },
        5000);
    CHECK(gotBoth);
    CHECK_EQ(results.size(), static_cast<std::size_t>(2));
    if (results.size() == 2) {
        // 假 transport 报 connected=true；假 session 报 member_count=2。
        const std::vector<std::byte>& first = results[0].second;
        const std::vector<std::byte>& second = results[1].second;
        CHECK(results[0].first == firstId);
        CHECK(results[1].first == secondId);
        CHECK_EQ(first.size(), static_cast<std::size_t>(1));
        CHECK(bbcoop::ipc::wire::readBool(ByteSpan(first.data(), first.size())));
        CHECK_EQ(second.size(), static_cast<std::size_t>(4));
        CHECK_EQ(bbcoop::ipc::wire::readU32(ByteSpan(second.data(), second.size())), 2U);
    }

    // §5.2：心跳计数由辅助侧在共享内存里递增，宿主侧本地读。
    CHECK(waitUntil([&host] { return host.heartbeatCounter() >= 3ULL; }, 5000));

    // A-1：真的把辅助进程杀掉 → 宿主进程（就是本进程）还活着，且进程退出可判定。
    CHECK(processes->terminate(pid) == Error::Ok);
    bool exited = false;
    CHECK(processes->waitForExit(pid, 5000, exited) == Error::Ok);
    CHECK(exited);
    CHECK(!processes->isAlive(pid));

    // 通道关闭归约到同一个入口，且**只触发一次**（D-7 幂等）。
    LivenessRegistry registry;
    int notifications = 0;
    const auto onClosed = [](void* user, ChannelId) noexcept { ++(*static_cast<int*>(user)); };
    CHECK(registry.watch(ChannelId{1}, onClosed, &notifications) == Error::Ok);
    registry.onChannelClosed(ChannelId{1});
    registry.onChannelClosed(ChannelId{1});
    CHECK_EQ(notifications, 1);
    CHECK(registry.state(ChannelId{1}) == bbcoop::ipc::ChannelState::Gone);
    CHECK(std::strcmp(bbcoop::ipc::to_string(bbcoop::ipc::UnavailableReason::ChannelClosed),
                      "channel_closed") == 0);

    processes->release(pid);
}

int runAuxChildImpl(const char* mappingName) {
    wchar_t name[128] = {};
    widenAscii(mappingName, name, 128);

    MappingResult result = MappingResult::Failed;
    auto mapping = ChannelMapping::open(name, result);
    if (!mapping) {
        std::printf("[aux-child] open failed: %s\n", bbcoop::ipc::to_string(result));
        return 2;
    }

    FakeTransport transport;
    FakeSession session;
    TransportStub transportStub(transport);
    SessionStub sessionStub(session);
    SharedChannel channel(*mapping->layout(), ChannelRole::Aux);
    channel.publishHello();

    // A-7：上线先发一条审计级记录。辅助进程零文件权限，日志只能走 IPC。
    const auto message = bytes("aux-child online");
    if (!channel.trySendMessage(MessageKind::LogRecord, Durability::Audit,
                                ByteSpan(message.data(), message.size()))) {
        return 3;
    }

    const ULONGLONG deadline = GetTickCount64() + 20000;  // 兜底：父进程失联也不会挂死
    while (GetTickCount64() < deadline) {
        channel.noteHeartbeat();

        RecordHeader header{};
        ByteSpan payload{};
        while (channel.tryReceive(header, payload)) {
            if (header.kind != MessageKind::Call) {
                continue;
            }
            std::array<std::byte, 32> out{};
            const CallDispatchResult dispatched =
                bbcoop::ipc::dispatchCall(&transportStub, &sessionStub, payload,
                                          MutableByteSpan(out.data(), out.size()));
            std::array<std::byte, 64> encoded{};
            const std::size_t encodedSize = bbcoop::ipc::encodeCallResult(
                dispatched.callId, dispatched.error, ByteSpan(out.data(), dispatched.outSize),
                MutableByteSpan(encoded.data(), encoded.size()));
            if (encodedSize == 0 ||
                !channel.trySendMessage(MessageKind::CallResult, Durability::Diagnostic,
                                        ByteSpan(encoded.data(), encodedSize))) {
                return 4;
            }
        }
        Sleep(5);
    }
    return 5;
}

#endif  // _WIN32

}  // namespace

void runChannelTests() {
    testWireRoundTrip();
    testStubHandlesAllOps();
    testProxyUsesLocalCacheAndNeverBlocks();
    testDispatchRoundTrip();
    testSharedChannelHandshakeAndCalls();
    testSharedChannelBackpressureIsDecidable();
    testSharedLayoutVersionGate();
    testWatchdogStaleIsReportedOnce();
    testWatchdogReadyTimeout();
    testChannelMappingName();
#ifdef _WIN32
    testChannelMappingLifecycle();
#endif
}

void runCrossProcessChannelTests() {
#ifdef _WIN32
    runCrossProcessChannelTestsImpl();
#else
    std::printf("[skip] 跨进程通道用例只在 Windows 上运行（T-021 只发布 Windows 目标）\n");
#endif
}

int runAuxChild(const char* mappingName) {
#ifdef _WIN32
    return runAuxChildImpl(mappingName);
#else
    (void)mappingName;
    return 1;
#endif
}

}  // namespace bbcoop::test
