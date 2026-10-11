// 本文件是 v1 冻结接口的一部分（任务书 §3.6 原则 3）。
//
// 冻结后的任何变更按 §52-c 的流程走：
//   建 Issue -> 评估影响面 -> 提供过渡方案 -> AI-00 裁决 -> 同步 docs/interfaces/
// **禁止**为了让某个模块方便而直接改这里。
//
// 两条贯穿全局的约束：
//   * 跨模块边界的函数全部 noexcept —— 异常穿过边界会让一个模块的失败
//     变成整个进程的失败（§3.6 原则 5）。
//   * 接口层不提供任何「先连上再说」的路径 —— crypto 失败即拒绝联机（原则 6）。

#pragma once

#include <cstdint>

#include "errors.h"
#include "module_context.h"
#include "version.h"

namespace bbcoop {

// 模块的健康状态。Kernel 汇总它来生成 `--self-test` 的输出。
enum class Health : std::int32_t {
    // 正常工作。
    Ok = 0,

    // 部分能力不可用，但进程与模块本身都在。
    Degraded = 1,

    // 未初始化、已被熔断、或尚未实现。
    // **骨架模块在实现之前一律返回它**（T-007 验收）。
    Unavailable = 2,
};

inline constexpr const char* to_string(Health h) noexcept {
    switch (h) {
        case Health::Ok: return "Ok";
        case Health::Degraded: return "Degraded";
        case Health::Unavailable: return "Unavailable";
    }
    return "Unknown";
}

// 每个模块必须实现的接口。
//
// 全部方法 noexcept。理由见文件头的说明：异常跨过模块边界，
// 一个模块的失败就会变成整个进程的失败，而那正是 §3.6 原则 5 要避免的。
class IModule {
public:
    IModule() = default;
    virtual ~IModule() = default;
    IModule(const IModule&) = delete;
    IModule& operator=(const IModule&) = delete;

    // 与任务书 §3.6 注册表逐字一致。Kernel 用它做注册、日志过滤与自检报告。
    virtual const char* id() const noexcept = 0;

    // 初始化。
    //
    // **失败不得阻断其他模块**（§3.6 原则 5）：返回非 Ok 时，
    // Kernel 只把本模块标记为 Unavailable，继续加载其余模块。
    // 因此实现里不要做「依赖没准备好就让整个进程退出」这种事。
    virtual Error init(ModuleContext& ctx) noexcept = 0;

    // 周期调用。单次调用超过 Kernel 的阈值即触发熔断。
    // 阈值的具体数值**待 T-005 确定**（任务书未给出）。
    virtual Error tick(std::uint64_t now_ms) noexcept = 0;

    // 关闭。必须幂等，且即使 init 曾经失败也应能安全调用。
    virtual void shutdown() noexcept = 0;

    virtual Health health() const noexcept = 0;
};

// 模块的创建函数签名。
//
// Kernel 通过它创建实例，因此不需要知道模块的具体类型。
//
// 返回 nullptr 表示创建失败 —— Kernel 按「本模块不可用」处理，
// 不影响其他模块。
using CreateModuleFn = IModule* (*)() noexcept;

// 一个模块的注册项。Kernel 的注册表由它们构成。
//
// 具体采用静态注册表还是动态加载，由 **T-005 决定**：
// 任务书 §41.1 对 T-005 的要求是「模块加载、注册表」，
// 但没有规定机制。接口层只固定这里的形状。
struct ModuleDescriptor {
    const char* id = nullptr;
    CreateModuleFn create = nullptr;
};

}  // namespace bbcoop
