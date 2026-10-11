// Kernel 提供给模块的宿主能力：日志与服务注册表（T-005 产出）。
//
// 本文件属于 Kernel 内部接口：模块**不得** include（见 policy.h 的说明）。
// 模块拿到的是 src/interfaces/module_context.h 里的 ILogger 与 IServiceRegistry，
// 而不是这里的实现类型。
//
// ── 为什么服务注册表要按模块分视图 ─────────────────────────────────────────
//
// 冻结接口的 publish / withdraw 没有模块参数，但 module_context.h 要求
// 「shutdown 后 Kernel 撤销该模块的全部发布」。要让这件事可行，注册表必须知道
// 「哪条发布是谁发的」。所以每个模块拿到一个带 owner 的视图（ModuleServiceView），
// 由它记录归属；Kernel 在 shutdown 时按 owner 整体撤销。
// 这是接口层没有写明的部分，属 T-005 的实现决定。
//
// 视图还顺带堵住一条歧义：模块只能撤销自己发布的服务，撤销别人的返回 Refused。

#pragma once

#include <cstddef>
#include <cstdint>

#include "module_context.h"
#include "policy.h"

namespace bbcoop::kernel {

// ILogger 的实现：把日志写到 stderr，并按等级过滤。
//
// 为什么默认写 stderr 而不是 stdout：`bloodcoop-host --self-test` 的 JSON 走 stdout，
// 两者必须分开，否则日志会把 JSON 弄成不可解析的文本。
//
// 不做线程安全（主循环单线程）。不分配内存：一行日志在固定缓冲区里拼好，
// 因为 log() 是 noexcept 的，分配失败抛出的异常不能穿过这条边界。
class KernelLogger final : public bbcoop::ILogger {
public:
    using Sink = void (*)(void* user, const char* line);

    KernelLogger() = default;
    KernelLogger(const KernelLogger&) = delete;
    KernelLogger& operator=(const KernelLogger&) = delete;
    ~KernelLogger() override = default;

    void log(LogLevel level, const char* module_id, const char* message) noexcept override;

    void setMinLevel(LogLevel level) noexcept;
    LogLevel minLevel() const noexcept;

    // 测试与后续任务（T-013 / T-014 要抓日志）用的注入点。
    // 传 nullptr 恢复默认的 stderr 输出。
    void setSink(Sink sink, void* user) noexcept;

    // 因低于门槛被丢掉的条数。自检报告不输出它，但测试用它证明过滤真的生效。
    std::uint32_t droppedCount() const noexcept;
    std::uint32_t writtenCount() const noexcept;

private:
    LogLevel m_minLevel = LogLevel::Warn;
    Sink m_sink = nullptr;
    void* m_user = nullptr;
    std::uint32_t m_dropped = 0;
    std::uint32_t m_written = 0;
};

// 服务表本体。Kernel 持有一个，模块通过各自的视图访问。
class ServiceTable final {
public:
    ServiceTable() = default;
    ServiceTable(const ServiceTable&) = delete;
    ServiceTable& operator=(const ServiceTable&) = delete;
    ~ServiceTable() = default;

    // ownerId 为空表示 Kernel 自己发布。
    Error publishFor(const char* ownerId, const char* name, int major, void* service) noexcept;

    // ownerId 不为空时，只能撤销自己的发布。
    Error withdrawFor(const char* ownerId, const char* name, int major) noexcept;

    // 撤销某个 owner 的全部发布，返回撤销条数。Kernel 在 shutdown 时调用。
    std::size_t withdrawAllFor(const char* ownerId) noexcept;

    void* find(const char* name, int major) const noexcept;

    std::size_t size() const noexcept;

    void setLogger(ILogger* logger) noexcept;

private:
    struct Entry {
        const char* name = nullptr;
        int major = 0;
        void* service = nullptr;
        const char* owner = nullptr;
        bool used = false;
    };

    Entry m_entries[kMaxServiceEntries];
    std::size_t m_size = 0;
    ILogger* m_logger = nullptr;
};

// 交给单个模块的 IServiceRegistry 视图，owner 即该模块的 ID。
//
// 按值放在模块槽位里（Kernel 不做动态分配），所以默认构造是空视图，
// 由 Kernel 在加载时 bind() 上去。未 bind 的视图对任何操作返回 Refused / nullptr。
class ModuleServiceView final : public bbcoop::IServiceRegistry {
public:
    ModuleServiceView() = default;
    ModuleServiceView(const ModuleServiceView&) = delete;
    ModuleServiceView& operator=(const ModuleServiceView&) = delete;
    ~ModuleServiceView() override = default;

    void bind(ServiceTable& table, const char* ownerId) noexcept;

    void* find(const char* service_name, int version_major) noexcept override;
    Error publish(const char* service_name, int version_major, void* service) noexcept override;
    Error withdraw(const char* service_name, int version_major) noexcept override;

    const char* ownerId() const noexcept;

private:
    ServiceTable* m_table = nullptr;
    const char* m_ownerId = nullptr;
};

}  // namespace bbcoop::kernel
