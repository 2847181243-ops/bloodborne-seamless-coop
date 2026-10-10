// 独立编译验证（T-004 验收）。
//
// 每个接口头文件都被单独 include 并实例化一次，确保：
//   1. 它自己能编译（不依赖别处的 include 顺序）
//   2. 它不依赖 modules/ 或 kernel/ 下的任何头文件
//
// 这个文件**故意不做运行时断言** —— 它只在编译期证明接口可用。
// 行为验证在 T-006 的 Kernel 单元测试里。

#include "errors.h"
#include "igameplay.h"
#include "imodule.h"
#include "isession.h"
#include "itransport.h"
#include "module_context.h"
#include "version.h"

namespace {

// 一个最小实现：骨架模块应当长这样 —— 什么都不做，返回 Unavailable，
// 且**不崩溃**（任务书 T-007 验收）。
class SkeletonModule final : public bbcoop::IModule {
public:
    const char* id() const noexcept override { return "mod.example"; }

    bbcoop::Error init(bbcoop::ModuleContext&) noexcept override {
        return bbcoop::Error::Unavailable;
    }

    bbcoop::Error tick(std::uint64_t) noexcept override {
        return bbcoop::Error::Unavailable;
    }

    void shutdown() noexcept override {}

    bbcoop::Health health() const noexcept override {
        return bbcoop::Health::Unavailable;
    }
};

// 服务查找在依赖缺失时必须返回 nullptr，而不是崩溃。
void check_service_lookup_is_null_safe() {
    bbcoop::HostServices host;  // logger 与 services 都是 nullptr
    const auto* t = bbcoop::find_service<bbcoop::ITransport>(host);
    (void)t;  // 期望 nullptr；解引用会崩，这里只验证不崩
}

void check_error_helpers_are_constexpr() {
    static_assert(!bbcoop::failed(bbcoop::Error::Ok));
    static_assert(bbcoop::failed(bbcoop::Error::Unavailable));
    static_assert(bbcoop::to_string(bbcoop::Error::CryptoUnavailable)[0] == 'C');
    static_assert(bbcoop::to_string(bbcoop::Health::Unavailable)[0] == 'U');
    static_assert(bbcoop::kInterfaceVersion == 100);
}

}  // namespace

int main() {
    SkeletonModule m;
    bbcoop::ModuleContext ctx;
    (void)m.init(ctx);
    (void)m.tick(0);
    m.shutdown();
    (void)m.health();
    check_service_lookup_is_null_safe();
    check_error_helpers_are_constexpr();
    return 0;
}
