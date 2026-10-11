// T-006 单元测试之三：**熔断**。
// owner: AI-00（T-006）。
//
// 覆盖任务书 §41.1 T-005 的验收「tick 超时熔断」，以及 07-kernel-seam §1.3 的两条规则：
//   * 进入不可用必须**幂等**（不重复计数、不重复记日志）；
//   * 进程外模块的不可用可以由外部事件设置（通道关闭 / 心跳停滞），
//     且「卡住」判定要**先观察到心跳变化**才武装。
//
// 阈值与周期都是**暂定**（policy.h 里注明），测试用注入的策略值，不用默认值 ——
// 这样改暂定数值不会让测试莫名其妙地红。

#include <memory>
#include <new>

#include "bbcoop/ipc/protocol.h"
#include "bbcoop/kernel/kernel.h"
#include "bbcoop/kernel/policy.h"
#include "fixture_modules.h"
#include "test_framework.h"

namespace {

using bbcoop::Error;
using bbcoop::Health;
using bbcoop::ipc::ChannelId;
using bbcoop::kernel::Kernel;
using bbcoop::kernel::KernelPolicy;
using bbcoop::kernel::ModuleSource;
using bbcoop::kernel::ModuleStatus;
using bbcoop::kernel::UnavailableReason;
using bbcoop::fixtures::FakeLiveness;
using bbcoop::fixtures::TickFixtureModule;
using bbcoop::fixtures::TickMode;

std::unique_ptr<TickFixtureModule> newTickFixture(const char* id) {
    return std::unique_ptr<TickFixtureModule>(new (std::nothrow) TickFixtureModule(id));
}

// 单次 tick 耗时超过阈值 → 判超时；连续达到阈值次数 → 熔断，且**只标记一次**。
void testTickTimeoutTripsTheCircuit() {
    KernelPolicy policy;
    policy.tickTimeoutMs = 1;  // 夹具每次慢 tick 睡 5ms（sleep_for 保证至少睡够）
    policy.failureThreshold = 3;
    Kernel kernel(policy);

    auto module = newTickFixture("mod.slow");
    module->mode = TickMode::StallThenOk;
    module->stallTicks = 3;
    module->stallMs = 5;
    TickFixtureModule* raw = module.get();
    CHECK_EQ(static_cast<int>(kernel.addModule(std::move(module), true)),
             static_cast<int>(Error::Ok));

    kernel.tick(1000);
    CHECK_EQ(raw->tickCalls, static_cast<std::uint32_t>(1));
    CHECK(!kernel.findModule("mod.slow")->circuitOpen);
    CHECK_EQ(kernel.findModule("mod.slow")->tickTimeouts, static_cast<std::uint32_t>(1));

    kernel.tick(1100);
    kernel.tick(1200);

    const ModuleStatus* status = kernel.findModule("mod.slow");
    CHECK(status->circuitOpen);
    CHECK(status->unavailableReason == UnavailableReason::TickTimeout);
    CHECK_EQ(status->tickTimeouts, static_cast<std::uint32_t>(3));
    CHECK_EQ(status->unavailableMarks, static_cast<std::uint32_t>(1));

    // 熔断之后**不再调用**它：tick 计数必须冻住（任务书 §41.1 T-005「停止调用它」）。
    const std::uint32_t frozen = raw->tickCalls;
    kernel.tick(1300);
    kernel.tick(1400);
    CHECK_EQ(raw->tickCalls, frozen);
}

// 连续 tick 失败达阈值 → 熔断，原因与超时可区分；一次成功即清零。
void testConsecutiveFailuresTripOnce() {
    KernelPolicy policy;
    policy.failureThreshold = 3;
    Kernel kernel(policy);

    auto module = newTickFixture("mod.flaky");
    module->mode = TickMode::PersistentFailure;
    TickFixtureModule* raw = module.get();
    CHECK_EQ(static_cast<int>(kernel.addModule(std::move(module), false)),
             static_cast<int>(Error::Ok));

    kernel.tick(1000);
    CHECK_EQ(kernel.findModule("mod.flaky")->consecutiveFailures,
             static_cast<std::uint32_t>(1));
    kernel.tick(1100);
    CHECK(!kernel.findModule("mod.flaky")->circuitOpen);
    kernel.tick(1200);

    const ModuleStatus* status = kernel.findModule("mod.flaky");
    CHECK(status->circuitOpen);
    CHECK(status->unavailableReason == UnavailableReason::TickFailures);
    CHECK(status->unavailableReason != UnavailableReason::TickTimeout);
    CHECK_EQ(status->unavailableMarks, static_cast<std::uint32_t>(1));
    CHECK_EQ(raw->tickCalls, static_cast<std::uint32_t>(3));

    // 再 tick 多少次都不会「重复标记」。
    kernel.tick(1300);
    kernel.tick(1400);
    CHECK_EQ(kernel.findModule("mod.flaky")->unavailableMarks,
             static_cast<std::uint32_t>(1));
    CHECK_EQ(raw->tickCalls, static_cast<std::uint32_t>(3));
}

// resetCircuit 的四种返回，以及**不该**被手工恢复的两种情况。
void testResetCircuitSemantics() {
    KernelPolicy policy;
    policy.failureThreshold = 1;
    Kernel kernel(policy);

    auto module = newTickFixture("mod.reset");
    module->mode = TickMode::PersistentFailure;
    CHECK_EQ(static_cast<int>(kernel.addModule(std::move(module), false)),
             static_cast<int>(Error::Ok));

    // 还没熔断 → Refused（不静默成功）。
    CHECK_EQ(static_cast<int>(kernel.resetCircuit("mod.reset")),
             static_cast<int>(Error::Refused));
    // 不存在的模块 → Unavailable；nullptr → InvalidArgument。
    CHECK_EQ(static_cast<int>(kernel.resetCircuit("mod.nope")),
             static_cast<int>(Error::Unavailable));
    CHECK_EQ(static_cast<int>(kernel.resetCircuit(nullptr)),
             static_cast<int>(Error::InvalidArgument));

    kernel.tick(1000);
    CHECK(kernel.findModule("mod.reset")->circuitOpen);
    CHECK_EQ(static_cast<int>(kernel.resetCircuit("mod.reset")), static_cast<int>(Error::Ok));
    CHECK(!kernel.findModule("mod.reset")->circuitOpen);
    CHECK(kernel.findModule("mod.reset")->unavailableReason == UnavailableReason::None);
    // 恢复之后又被 tick。
    kernel.tick(1100);
    CHECK_EQ(kernel.findModule("mod.reset")->tickCalls, static_cast<std::uint64_t>(2));
}

// 通道关闭：一条通道上的**全部** remote 模块一起标不可用，且幂等。
// 一个辅助进程装 6 个远程模块（06 §1），所以这里必须是复数语义。
void testChannelClosedMarksEveryRemoteModuleOnce() {
    Kernel kernel;
    FakeLiveness liveness;
    kernel.setLiveness(&liveness);

    const ChannelId channel0{7};
    const ChannelId channel1{8};
    CHECK_EQ(static_cast<int>(kernel.addRemote("mod.transport", channel0, true)),
             static_cast<int>(Error::Ok));
    CHECK_EQ(static_cast<int>(kernel.addRemote("mod.crypto", channel0, true)),
             static_cast<int>(Error::Ok));
    CHECK_EQ(static_cast<int>(kernel.addRemote("mod.relay", channel1, false)),
             static_cast<int>(Error::Ok));

    kernel.markRemoteReady(channel0, 0);
    kernel.markRemoteReady(channel1, 0);
    CHECK(kernel.findModule("mod.transport")->source == ModuleSource::Remote);
    CHECK(kernel.findModule("mod.transport")->health == Health::Ok);

    CHECK_EQ(liveness.closeChannel(channel0), static_cast<std::size_t>(1));
    kernel.onChannelClosed(channel0);

    const ModuleStatus* transport = kernel.findModule("mod.transport");
    const ModuleStatus* crypto = kernel.findModule("mod.crypto");
    const ModuleStatus* relay = kernel.findModule("mod.relay");
    CHECK(transport->unavailableReason == UnavailableReason::ChannelClosed);
    CHECK(crypto->unavailableReason == UnavailableReason::ChannelClosed);
    CHECK_EQ(transport->unavailableMarks, static_cast<std::uint32_t>(1));
    CHECK_EQ(crypto->unavailableMarks, static_cast<std::uint32_t>(1));
    // 另一条通道上的模块不受影响。
    CHECK(relay->unavailableReason == UnavailableReason::None);
    CHECK(relay->health == Health::Ok);

    // 幂等：重复回调不产生第二次迁移。
    kernel.onChannelClosed(channel0);
    kernel.onChannelClosed(channel0);
    CHECK_EQ(transport->unavailableMarks, static_cast<std::uint32_t>(1));
    CHECK_EQ(crypto->unavailableMarks, static_cast<std::uint32_t>(1));

    // 对端进程已经没了 → 不允许手工恢复（那会得到一个「显示正常、实际没人应答」的模块）。
    CHECK_EQ(static_cast<int>(kernel.resetCircuit("mod.transport")),
             static_cast<int>(Error::Refused));
}

// 心跳停滞：**先武装再判卡住**，判到之后只发**一次**终止请求。
void testHeartbeatStallArmsThenRequestsTerminationOnce() {
    Kernel kernel;
    FakeLiveness liveness;
    kernel.setLiveness(&liveness);

    const ChannelId channel{3};
    CHECK_EQ(static_cast<int>(kernel.addRemote("mod.session_core", channel, true)),
             static_cast<int>(Error::Ok));
    kernel.markRemoteReady(channel, 0);

    // 还没观察到心跳变化：即使远超 N 也不能判「卡住」（07 §1.3 规则 1）。
    const std::uint64_t staleMs = bbcoop::ipc::kHeartbeatStaleMs;
    kernel.tick(staleMs * 4);
    CHECK(!kernel.findModule("mod.session_core")->circuitOpen);

    // 心跳动两次 → 武装。
    liveness.advanceHeartbeat(channel);
    kernel.tick(staleMs * 4 + 10);
    liveness.advanceHeartbeat(channel);
    kernel.tick(staleMs * 4 + 20);
    CHECK(!kernel.findModule("mod.session_core")->circuitOpen);

    // 冻住心跳：N 之内不判，超过 N 判。
    const std::uint64_t base = staleMs * 4 + 20;
    kernel.tick(base + staleMs - 1);
    CHECK(!kernel.findModule("mod.session_core")->circuitOpen);

    kernel.tick(base + staleMs + 1);
    const ModuleStatus* status = kernel.findModule("mod.session_core");
    CHECK(status->circuitOpen);
    CHECK(status->unavailableReason == UnavailableReason::HeartbeatStale);
    CHECK_EQ(status->unavailableMarks, static_cast<std::uint32_t>(1));

    // **只请求一次**：Kernel 只发请求，执行在 T-021 的平台适配层（K-10）。
    CHECK_EQ(liveness.terminateRequests(channel), static_cast<std::uint32_t>(1));
    kernel.tick(base + staleMs * 5);
    CHECK_EQ(liveness.terminateRequests(channel), static_cast<std::uint32_t>(1));
    CHECK_EQ(kernel.findModule("mod.session_core")->unavailableMarks,
             static_cast<std::uint32_t>(1));
}

}  // namespace

int main() {
    testTickTimeoutTripsTheCircuit();
    testConsecutiveFailuresTripOnce();
    testResetCircuitSemantics();
    testChannelClosedMarksEveryRemoteModuleOnce();
    testHeartbeatStallArmsThenRequestsTerminationOnce();
    return bbcoop::test::finish();
}
