// T-006 单元测试之二：**故障隔离**。
// owner: AI-00（T-006）。
//
// 覆盖任务书 §41.1 T-005 的验收「init 失败不阻断其他模块」与 §3.6 原则 5「故障不传播」。
//
// ⚠️ 这里能证明的边界（写在测试里，避免被读过成「崩溃已被隔离」）：
//   本套件注入的是**返回错误**。Kernel 拦得住「模块报告失败」，拦不住段错误与
//   真正卡死不返回的 tick —— 那两者会带走整个进程（06-process-boundary.md §6）。
//   因此本套件**不注入**段错误，也**不注入**「tick 抛异常」：`IModule::tick` 是
//   noexcept 的，派生类不能放宽异常规格，抛出即 std::terminate。

#include <memory>
#include <new>

#include "bbcoop/kernel/kernel.h"
#include "bbcoop/kernel/policy.h"
#include "fixture_modules.h"
#include "test_framework.h"

namespace {

using bbcoop::Error;
using bbcoop::IModule;
using bbcoop::kernel::Kernel;
using bbcoop::kernel::KernelPolicy;
using bbcoop::kernel::ModuleStatus;
using bbcoop::fixtures::InitFailModule;
using bbcoop::fixtures::NormalModule;
using bbcoop::fixtures::TickFixtureModule;
using bbcoop::fixtures::TickMode;

std::unique_ptr<NormalModule> newNormal(const char* id) {
    return std::unique_ptr<NormalModule>(new (std::nothrow) NormalModule(id));
}
std::unique_ptr<InitFailModule> newInitFail(const char* id, Error result) {
    return std::unique_ptr<InitFailModule>(new (std::nothrow) InitFailModule(id, result));
}
std::unique_ptr<TickFixtureModule> newTickFixture(const char* id) {
    return std::unique_ptr<TickFixtureModule>(new (std::nothrow) TickFixtureModule(id));
}

// 一个模块 init 失败 → 其余模块照常完成初始化，失败者**永不 tick**。
void testInitFailureDoesNotBlockOthers() {
    Kernel kernel;
    auto first = newNormal("mod.a");
    auto broken = newInitFail("mod.broken", Error::Internal);
    auto last = newNormal("mod.c");
    NormalModule* rawFirst = first.get();
    InitFailModule* rawBroken = broken.get();
    NormalModule* rawLast = last.get();

    CHECK_EQ(static_cast<int>(kernel.addModule(std::move(first), false)),
             static_cast<int>(Error::Ok));
    CHECK_EQ(static_cast<int>(kernel.addModule(std::move(broken), false)),
             static_cast<int>(Error::Ok));
    CHECK_EQ(static_cast<int>(kernel.addModule(std::move(last), false)),
             static_cast<int>(Error::Ok));

    const ModuleStatus* brokenStatus = kernel.findModule("mod.broken");
    CHECK(brokenStatus != nullptr && !brokenStatus->initialized);
    CHECK(brokenStatus->health == bbcoop::Health::Unavailable);
    CHECK(kernel.findModule("mod.a")->initialized);
    CHECK(kernel.findModule("mod.c")->initialized);

    // 失败者永不 tick，成功者照常 tick —— 而且两个成功者都跑到。
    kernel.tick(1000);
    kernel.tick(2000);
    CHECK_EQ(rawBroken->tickCalls, static_cast<std::uint32_t>(0));
    CHECK_EQ(rawFirst->tickCalls, static_cast<std::uint32_t>(2));
    CHECK_EQ(rawLast->tickCalls, static_cast<std::uint32_t>(2));
}

// 一个模块一直 tick 失败 → 熔断之后**其余模块继续 tick**（故障不传播）。
void testTickFailureDoesNotStopOthers() {
    KernelPolicy policy;
    policy.failureThreshold = 2;
    Kernel kernel(policy);

    auto bad = newTickFixture("mod.bad");
    bad->mode = TickMode::PersistentFailure;
    TickFixtureModule* rawBad = bad.get();
    auto good = newNormal("mod.good");
    NormalModule* rawGood = good.get();

    CHECK_EQ(static_cast<int>(kernel.addModule(std::move(bad), false)),
             static_cast<int>(Error::Ok));
    CHECK_EQ(static_cast<int>(kernel.addModule(std::move(good), false)),
             static_cast<int>(Error::Ok));

    kernel.tick(1000);  // 第 1 次失败
    kernel.tick(1100);  // 第 2 次失败 → 熔断
    CHECK(kernel.findModule("mod.bad")->circuitOpen);

    // 熔断者不再被调用，但另一个模块**照常被调用**（这是「故障不传播」的执行点）。
    const std::uint32_t badCallsAtTrip = rawBad->tickCalls;
    kernel.tick(1200);
    kernel.tick(1300);
    CHECK_EQ(rawBad->tickCalls, badCallsAtTrip);
    CHECK_EQ(rawGood->tickCalls, static_cast<std::uint32_t>(4));
}

// shutdown：幂等，且 init 曾经失败的模块也要收到（冻结接口的要求）。
void testShutdownIsIdempotentAndReachesFailedModules() {
    Kernel kernel;
    auto good = newNormal("mod.good");
    NormalModule* rawGood = good.get();
    auto broken = newInitFail("mod.broken", Error::Unavailable);
    InitFailModule* rawBroken = broken.get();

    CHECK_EQ(static_cast<int>(kernel.addModule(std::move(good), false)),
             static_cast<int>(Error::Ok));
    CHECK_EQ(static_cast<int>(kernel.addModule(std::move(broken), false)),
             static_cast<int>(Error::Ok));

    kernel.shutdownAll();
    CHECK_EQ(rawGood->shutdownCalls, static_cast<std::uint32_t>(1));
    CHECK_EQ(rawBroken->shutdownCalls, static_cast<std::uint32_t>(1));
    CHECK_EQ(kernel.moduleCount(), static_cast<std::size_t>(0));

    // 再来一次：不崩、不重复调用（幂等）。
    kernel.shutdownAll();
    CHECK_EQ(rawGood->shutdownCalls, static_cast<std::uint32_t>(1));
    CHECK_EQ(rawBroken->shutdownCalls, static_cast<std::uint32_t>(1));
}

// shutdown 要撤销该模块发布的服务（module_context.h 的约定）。
void testShutdownWithdrawsPublishedServices() {
    Kernel kernel;
    auto good = newNormal("mod.publisher");
    CHECK_EQ(static_cast<int>(kernel.addModule(std::move(good), false)),
             static_cast<int>(Error::Ok));

    int service = 7;
    CHECK_EQ(static_cast<int>(kernel.services().publishFor("mod.publisher", "svc", 1, &service)),
             static_cast<int>(Error::Ok));
    CHECK(kernel.services().find("svc", 1) == &service);

    kernel.shutdownAll();
    CHECK(kernel.services().find("svc", 1) == nullptr);
}

}  // namespace

int main() {
    testInitFailureDoesNotBlockOthers();
    testTickFailureDoesNotStopOthers();
    testShutdownIsIdempotentAndReachesFailedModules();
    testShutdownWithdrawsPublishedServices();
    return bbcoop::test::finish();
}
