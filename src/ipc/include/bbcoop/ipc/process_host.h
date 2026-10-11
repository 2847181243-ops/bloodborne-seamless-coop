// T-021：辅助进程的平台监督接口（08-ipc.md §5.3 / D-9）。
// owner: AI-00（T-021）。
//
// 分工（K-10）：Kernel 只发「请求终止」；本接口是执行面的最小机制：
//   * 启动（挂起后加入作业对象，再恢复，避免逃逸）；
//   * 「已死」以可等待的进程状态为准（REF-001 A4）；
//   * 终止按作业对象整组回收，失败回退单进程；不用 PID 复用做身份判断。
// 所有方法 noexcept：实现内部不得让异常逃出（New 失败用 nothrow）。

#pragma once

#include <cstdint>
#include <memory>

#include "errors.h"

namespace bbcoop::ipc {

struct ProcessId {
    std::uint32_t value = 0;

    friend constexpr bool operator==(ProcessId a, ProcessId b) noexcept {
        return a.value == b.value;
    }
    friend constexpr bool operator!=(ProcessId a, ProcessId b) noexcept {
        return a.value != b.value;
    }
};

class IProcessHost {
public:
    IProcessHost() = default;
    virtual ~IProcessHost() = default;
    IProcessHost(const IProcessHost&) = delete;
    IProcessHost& operator=(const IProcessHost&) = delete;

    virtual Error start(const wchar_t* exePath, const wchar_t* args,
                        ProcessId& out) noexcept = 0;

    // 可等待状态判定：进程已退出返回 false。
    virtual bool isAlive(ProcessId pid) const noexcept = 0;

    // 到达超时前退出则 exited=true。
    virtual Error waitForExit(ProcessId pid, std::uint32_t timeoutMs,
                              bool& exited) noexcept = 0;

    // 整组回收（作业对象）；不可用时回退为单进程终止。
    virtual Error terminate(ProcessId pid) noexcept = 0;

    // 释放句柄；此后该 pid 无效。
    virtual void release(ProcessId pid) noexcept = 0;
};

// Windows 返回实现；其它平台返回 nullptr（T-021 只发布 Windows 目标）。
std::unique_ptr<IProcessHost> createPlatformProcessHost() noexcept;

}  // namespace bbcoop::ipc
