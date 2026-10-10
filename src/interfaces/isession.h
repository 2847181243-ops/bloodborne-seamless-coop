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
#include "version.h"

namespace bbcoop {

// 会话能力。由 `mod.session_core` 提供（任务书 §3.6 注册表）。
//
// 注意层级：`mod.session_core` 在 Layer 0，而复制相关的能力在 Layer 1 的
// `mod.replication_core`。这两者在 v1.0 的任务书里原本属于同一个 Agent（AI-A2），
// 拆分后原来的「内部接口」变成了**跨层接口**（任务书附录 A 特别指出这一点），
// 所以本接口只暴露 Layer 0 能负责的部分。
class ISession {
public:
    static constexpr const char* kServiceName = "bbcoop.session";
    static constexpr int kServiceVersionMajor = 1;

    ISession() = default;
    virtual ~ISession() = default;
    ISession(const ISession&) = delete;
    ISession& operator=(const ISession&) = delete;

    // 会话是否已建立（含握手与密钥交换完成）。
    virtual bool active() const noexcept = 0;

    // 当前成员数，含本机。
    virtual std::uint32_t member_count() const noexcept = 0;

    // 本机是否为 Host。
    //
    // 任务书 §27 规定 Authority 模型下由 Host 裁决。
    // 这个查询存在是因为「谁说了算」会改变很多模块的行为，
    // 而它不该由各模块自己猜。
    virtual bool is_host() const noexcept = 0;
};

}  // namespace bbcoop
