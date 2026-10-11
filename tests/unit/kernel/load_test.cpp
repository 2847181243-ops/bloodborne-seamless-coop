// T-006 单元测试之一：**加载**。
// owner: AI-00（T-006）。
//
// 覆盖任务书 §41.1 T-006 要求的第一个领域。重点在「表里的每一项如何被接纳或拒绝」：
//   * 正常加载与 init 结果的分派；
//   * 工厂返回 nullptr；
//   * 表项 ID 与实例自报 ID **不一致** → 拒绝（表将来是生成的，名字对不上说明生成物坏了）；
//   * 重复 ID、表满、nullptr、空 ID；
//   * `skipped[]` 透传到自检报告（T-012 的模块隔离判定要看「某个模块确实被禁用了」）；
//   * 服务注册表：init 期内发布、find 能取到、shutdown 时按 owner 整体撤销。

#include <cstring>
#include <memory>
#include <new>

#include "bbcoop/kernel/kernel.h"
#include "bbcoop/kernel/policy.h"
#include "fixture_modules.h"
#include "test_framework.h"

namespace {

using bbcoop::Error;
using bbcoop::IModule;
using bbcoop::ModuleDescriptor;
using bbcoop::kernel::Kernel;
using bbcoop::kernel::ModuleTable;
using bbcoop::fixtures::InitFailModule;
using bbcoop::fixtures::NormalModule;
using bbcoop::fixtures::createInitFailModule;
using bbcoop::fixtures::createNormalModule;
using bbcoop::fixtures::createNullModule;
using bbcoop::fixtures::createWrongIdModule;

std::unique_ptr<NormalModule> newNormal(const char* id = "mod.normal") {
    return std::unique_ptr<NormalModule>(new (std::nothrow) NormalModule(id));
}

// 表项齐全时应当全部加载，且 init 结果按模块各自分派。
void testLoadTableAcceptsAndDispatches() {
    const ModuleDescriptor descriptors[] = {
        {"mod.normal", createNormalModule},
        {"mod.initfail", createInitFailModule},
    };
    const bool required[] = {false, false};
    ModuleTable table;
    table.loadable = descriptors;
    table.requiredFlags = required;
    table.loadableCount = 2;

    Kernel kernel;
    kernel.loadTable(table);

    CHECK_EQ(kernel.moduleCount(), static_cast<std::size_t>(2));
    const bbcoop::kernel::ModuleStatus* ok = kernel.findModule("mod.normal");
    const bbcoop::kernel::ModuleStatus* bad = kernel.findModule("mod.initfail");
    CHECK(ok != nullptr && bad != nullptr);
    if (ok == nullptr || bad == nullptr) {
        return;
    }
    CHECK(ok->initialized);
    CHECK(!bad->initialized);
    CHECK(bad->unavailableReason == bbcoop::kernel::UnavailableReason::InitFailed);
}

// 工厂返回 nullptr：按「本模块不可用」处理，**不影响其他模块**（冻结接口的约定）。
void testLoadTableFactoryReturnsNull() {
    const ModuleDescriptor descriptors[] = {
        {"mod.normal", createNormalModule},
        {"mod.null", createNullModule},
    };
    ModuleTable table;
    table.loadable = descriptors;
    table.loadableCount = 2;

    Kernel kernel;
    kernel.loadTable(table);

    // 创建失败的项根本不进槽位；其余照常。
    CHECK_EQ(kernel.moduleCount(), static_cast<std::size_t>(1));
    CHECK(kernel.findModule("mod.null") == nullptr);
    CHECK(kernel.findModule("mod.normal") != nullptr);
    CHECK(kernel.findModule("mod.normal")->initialized);
}

// 表里写 mod.x、实例自报别的名字 → 拒绝这一项。
// 这条是「生成物或命名约定坏了」的哨兵：宁可少加载一个，也不要出现
// 「表里一个名字、跑起来另一个」的静默不一致。
void testLoadTableRejectsIdMismatch() {
    const ModuleDescriptor descriptors[] = {
        {"mod.normal", createNormalModule},
        {"mod.claiming-to-be-normal", createWrongIdModule},
    };
    ModuleTable table;
    table.loadable = descriptors;
    table.loadableCount = 2;

    Kernel kernel;
    kernel.loadTable(table);

    CHECK_EQ(kernel.moduleCount(), static_cast<std::size_t>(1));
    CHECK(kernel.findModule("mod.normal") != nullptr);
    // 表里声明的名字查不到（它自报的是别的名字），实例自报的名字也查不到（被拒绝）。
    CHECK(kernel.findModule("mod.claiming-to-be-normal") == nullptr);
    CHECK(kernel.findModule("mod.actually-something-else") == nullptr);
}

// 表项不完整（缺 id 或缺工厂）：跳过该项并记错误，其余照常。
void testLoadTableSkipsIncompleteEntries() {
    const ModuleDescriptor descriptors[] = {
        {"mod.normal", createNormalModule},
        {nullptr, createNormalModule},
        {"mod.nofactory", nullptr},
    };
    ModuleTable table;
    table.loadable = descriptors;
    table.loadableCount = 3;

    Kernel kernel;
    kernel.loadTable(table);
    CHECK_EQ(kernel.moduleCount(), static_cast<std::size_t>(1));
    CHECK(kernel.findModule("mod.normal") != nullptr);
}

// addModule 的入参校验：nullptr、空 ID、重复 ID。
void testAddModuleValidation() {
    Kernel kernel;
    CHECK_EQ(static_cast<int>(kernel.addModule(nullptr)),
             static_cast<int>(Error::InvalidArgument));

    auto blank = newNormal("");
    CHECK_EQ(static_cast<int>(kernel.addModule(std::move(blank))),
             static_cast<int>(Error::InvalidArgument));

    auto first = newNormal("mod.dup");
    CHECK_EQ(static_cast<int>(kernel.addModule(std::move(first))),
             static_cast<int>(Error::Ok));

    // 重复 ID：拒绝，不覆盖已有槽位。
    auto second = newNormal("mod.dup");
    CHECK_EQ(static_cast<int>(kernel.addModule(std::move(second))),
             static_cast<int>(Error::Refused));
    CHECK_EQ(kernel.moduleCount(), static_cast<std::size_t>(1));
}

// 表满：超过 kMaxModules 的项被拒绝，已有模块不受影响。
//
// ⚠️ 这里的 id 必须放**静态存储**：Kernel 只存 `id()` 返回的**指针**，不复制字符串
//    （定长存储 + noexcept 路径不分配内存，见 kernel.h 的约定）。
//    第一版把 id 放在循环里的栈缓冲上，缓冲每轮被复用 → 所有槽位的 id 指向同一地址，
//    于是从第 2 个开始全被判成「重复 ID」。日志里那串 "duplicate module id" 就是它。
void testAddModuleRefusedWhenTableFull() {
    Kernel kernel;
    static char ids[bbcoop::kernel::kMaxModules][16];
    std::size_t accepted = 0;
    for (std::size_t i = 0; i < bbcoop::kernel::kMaxModules; ++i) {
        std::snprintf(ids[i], sizeof(ids[i]), "mod.%zu", i);
        auto* raw = new (std::nothrow) NormalModule(ids[i]);
        if (kernel.addModule(std::unique_ptr<IModule>(raw)) == Error::Ok) {
            ++accepted;
        }
    }
    CHECK_EQ(accepted, bbcoop::kernel::kMaxModules);
    CHECK_EQ(static_cast<int>(kernel.addModule(newNormal("mod.overflow"))),
             static_cast<int>(Error::Refused));
    CHECK_EQ(kernel.moduleCount(), bbcoop::kernel::kMaxModules);
}

// skipped[] 必须透传到自检报告：只看 modules 数组分不清
// 「被配置禁用」与「构建时漏了」—— T-012 的模块隔离判定依赖这一点。
void testSkippedModulesReachTheReport() {
    const char* const skippedIds[] = {"mod.loot", "mod.pvp"};
    const char* const skippedReasons[] = {"disabled", "not_implemented"};

    ModuleTable table;
    table.skippedIds = skippedIds;
    table.skippedReasons = skippedReasons;
    table.skippedCount = 2;

    Kernel kernel;
    kernel.loadTable(table);

    const bbcoop::kernel::SelfTestReport report = kernel.selfTestReport();
    CHECK_EQ(report.skippedCount, static_cast<std::size_t>(2));
    CHECK(report.skippedIds != nullptr && report.skippedReasons != nullptr);
    if (report.skippedIds != nullptr && report.skippedReasons != nullptr) {
        CHECK(std::strcmp(report.skippedIds[0], "mod.loot") == 0);
        CHECK(std::strcmp(report.skippedReasons[0], "disabled") == 0);
        CHECK(std::strcmp(report.skippedReasons[1], "not_implemented") == 0);
    }
}

// 服务注册表：init 期内发布 → find 取到；shutdown 后按 owner 整体撤销。
void testServiceRegistryPublishAndWithdraw() {
    Kernel kernel;
    bbcoop::kernel::ServiceTable& services = kernel.services();

    int service = 5;
    CHECK_EQ(static_cast<int>(services.publishFor("mod.a", "svc.x", 1, &service)),
             static_cast<int>(Error::Ok));
    CHECK(services.find("svc.x", 1) == &service);

    // 重复发布同一个 (名字, 主版本)：拒绝，不覆盖已有实现。
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

    // shutdown 时按 owner 撤销。
    CHECK_EQ(services.withdrawAllFor("mod.a"), static_cast<std::size_t>(1));
    CHECK(services.find("svc.x", 1) == nullptr);
}

}  // namespace

int main() {
    testLoadTableAcceptsAndDispatches();
    testLoadTableFactoryReturnsNull();
    testLoadTableRejectsIdMismatch();
    testLoadTableSkipsIncompleteEntries();
    testAddModuleValidation();
    testAddModuleRefusedWhenTableFull();
    testSkippedModulesReachTheReport();
    testServiceRegistryPublishAndWithdraw();
    return bbcoop::test::finish();
}
