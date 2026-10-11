// T-006 单元测试之四：**健康聚合**。
// owner: AI-00（T-006）。
//
// 覆盖任务书 §41.1 T-006 的第四个领域，以及 07-kernel-seam §1.5：
// 自检报告的每个模块要能区分「为什么不可用」，所以聚合不能只看计数。
//
// 一条容易做错的规则在这里被钉住：模块**自报**的 Health 不能盖过 Kernel 的判定 ——
// 未初始化或已熔断的模块一律是 Unavailable，否则自检会显示一个假的 Ok
// （夹具里的 InitFailModule 就故意自报 Ok）。

#include <memory>
#include <new>
#include <string>

#include "bbcoop/kernel/kernel.h"
#include "bbcoop/kernel/self_test.h"
#include "fixture_modules.h"
#include "test_framework.h"

namespace {

using bbcoop::Error;
using bbcoop::Health;
using bbcoop::ipc::ChannelId;
using bbcoop::kernel::Kernel;
using bbcoop::kernel::ModuleStatus;
using bbcoop::kernel::SelfTestOverall;
using bbcoop::kernel::UnavailableReason;
using bbcoop::fixtures::InitFailModule;
using bbcoop::fixtures::NormalModule;

std::unique_ptr<NormalModule> newNormal(const char* id) {
    return std::unique_ptr<NormalModule>(new (std::nothrow) NormalModule(id));
}
std::unique_ptr<InitFailModule> newInitFail(const char* id) {
    return std::unique_ptr<InitFailModule>(
        new (std::nothrow) InitFailModule(id, Error::Internal));
}

// 聚合：三种状态各自计数，且**最差状态**要单独给出
//（「一个模块不可用」与「五个模块全 Ok」是两种系统，只看计数分不出来）。
void testSummaryCountsAndWorst() {
    Kernel kernel;
    auto good = newNormal("mod.good");
    good->reportedHealth = Health::Degraded;
    CHECK_EQ(static_cast<int>(kernel.addModule(std::move(good), false)),
             static_cast<int>(Error::Ok));
    auto broken = newInitFail("mod.broken");
    CHECK_EQ(static_cast<int>(kernel.addModule(std::move(broken), false)),
             static_cast<int>(Error::Ok));

    // 模块自报的健康要经过一次巡检才进聚合（tick 里按周期做，这里手工触发）。
    kernel.pollHealth();

    const bbcoop::kernel::HealthSummary summary = kernel.summarizeHealth();
    CHECK_EQ(summary.total, static_cast<std::size_t>(2));
    CHECK_EQ(summary.ok, static_cast<std::size_t>(0));
    CHECK_EQ(summary.degraded, static_cast<std::size_t>(1));
    CHECK_EQ(summary.unavailable, static_cast<std::size_t>(1));
    CHECK(summary.worst == Health::Unavailable);
}

// Kernel 的覆盖规则：未初始化 / 已熔断 → Unavailable，不管模块自报什么。
// （InitFailModule::health() 故意返回 Ok —— 这条就是防「自检里出现假的 Ok」。）
void testKernelOverrideBeatsSelfReport() {
    Kernel kernel;
    auto broken = newInitFail("mod.liar");
    CHECK_EQ(static_cast<int>(kernel.addModule(std::move(broken), false)),
             static_cast<int>(Error::Ok));

    kernel.pollHealth();
    const ModuleStatus* status = kernel.findModule("mod.liar");
    CHECK(!status->initialized);
    CHECK(status->health == Health::Unavailable);
}

// overall 的三值规则（09-kernel-host.md §8）：
//   fail     至少一个 required 模块不是 Ok
//   degraded 没有 required 失败，但没有模块、或有模块不是 Ok
//   pass     所有已加载模块都是 Ok，且至少有一个模块
void testSelfTestOverallRules() {
    // 一个模块都没有 → degraded（不是 pass）：宿主活着但没有任何能力。
    // 这一条是防「真空通过」的 —— 空注册表若算 pass，Gate 6 会在框架期恒真。
    {
        Kernel kernel;
        CHECK(kernel.selfTestReport().overall == SelfTestOverall::Degraded);
    }
    // 只有非 required 的模块失败 → degraded。
    {
        Kernel kernel;
        auto broken = newInitFail("mod.optional");
        CHECK_EQ(static_cast<int>(kernel.addModule(std::move(broken), false)),
                 static_cast<int>(Error::Ok));
        CHECK(kernel.selfTestReport().overall == SelfTestOverall::Degraded);
    }
    // 全部正常 → pass。
    {
        Kernel kernel;
        auto good = newNormal("mod.good");
        CHECK_EQ(static_cast<int>(kernel.addModule(std::move(good), true)),
                 static_cast<int>(Error::Ok));
        CHECK(kernel.selfTestReport().overall == SelfTestOverall::Pass);
    }
    // required 失败 → fail（任务书 §41.1 T-014 的判据）。
    {
        Kernel kernel;
        auto broken = newInitFail("mod.required");
        CHECK_EQ(static_cast<int>(kernel.addModule(std::move(broken), true)),
                 static_cast<int>(Error::Ok));
        CHECK(kernel.selfTestReport().overall == SelfTestOverall::Fail);
    }
}

// 自检 JSON：来源与原因必须在输出里看得见（07-kernel-seam §1.5），
// 且「没有失败」要写成 JSON 的 null，不能写成字符串 "none"。
void testSelfTestJsonCarriesSourceAndReason() {
    Kernel kernel;
    auto good = newNormal("mod.good");
    CHECK_EQ(static_cast<int>(kernel.addModule(std::move(good), false)),
             static_cast<int>(Error::Ok));
    auto broken = newInitFail("mod.broken");
    CHECK_EQ(static_cast<int>(kernel.addModule(std::move(broken), true)),
             static_cast<int>(Error::Ok));
    const ChannelId channel{5};
    CHECK_EQ(static_cast<int>(kernel.addRemote("mod.relay", channel, true)),
             static_cast<int>(Error::Ok));

    const bbcoop::kernel::SelfTestReport report = kernel.selfTestReport();
    CHECK(report.overall == SelfTestOverall::Fail);  // mod.broken 是 required

    const std::string json = bbcoop::kernel::renderSelfTestJson(report, "0.1.0");
    CHECK(json.find("\"overall\": \"fail\"") != std::string::npos);
    CHECK(json.find("\"version\": \"0.1.0\"") != std::string::npos);
    CHECK(json.find("\"interface_version\": ") != std::string::npos);
    // 两种来源都要看得见。
    CHECK(json.find("\"source\": \"local\"") != std::string::npos);
    CHECK(json.find("\"source\": \"remote\"") != std::string::npos);
    // 失败原因要看得见。
    CHECK(json.find("\"unavailable_reason\": \"init_failed\"") != std::string::npos);
    // 没有失败的那些必须是 null，不是 "none"。
    CHECK(json.find("\"unavailable_reason\": null") != std::string::npos);
    CHECK(json.find("\"none\"") == std::string::npos);
}

}  // namespace

int main() {
    testSummaryCountsAndWorst();
    testKernelOverrideBeatsSelfReport();
    testSelfTestOverallRules();
    testSelfTestJsonCarriesSourceAndReason();
    return bbcoop::test::finish();
}
