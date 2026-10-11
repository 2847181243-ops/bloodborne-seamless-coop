// T-005 Kernel 单元测试。
// owner: AI-00（T-005）。
//
// 覆盖 `docs/architecture/09-kernel-host.md` §9 的验收项：
//   A-1 init 失败不阻断其他模块        A-2 tick 超时熔断
//   A-3 连续失败熔断且只标记一次        A-4 通道关闭 → 该通道上**全部** remote 模块
//   A-5 心跳停滞 → 恰好一次终止请求      A-6 健康聚合
//   A-7 自检 JSON 的 overall 与 reason   A-8 事件总线语义
//   A-9 日志过滤与计数
//
// 这里**不注入段错误**，也不注入「tick 抛异常」：`IModule::tick` 是 noexcept 的，
// 派生类不能放宽异常规格，抛出即 std::terminate（见 kernel.h 的说明）。
// 可注入的只有「返回错误」与「耗时超过阈值」。

#include <cstring>
#include <new>
#include <memory>
#include <string>

#include "bbcoop/ipc/protocol.h"
#include "bbcoop/kernel/event_bus.h"
#include "bbcoop/kernel/kernel.h"
#include "bbcoop/kernel/policy.h"
#include "bbcoop/kernel/self_test.h"
#include "fixture_modules.h"
#include "test_framework.h"

