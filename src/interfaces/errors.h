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

namespace bbcoop {

// 模块操作的结果。
//
// 刻意不区分「暂时失败」与「永久失败」：是否需要重试由 Kernel 的熔断策略决定
// （T-005），接口层不替它做判断。
enum class Error : std::int32_t {
    Ok = 0,

    // 模块当前不可用：未实现、依赖缺失、或已被熔断。
    // 骨架模块在实现之前一律返回它（T-007 验收），且**不得因此崩溃**。
    Unavailable = 1,

    NotInitialized = 2,
    InvalidArgument = 3,

    // 超过 Kernel 允许的单次调用时长。阈值由 T-005 确定。
    Timeout = 4,

    // 被策略拒绝：当前状态不允许该操作，且不是错误用法。
    Refused = 5,

    // 安全硬依赖不满足（§3.6 原则 6）。
    // **调用方不得把它当作「可以降级」的信号** —— 正确反应是拒绝联机，
    // 而不是改用明文或跳过校验。
    CryptoUnavailable = 6,

    // 模块内部错误。Kernel 据此把它标记为不可用，但不影响其他模块。
    Internal = 7,
};

// 失败判断统一走这里，避免各处写 `!= Error::Ok` 时漏掉取反。
inline constexpr bool failed(Error e) noexcept { return e != Error::Ok; }

inline constexpr const char* to_string(Error e) noexcept {
    switch (e) {
        case Error::Ok: return "Ok";
        case Error::Unavailable: return "Unavailable";
        case Error::NotInitialized: return "NotInitialized";
        case Error::InvalidArgument: return "InvalidArgument";
        case Error::Timeout: return "Timeout";
        case Error::Refused: return "Refused";
        case Error::CryptoUnavailable: return "CryptoUnavailable";
        case Error::Internal: return "Internal";
    }
    return "Unknown";
}

}  // namespace bbcoop
