// 模块契约测试的共用本体（T-007）。
// owner: AI-00。
//
// 17 个模块的 `src/modules/<名字>/tests/contract_test.cpp` 都只调用这里的一个函数。
//
// 为什么共用一份而不是每个模块各写一遍：这份契约说的是**骨架阶段的对外约定**
// （ID 逐字一致、init 返回 Unavailable、不发布服务、tick 不记日志、shutdown 幂等）。
// 复制 17 份的结果一定是 17 份各自漂移 —— 那时「契约测试」这个名字就不成立了，
// 它变成 17 个各不相同的冒烟测试。
//
// ⚠️ 这里放的是**测试脚手架**，不是产品代码：它只在测试目标里被 include，
//    模块本体（bbcoop_mod_*）不依赖它。
//
// 契约的内容与理由见 docs/architecture/07-kernel-seam.md 与任务书 §3.6 原则 5。

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#include "errors.h"
#include "imodule.h"
#include "module_context.h"
#include "test_framework.h"

namespace bbcoop::fixtures {

// 记账用的日志桩：契约要能断言「tick 不许记日志」。
class RecordingLogger final : public ILogger {
public:
    void log(LogLevel level, const char* module_id, const char* message) noexcept override {
        ++lines;
        lastLevel = level;
        lastModuleId = module_id != nullptr ? module_id : "";
        lastMessage = message != nullptr ? message : "";
    }

    std::size_t lines = 0;
    LogLevel lastLevel = LogLevel::Debug;
    std::string lastModuleId;
    std::string lastMessage;
};

// 记账用的服务注册表桩：契约要能断言「骨架阶段什么都不发布」。
//
// 为什么这条值得断言：服务注册表拒绝重复发布（返回 Refused 且不覆盖），
// 所以「先发布一个空壳占位」会让真正的实现**再也发布不上去**。
// 骨架阶段发布空壳是最容易犯、又最难查的一种错。
class RecordingServices final : public IServiceRegistry {
public:
    void* find(const char* service_name, int version_major) noexcept override {
        ++finds;
        (void)service_name;
        (void)version_major;
        return nullptr;  // 骨架阶段没有任何能力可用
    }

    Error publish(const char* service_name, int version_major, void* service) noexcept override {
        ++publishes;
        lastPublished = service_name != nullptr ? service_name : "";
        (void)version_major;
        (void)service;
        return Error::Ok;
    }

    Error withdraw(const char* service_name, int version_major) noexcept override {
        ++withdraws;
        (void)service_name;
        (void)version_major;
        return Error::Ok;
    }

    std::size_t publishes = 0;
    std::size_t withdraws = 0;
    std::size_t finds = 0;
    std::string lastPublished;
};

// 跑完一个模块的骨架契约。每个模块的 contract_test.cpp 只调用这一个函数。
//
// expected_id 必须与任务书 §3.6 注册表**逐字一致** —— 这是模块身份的唯一来源，
// 自检输出、日志过滤、配置（T-008）三处都按它匹配。
inline void run_module_contract(const char* expected_id, CreateModuleFn create) {
    CHECK(expected_id != nullptr);
    CHECK(create != nullptr);
    if (expected_id == nullptr || create == nullptr) {
        return;
    }

    RecordingLogger logger;
    RecordingServices services;
    HostServices host;
    host.logger = &logger;
    host.services = &services;

    ModuleContext ctx;
    ctx.module_id = expected_id;
    ctx.host = &host;

    // ── 创建 ────────────────────────────────────────────────────────────────
    IModule* module = create();
    CHECK(module != nullptr);
    if (module == nullptr) {
        return;  // 工厂返回 nullptr 也是合法实现（Kernel 按不可用处理），但骨架不该这样
    }

    // ── 身份 ────────────────────────────────────────────────────────────────
    const char* id = module->id();
    CHECK(id != nullptr);
    CHECK(id != nullptr && std::strcmp(id, expected_id) == 0);
    // 指针必须稳定：Kernel 只存这个指针，不复制字符串（kernel.h 的约定）。
    // 返回指向临时缓冲的指针会让注册表指向失效内存。
    CHECK(module->id() == id);

    // ── 未初始化时：不可用 ──────────────────────────────────────────────────
    CHECK(module->health() == Health::Unavailable);

    // ── init：依赖缺失是正常状态 ────────────────────────────────────────────
    // 先用 host == nullptr 调一次：模块在没有宿主的环境里被直接实例化
    // （单元测试、独立 CLI）时必须不崩、不空指针解引用。
    ModuleContext bare;
    bare.module_id = expected_id;
    bare.host = nullptr;
    CHECK(module->init(bare) == Error::Unavailable);

    // 正常路径。
    CHECK(module->init(ctx) == Error::Unavailable);
    CHECK(module->health() == Health::Unavailable);

    // 骨架阶段**不得发布任何服务**（理由见 RecordingServices 的注释）。
    CHECK_EQ(services.publishes, static_cast<std::size_t>(0));

    // ── tick：可反复调用、不崩、不记日志 ────────────────────────────────────
    // tick 每帧都会被调用，在这里记日志会把日志刷爆（任务书 §34）。
    const std::size_t linesAfterInit = logger.lines;
    CHECK(module->tick(1000) == Error::Unavailable);
    CHECK(module->tick(1016) == Error::Unavailable);
    CHECK_EQ(logger.lines, linesAfterInit);

    // ── shutdown：幂等 ──────────────────────────────────────────────────────
    module->shutdown();
    module->shutdown();  // 第二次必须安全
    module->health();    // 关闭之后仍可查询（Kernel 会这么用）
    CHECK(module->id() == id);

    // 整个生命周期里一个服务都没发布过。
    CHECK_EQ(services.publishes, static_cast<std::size_t>(0));

    // 从未 init 过的实例：shutdown 也要安全（Kernel 会对 init 失败的模块调用它）。
    IModule* fresh = create();
    CHECK(fresh != nullptr);
    if (fresh != nullptr) {
        fresh->shutdown();
        fresh->shutdown();
        CHECK(fresh->health() == Health::Unavailable);
        delete fresh;
    }

    delete module;
}

}  // namespace bbcoop::fixtures
