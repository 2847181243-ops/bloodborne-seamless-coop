// mod.discovery 的骨架实现（T-007）。
// owner: AI-B2。
//
// 模块 ID 与任务书 §3.6 注册表逐字一致：`mod.discovery`（Layer 0，发现）。
// 职责（任务书 §3.6）：房间码、Peer 发现。
//
// 这个文件是 17 个模块的**模板**：形状（实现 IModule + 导出工厂 + 骨架阶段
// 一切返回 Unavailable 且不崩溃）照着它写，职责内容各自填。
// ── 骨架阶段的约定（任务书 §41.1 T-007 的验收）────────────────────────────
//   * init 返回 Error::Unavailable，且**不崩溃**（连 ctx.host == nullptr 也不崩）；
//   * tick 返回 Unavailable 且**不记日志**：tick 每帧都会被调用，在这里记日志
//     会把日志刷爆（任务书 §34）；
//   * health 返回 Health::Unavailable；
//   * shutdown 幂等，init 没成功也能安全调用；
//   * **不发布任何服务**。服务注册表拒绝重复发布（返回 Refused 且不覆盖），
//     所以先发布一个空壳占位会让**真正的实现再也发布不上去**。
//
// ⚠️ 实现这个模块时最容易踩的一件事：**init 成功之后，tick 应当返回 Ok**，
//    「没事可做」也算成功（那属于健康，用 health() 表达）。
//    如果 tick 继续返回 Unavailable，Kernel 会把它当成 tick 失败并累计到阈值，
//    三次之后熔断这个模块 —— 一个正常的模块会被判死。
//    骨架阶段之所以返回 Unavailable，是因为 init 从未成功，tick 根本到不了这里。
//
// ── 实现这个模块时不要做什么 ───────────────────────────────────────────────
//   * 只 include `src/interfaces/` 里的冻结接口。不得 include kernel/ 或别的模块
//     （§3.6 硬性禁止）；
//   * 需要别的模块的能力时，用 find_service<T>()（module_context.h）并**处理 nullptr**
//     —— 依赖缺失是正常状态，不是异常（§3.6 原则 5）；
//   * 不要抛异常：跨模块边界的函数是 noexcept 的，抛出会直接 terminate 整个进程；
//   * 不得为了实现功能而削弱安全约束（任务书 §2.4）；也**不得用 Mock 冒充实现**
//     （§38 六条明令禁止）。

#include <cstdint>
#include <new>

#include "errors.h"
#include "imodule.h"
#include "module_context.h"

namespace {

// 模块 ID 用私有常量而不是散落的字面量：注册表、日志过滤、自检输出三处必须一致。
constexpr const char* kModuleId = "mod.discovery";

class DiscoveryModule final : public bbcoop::IModule {
public:
    DiscoveryModule() = default;
    ~DiscoveryModule() override = default;

    const char* id() const noexcept override { return kModuleId; }

    bbcoop::Error init(bbcoop::ModuleContext& ctx) noexcept override {
        // ctx.host 可能是 nullptr（模块在没有宿主的环境里被直接实例化，例如单元测试），
        // 所以取用它之前一律判空 —— 依赖缺失是正常状态。
        m_moduleId = ctx.module_id != nullptr ? ctx.module_id : kModuleId;
        m_host = ctx.host;
        log(bbcoop::LogLevel::Info, "init: 尚未实现，本次返回 Unavailable");

        // 骨架阶段到此为止：不发布服务、不建立连接、不分配资源。
        // 没有资源要释放，因此 shutdown 天然幂等。
        return bbcoop::Error::Unavailable;
    }

    bbcoop::Error tick(std::uint64_t nowMs) noexcept override {
        // 骨架阶段什么都不做，也**不记日志**（§34：tick 每帧调用）。
        (void)nowMs;
        return bbcoop::Error::Unavailable;
    }

    void shutdown() noexcept override {
        // 幂等：重复调用、init 从未成功时调用都必须安全。
        // 实现之后要在这里断开连接、停止线程，并且**不得抛异常**。
        m_host = nullptr;
    }

    bbcoop::Health health() const noexcept override {
        return bbcoop::Health::Unavailable;
    }

private:
    void log(bbcoop::LogLevel level, const char* message) noexcept {
        if (m_host != nullptr && m_host->logger != nullptr) {
            m_host->logger->log(level, m_moduleId, message);
        }
    }

    const char* m_moduleId = kModuleId;
    bbcoop::HostServices* m_host = nullptr;
};

}  // namespace

bbcoop::IModule* bbcoop_create_mod_discovery() noexcept {
    // 返回 nullptr 表示创建失败，Kernel 按「本模块不可用」处理，不影响其他模块。
    return new (std::nothrow) DiscoveryModule();
}
