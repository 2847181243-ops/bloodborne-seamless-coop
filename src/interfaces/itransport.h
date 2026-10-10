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

#include <cstddef>
#include <cstdint>

#include "errors.h"
#include "version.h"

namespace bbcoop {

// 传输能力。由 `mod.transport` 提供（任务书 §3.6 注册表）。
//
// 刻意保持很小：任务书 §29 详细规定了传输层的设计，但那些细节属于模块实现，
// 不是模块间契约。契约只需要「别人怎么用它」。
class ITransport {
public:
    static constexpr const char* kServiceName = "bbcoop.transport";
    static constexpr int kServiceVersionMajor = 1;

    ITransport() = default;
    virtual ~ITransport() = default;
    ITransport(const ITransport&) = delete;
    ITransport& operator=(const ITransport&) = delete;

    // 连接是否已建立且可用。
    //
    // **返回 false 时调用方不得假定还存在别的可用路径**。
    // 尤其：不得因此改用未加密的传输（§3.6 原则 6）。
    virtual bool connected() const noexcept = 0;

    // 发送一段数据。返回 Ok 只表示已交给传输层，**不表示对端已收到**。
    // 可靠性由上层协议负责，不在本接口承诺范围内。
    virtual Error send(const std::uint8_t* data, std::size_t size) noexcept = 0;

    // 往返时延估算，单位毫秒。未知时返回 0 —— 上限与采样方式
    // **待 T-005/T-013 确定**。
    virtual std::uint32_t rtt_ms() const noexcept = 0;
};

}  // namespace bbcoop