namespace {

using bbcoop::Error;
using bbcoop::Health;
using bbcoop::ModuleContext;
using bbcoop::ModuleDescriptor;
using bbcoop::IModule;
using bbcoop::ipc::ChannelId;
using bbcoop::kernel::Kernel;
using bbcoop::kernel::KernelPolicy;
using bbcoop::kernel::ModuleStatus;
using bbcoop::kernel::ModuleTable;
using bbcoop::kernel::SelfTestOverall;
using bbcoop::kernel::UnavailableReason;
using bbcoop::fixtures::FakeLiveness;
using bbcoop::fixtures::InitFailModule;
using bbcoop::fixtures::NormalModule;
using bbcoop::fixtures::TickFixtureModule;
using bbcoop::fixtures::TickMode;

// 工厂函数：ModuleDescriptor 要的是 `IModule* (*)() noexcept`，
// 返回派生类指针的函数**不能**隐式转成它（返回类型不同），所以这里显式返回 IModule*。
// 需要拿到具体类型的用例直接把夹具 new 出来（见下面几个用例）。
IModule* createNormal() noexcept { return new (std::nothrow) NormalModule("mod.normal"); }
IModule* createNormalSecond() noexcept { return new (std::nothrow) NormalModule("mod.normal2"); }
IModule* createInitFail() noexcept {
    return new (std::nothrow) InitFailModule("mod.initfail", Error::Internal);
}

// 直接构造夹具的助手（用例需要观察夹具自己的计数时用它）。
std::unique_ptr<NormalModule> newNormal() {
    return std::unique_ptr<NormalModule>(new (std::nothrow) NormalModule("mod.normal"));
}
std::unique_ptr<InitFailModule> uniqueInitFail() {
    return std::unique_ptr<InitFailModule>(
        new (std::nothrow) InitFailModule("mod.initfail", Error::Internal));
}
std::unique_ptr<TickFixtureModule> newTickFixture() {
    return std::unique_ptr<TickFixtureModule>(
        new (std::nothrow) TickFixtureModule("mod.tickfixture"));
}

// 把「Kernel 里某个模块的状态」取出来做断言。
const ModuleStatus* statusOf(const Kernel& kernel, const char* id) {
    return kernel.findModule(id);
}

// ── A-1：init 失败不阻断其他模块 ────────────────────────────────────────────

void testInitFailureDoesNotBlockOthers() {
    const ModuleDescriptor descriptors[] = {
        {"mod.normal", createNormal},
        {"mod.initfail", createInitFail},
        {"mod.normal2", createNormalSecond},
    };
    const bool required[] = {false, false, false};
    ModuleTable table;
    table.loadable = descriptors;
    table.requiredFlags = required;
    table.loadableCount = 3;

    Kernel kernel;
    kernel.loadTable(table);

    CHECK_EQ(kernel.moduleCount(), static_cast<std::size_t>(3));

    const ModuleStatus* normal = statusOf(kernel, "mod.normal");
    const ModuleStatus* broken = statusOf(kernel, "mod.initfail");
    const ModuleStatus* second = statusOf(kernel, "mod.normal2");
    CHECK(normal != nullptr && broken != nullptr && second != nullptr);
    if (normal == nullptr || broken == nullptr || second == nullptr) {
        return;
    }

    // 两个正常的模块照常初始化；失败的只影响它自己。
    CHECK(normal->initialized);
    CHECK(second->initialized);
    CHECK(!broken->initialized);
    CHECK(broken->unavailableReason == UnavailableReason::InitFailed);
    CHECK(broken->health == Health::Unavailable);
    // 模块自报 Ok 也不能让状态变成 Ok —— Kernel 的覆盖规则必须赢。
    CHECK(normal->health == Health::Ok);

    // init 失败的模块**永不**被 tick（这是「停止调用它」在加载失败上的对应项）。
    kernel.tick(1000);
    kernel.tick(2000);
    CHECK_EQ(normal->tickCalls, static_cast<std::uint64_t>(2));
    CHECK_EQ(second->tickCalls, static_cast<std::uint64_t>(2));
}

// ── A-2 / A-3：超时与连续失败熔断 ───────────────────────────────────────────

void testTickTimeoutTripsTheCircuit() {
    KernelPolicy policy;
    policy.tickTimeoutMs = 1;   // 极低阈值：夹具每次慢 tick 睡 5ms
    policy.failureThreshold = 3;
    Kernel kernel(policy);

    auto module = newTickFixture();
    TickFixtureModule* raw = module.get();
    raw->mode = TickMode::StallThenOk;
    raw->stallTicks = 3;   // 前 3 次都超时 → 第 3 次熔断
    raw->stallMs = 5;

    CHECK_EQ(static_cast<int>(kernel.addModule(std::move(module), true)),
             static_cast<int>(Error::Ok));

    kernel.tick(1000);
    CHECK_EQ(raw->tickCalls, static_cast<std::uint32_t>(1));
    CHECK(!statusOf(kernel, "mod.tickfixture")->circuitOpen);

    kernel.tick(1100);
    kernel.tick(1200);

    const ModuleStatus* status = statusOf(kernel, "mod.tickfixture");
    CHECK(status->circuitOpen);
    CHECK(status->unavailableReason == UnavailableReason::TickTimeout);
    CHECK_EQ(status->tickTimeouts, static_cast<std::uint32_t>(3));
    CHECK_EQ(status->unavailableMarks, static_cast<std::uint32_t>(1));

    // 熔断之后**不再调用**它：tick 计数必须冻住。
    const std::uint32_t frozen = raw->tickCalls;
    kernel.tick(1300);
    kernel.tick(1400);
    CHECK_EQ(raw->tickCalls, frozen);
}

void testConsecutiveFailuresTripOnce() {
    KernelPolicy policy;
    policy.failureThreshold = 3;
    Kernel kernel(policy);

    auto module = newTickFixture();
    TickFixtureModule* raw = module.get();
    raw->mode = TickMode::PersistentFailure;
    CHECK_EQ(static_cast<int>(kernel.addModule(std::move(module), false)),
             static_cast<int>(Error::Ok));

    kernel.tick(1000);
    CHECK_EQ(const_cast<ModuleStatus*>(statusOf(kernel, "mod.tickfixture"))->consecutiveFailures,
             static_cast<std::uint32_t>(1));
    kernel.tick(1100);
    CHECK(!statusOf(kernel, "mod.tickfixture")->circuitOpen);
    kernel.tick(1200);

    const ModuleStatus* status = statusOf(kernel, "mod.tickfixture");
    CHECK(status->circuitOpen);
    CHECK(status->unavailableReason == UnavailableReason::TickFailures);
    CHECK_EQ(status->unavailableMarks, static_cast<std::uint32_t>(1));
    CHECK_EQ(raw->tickCalls, static_cast<std::uint32_t>(3));

    // 再 tick 多少次都不会「重复标记」。
    kernel.tick(1300);
    kernel.tick(1400);
    CHECK_EQ(statusOf(kernel, "mod.tickfixture")->unavailableMarks,
             static_cast<std::uint32_t>(1));
    CHECK_EQ(raw->tickCalls, static_cast<std::uint32_t>(3));
}

// ── A-4：通道关闭 → 该通道上的**全部** remote 模块 ──────────────────────────

void testChannelClosedMarksEveryModuleOnThatChannel() {
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

    // 一个 aux 进程塞 6 个远程模块（06 §1 / 08-ipc §3）：关一条通道要标掉它上面的**每一个**。
    kernel.markRemoteReady(channel0, 0);
    kernel.markRemoteReady(channel1, 0);
    CHECK(statusOf(kernel, "mod.transport")->initialized);
    CHECK(statusOf(kernel, "mod.transport")->health == Health::Ok);
    CHECK(statusOf(kernel, "mod.transport")->source ==
          bbcoop::kernel::ModuleSource::Remote);

    // 手工触发通道关闭（假实现只回调一次，和真实实现一致）。
    CHECK_EQ(liveness.closeChannel(channel0), static_cast<std::size_t>(1));
    kernel.onChannelClosed(channel0);

    const ModuleStatus* transport = statusOf(kernel, "mod.transport");
    const ModuleStatus* crypto = statusOf(kernel, "mod.crypto");
    const ModuleStatus* relay = statusOf(kernel, "mod.relay");
    CHECK(transport->unavailableReason == UnavailableReason::ChannelClosed);
    CHECK(crypto->unavailableReason == UnavailableReason::ChannelClosed);
    CHECK(!transport->initialized || transport->circuitOpen);
    CHECK_EQ(transport->unavailableMarks, static_cast<std::uint32_t>(1));
    CHECK_EQ(crypto->unavailableMarks, static_cast<std::uint32_t>(1));
    // 另一条通道上的模块不受影响。
    CHECK(relay->unavailableReason == UnavailableReason::None);
    CHECK(relay->health == Health::Ok);

    // 幂等：重复回调不产生第二次迁移（07 §1.3 规则 2）。
    kernel.onChannelClosed(channel0);
    kernel.onChannelClosed(channel0);
    CHECK_EQ(transport->unavailableMarks, static_cast<std::uint32_t>(1));
    CHECK_EQ(crypto->unavailableMarks, static_cast<std::uint32_t>(1));

    // 通道没了就不能「手工恢复」—— 那会得到一个显示正常、实际没人应答的模块。
    CHECK_EQ(static_cast<int>(kernel.resetCircuit("mod.transport")),
             static_cast<int>(Error::Refused));
}

// ── A-5：心跳停滞 → 恰好一次终止请求 ────────────────────────────────────────

void testHeartbeatStallRequestsTerminationOnce() {
    Kernel kernel;
    FakeLiveness liveness;
    kernel.setLiveness(&liveness);

    const ChannelId channel{3};
    CHECK_EQ(static_cast<int>(kernel.addRemote("mod.session_core", channel, true)),
             static_cast<int>(Error::Ok));
    kernel.markRemoteReady(channel, 0);

    // 心跳先动两次：看门狗要**观察到第一次变化**之后才武装（07 §1.3 规则 1）。
    liveness.advanceHeartbeat(channel);
    kernel.tick(100);
    CHECK(!statusOf(kernel, "mod.session_core")->circuitOpen);

    liveness.advanceHeartbeat(channel);
    kernel.tick(200);
    CHECK(!statusOf(kernel, "mod.session_core")->circuitOpen);

    // 冻住心跳：N 之内不判卡住（N = ipc 的 kHeartbeatStaleMs，暂定 3000ms）。
    const std::uint64_t staleMs = bbcoop::ipc::kHeartbeatStaleMs;
    kernel.tick(200 + staleMs - 1);
    CHECK(!statusOf(kernel, "mod.session_core")->circuitOpen);

    kernel.tick(200 + staleMs + 1);
    const ModuleStatus* status = statusOf(kernel, "mod.session_core");
    CHECK(status->circuitOpen);
    CHECK(status->unavailableReason == UnavailableReason::HeartbeatStale);
    CHECK_EQ(status->unavailableMarks, static_cast<std::uint32_t>(1));

    // **只请求一次**：Kernel 只发请求，执行在 T-021 的平台适配层（K-10）。
    CHECK_EQ(liveness.terminateRequests(channel), static_cast<std::uint32_t>(1));
    kernel.tick(200 + staleMs * 3);
    CHECK_EQ(liveness.terminateRequests(channel), static_cast<std::uint32_t>(1));
    CHECK_EQ(statusOf(kernel, "mod.session_core")->unavailableMarks,
             static_cast<std::uint32_t>(1));
}

// ── A-6：健康聚合 ───────────────────────────────────────────────────────────

void testHealthAggregation() {
    Kernel kernel;
    auto good = newNormal();
    good->reportedHealth = Health::Degraded;  // 模块自报「部分能力不可用」
    CHECK_EQ(static_cast<int>(kernel.addModule(std::move(good), false)),
             static_cast<int>(Error::Ok));

    auto bad = uniqueInitFail();
    CHECK_EQ(static_cast<int>(kernel.addModule(std::move(bad), true)),
             static_cast<int>(Error::Ok));

    // 模块自报的健康要经过一次巡检才进聚合（tick 里按周期做，这里手工触发一次）。
    kernel.pollHealth();

    const bbcoop::kernel::HealthSummary summary = kernel.summarizeHealth();
    CHECK_EQ(summary.total, static_cast<std::size_t>(2));
    CHECK_EQ(summary.ok, static_cast<std::size_t>(0));
    CHECK_EQ(summary.degraded, static_cast<std::size_t>(1));
    CHECK_EQ(summary.unavailable, static_cast<std::size_t>(1));
    // 聚合不能只看计数：最差状态必须是最差的那个。
    CHECK(summary.worst == Health::Unavailable);
}

// ── A-7：自检 JSON ──────────────────────────────────────────────────────────

void testSelfTestJsonCarriesSourceAndReason() {
    Kernel kernel;
    auto good = newNormal();
    CHECK_EQ(static_cast<int>(kernel.addModule(std::move(good), false)),
             static_cast<int>(Error::Ok));
    auto bad = uniqueInitFail();
    CHECK_EQ(static_cast<int>(kernel.addModule(std::move(bad), true)),
             static_cast<int>(Error::Ok));

    const ChannelId channel{5};
    CHECK_EQ(static_cast<int>(kernel.addRemote("mod.relay", channel, true)),
             static_cast<int>(Error::Ok));

    // required 的模块失败 → overall = fail（任务书 §41.1 T-014 的判据）。
    const bbcoop::kernel::SelfTestReport report = kernel.selfTestReport();
    CHECK(report.overall == SelfTestOverall::Fail);

    const std::string json = bbcoop::kernel::renderSelfTestJson(report, "0.1.0");
    CHECK(json.find("\"overall\": \"fail\"") != std::string::npos);
    CHECK(json.find("\"version\": \"0.1.0\"") != std::string::npos);
    CHECK(json.find("\"interface_version\": ") != std::string::npos);
    // 每种来源与每种原因都要看得见（07-kernel-seam §1.5）。
    CHECK(json.find("\"source\": \"local\"") != std::string::npos);
    CHECK(json.find("\"source\": \"remote\"") != std::string::npos);
    CHECK(json.find("\"unavailable_reason\": \"init_failed\"") != std::string::npos);
    // 没有失败时必须是 JSON 的 null，而不是字符串 "none"。
    CHECK(json.find("\"unavailable_reason\": null") != std::string::npos);
    CHECK(json.find("\"none\"") == std::string::npos);
}

void testSelfTestOverallRules() {
    // 一个模块都没有 → degraded（不是 pass）：宿主活着但没有任何能力。
    // 这一条是防「真空通过」的：空注册表若算 pass，Gate 6 会在框架期恒真。
    {
        Kernel kernel;
        CHECK(kernel.selfTestReport().overall == SelfTestOverall::Degraded);
    }
    // 只有非 required 的模块失败 → degraded。
    {
        Kernel kernel;
        auto bad = uniqueInitFail();
        CHECK_EQ(static_cast<int>(kernel.addModule(std::move(bad), false)),
                 static_cast<int>(Error::Ok));
        CHECK(kernel.selfTestReport().overall == SelfTestOverall::Degraded);
    }
    // 全部正常 → pass。
    {
        Kernel kernel;
        auto good = newNormal();
        CHECK_EQ(static_cast<int>(kernel.addModule(std::move(good), true)),
                 static_cast<int>(Error::Ok));
        CHECK(kernel.selfTestReport().overall == SelfTestOverall::Pass);
    }
}

// ── A-8：事件总线 ───────────────────────────────────────────────────────────

struct BusCounter {
    std::size_t calls = 0;
    std::string lastTopic;
};

void onBusEvent(void* user, const bbcoop::kernel::Event& event) {
    auto* counter = static_cast<BusCounter*>(user);
    ++counter->calls;
    if (event.topic != nullptr) {
        counter->lastTopic = event.topic;
    }
}

// 重入：处理器内部再 publish 必须被拒绝，否则会无限递归。
bbcoop::kernel::EventBus* g_busForReentry = nullptr;
std::size_t g_reentryResult = 999;

void onReentrantPublish(void*, const bbcoop::kernel::Event&) {
    if (g_busForReentry != nullptr) {
        g_reentryResult =
            static_cast<std::size_t>(g_busForReentry->publish("topic", nullptr, 0));
    }
}

void testEventBusSemantics() {
    bbcoop::kernel::EventBus bus;
    BusCounter counter;
    CHECK_EQ(static_cast<int>(bus.subscribe("death", onBusEvent, &counter)),
             static_cast<int>(Error::Ok));

    CHECK_EQ(static_cast<int>(bus.publish("death", nullptr, 42)), static_cast<int>(Error::Ok));
    CHECK_EQ(counter.calls, static_cast<std::size_t>(1));
    CHECK_EQ(counter.lastTopic, std::string("death"));
    CHECK_EQ(bus.publishCount(), static_cast<std::size_t>(1));

    // 重复订阅同一 (topic, handler, user)：拒绝，避免同一条事件投两次。
    CHECK_EQ(static_cast<int>(bus.subscribe("death", onBusEvent, &counter)),
             static_cast<int>(Error::Refused));

    // 重入被拒绝并计数。
    g_busForReentry = &bus;
    g_reentryResult = 999;
    CHECK_EQ(static_cast<int>(bus.subscribe("reentry", onReentrantPublish, nullptr)),
             static_cast<int>(Error::Ok));
    CHECK_EQ(static_cast<int>(bus.publish("reentry", nullptr, 1)), static_cast<int>(Error::Ok));
    CHECK_EQ(g_reentryResult, static_cast<std::size_t>(Error::Refused));
    CHECK_EQ(bus.reentrantRejections(), static_cast<std::size_t>(1));
    g_busForReentry = nullptr;

    // 没订阅过的事件：投递成功但没有接收者（不是错误）。
    CHECK_EQ(static_cast<int>(bus.publish("nobody", nullptr, 2)), static_cast<int>(Error::Ok));

    // 取消订阅：没订阅过时返回 Refused，而不是静默成功。
    CHECK_EQ(static_cast<int>(bus.unsubscribe("nobody", onBusEvent, &counter)),
             static_cast<int>(Error::Refused));
    CHECK_EQ(static_cast<int>(bus.unsubscribe("death", onBusEvent, &counter)),
             static_cast<int>(Error::Ok));
}

// ── A-9：日志过滤与计数 ─────────────────────────────────────────────────────

struct LogCapture {
    std::size_t lines = 0;
    std::string last;
};

void onLogLine(void* user, const char* line) {
    auto* capture = static_cast<LogCapture*>(user);
    ++capture->lines;
    if (line != nullptr) {
        capture->last = line;
    }
}

void testLoggerFiltersAndCounts() {
    Kernel kernel;
    bbcoop::kernel::KernelLogger& logger = kernel.logger();

    LogCapture capture;
    logger.setSink(onLogLine, &capture);
    logger.setMinLevel(bbcoop::LogLevel::Warn);

    logger.log(bbcoop::LogLevel::Info, "mod.x", "info is below the threshold");
    CHECK_EQ(capture.lines, static_cast<std::size_t>(0));
    CHECK_EQ(logger.droppedCount(), static_cast<std::uint32_t>(1));

    logger.log(bbcoop::LogLevel::Warn, "mod.x", "warn passes");
    CHECK_EQ(capture.lines, static_cast<std::size_t>(1));
    CHECK_EQ(logger.writtenCount(), static_cast<std::uint32_t>(1));

    // 模块 ID 与消息都要出现在这一行里，否则按模块过滤就无从谈起。
    CHECK(capture.last.find("mod.x") != std::string::npos);
    CHECK(capture.last.find("warn passes") != std::string::npos);
}

// ── 服务注册表：owner 归属与 shutdown 撤销 ──────────────────────────────────

void testServiceRegistryOwnership() {
    Kernel kernel;
    bbcoop::kernel::ServiceTable& services = kernel.services();

    int service = 5;
    CHECK_EQ(static_cast<int>(services.publishFor("mod.a", "svc.x", 1, &service)),
             static_cast<int>(Error::Ok));
    CHECK(services.find("svc.x", 1) == &service);

    // 同一个 (名字, 主版本) 重复发布：拒绝，不覆盖已有实现。
    int other = 6;
    CHECK_EQ(static_cast<int>(services.publishFor("mod.b", "svc.x", 1, &other)),
             static_cast<int>(Error::Refused));
    CHECK(services.find("svc.x", 1) == &service);

    // 别的模块不能撤销我的发布。
    CHECK_EQ(static_cast<int>(services.withdrawFor("mod.b", "svc.x", 1)),
             static_cast<int>(Error::Refused));
    CHECK(services.find("svc.x", 1) == &service);

    // 主版本不同 → 是另一个服务。
    CHECK(services.find("svc.x", 2) == nullptr);

    // 关闭某个模块时按 owner 整体撤销。
    CHECK_EQ(services.withdrawAllFor("mod.a"), static_cast<std::size_t>(1));
    CHECK(services.find("svc.x", 1) == nullptr);
}

// ── shutdown 幂等 ───────────────────────────────────────────────────────────

void testShutdownIsIdempotentAndCallsFailedModules() {
    Kernel kernel;
    auto good = newNormal();
    NormalModule* rawGood = good.get();
    CHECK_EQ(static_cast<int>(kernel.addModule(std::move(good), false)),
             static_cast<int>(Error::Ok));

    auto bad = uniqueInitFail();
    InitFailModule* rawBad = bad.get();
    CHECK_EQ(static_cast<int>(kernel.addModule(std::move(bad), false)),
             static_cast<int>(Error::Ok));

    kernel.shutdownAll();
    // init 曾经失败的模块也要收到 shutdown（冻结接口的要求）。
    CHECK_EQ(rawGood->shutdownCalls, static_cast<std::uint32_t>(1));
    CHECK_EQ(rawBad->shutdownCalls, static_cast<std::uint32_t>(1));
    CHECK_EQ(kernel.moduleCount(), static_cast<std::size_t>(0));

    kernel.shutdownAll();  // 幂等：再来一次不崩、不重复调用已释放的实例
    CHECK_EQ(rawGood->shutdownCalls, static_cast<std::uint32_t>(1));
}

}  // namespace

int main() {
    testInitFailureDoesNotBlockOthers();
    testTickTimeoutTripsTheCircuit();
    testConsecutiveFailuresTripOnce();
    testChannelClosedMarksEveryModuleOnThatChannel();
    testHeartbeatStallRequestsTerminationOnce();
    testHealthAggregation();
    testSelfTestJsonCarriesSourceAndReason();
    testSelfTestOverallRules();
    testEventBusSemantics();
    testLoggerFiltersAndCounts();
    testServiceRegistryOwnership();
    testShutdownIsIdempotentAndCallsFailedModules();

    std::printf("checks=%d failures=%d\n", bbcoop::test::g_checks, bbcoop::test::g_failures);
    return (bbcoop::test::g_failures == 0) ? 0 : 1;
}
