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

namespace bbcoop {

// 接口版本。模块启动时与 Kernel 比对：主版本不同即拒绝加载。
inline constexpr int kInterfaceVersionMajor = 1;
inline constexpr int kInterfaceVersionMinor = 0;

// 便于日志与自检输出的单一数值形式。
inline constexpr int kInterfaceVersion =
    (kInterfaceVersionMajor * 100) + kInterfaceVersionMinor;

inline constexpr const char* kInterfaceVersionString = "1.0";

}  // namespace bbcoop
