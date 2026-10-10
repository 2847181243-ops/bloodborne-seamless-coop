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

#include "version.h"

namespace bbcoop {

enum class LogLevel : std::int32_t { Debug = 0, Info = 1, Warn = 2, Error = 3 };

// Kernel 提供给模块的日志通道。
//
// 为什么由 Kernel 提供而不是各模块各写各的：日志要能按模块过滤、
// 要能异步写入而不阻塞主线程（任务书 §34）。模块自己开文件就做不到这两点。
//
// **不得记录任何隐私数据**（任务书 §45 / SECURITY.md §2）：
// 不支持传任意对象，只接受格式化好的字符串，就是为了让这件事在类型层面就难做错。
class ILogger {
public:
    ILogger() = default;
    virtual ~ILogger() = default;
    ILogger(const ILogger&) = delete;
    ILogger& operator=(const ILogger&) = delete;

    virtual void log(LogLevel level, const char* module_id,
                     const char* message) noexcept = 0;
};

// 服务查找。
//
// 模块需要另一个模块的能力时**不能 include 对方的头文件**
// （§3.6 硬性禁止跨模块 include），只能按名字与版本向 Kernel 要。
class IServiceRegistry {
public:
    IServiceRegistry() = default;
    virtual ~IServiceRegistry() = default;
    IServiceRegistry(const IServiceRegistry&) = delete;
    IServiceRegistry& operator=(const IServiceRegistry&) = delete;

    // 返回 nullptr 表示该服务当前不可用。
    // **调用方必须处理 nullptr** —— 依赖缺失是正常状态，不是异常
    // （§3.6 原则 5：故障不传播）。
    virtual void* find(const char* service_name, int version_major) noexcept = 0;
};

struct HostServices {
    ILogger* logger = nullptr;
    IServiceRegistry* services = nullptr;
};

struct ModuleContext {
    // 与任务书 §3.6 注册表逐字一致的模块 ID。
    const char* module_id = nullptr;
    HostServices* host = nullptr;
};

// 类型安全的查找助手。
//
// 能力接口自己声明 kServiceName / kServiceVersionMajor，所以调用方写
//     auto* t = find_service<ITransport>(*ctx.host);
// 而不需要知道 ITransport 在哪个模块里实现。
template <typename T>
T* find_service(HostServices& host) noexcept {
    if (host.services == nullptr) {
        return nullptr;
    }
    return static_cast<T*>(host.services->find(T::kServiceName, T::kServiceVersionMajor));
}

}  // namespace bbcoop
