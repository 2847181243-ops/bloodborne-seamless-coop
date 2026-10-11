// T-021：IProcessHost 的 Win32 实现（08-ipc.md §5.3）。
// owner: AI-00（T-021）。
//
// 关键点（对照 REF-001）：
//   * CREATE_SUSPENDED -> AssignProcessToJobObject -> ResumeThread：先入作业再跑，
//     避免子进程在归属前派生孙进程而逃出回收范围；
//   * JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE：最后一个作业句柄关闭时整组终止，防孤儿；
//   * AssignProcessToJobObject 失败（例如宿主已在不允许嵌套的作业里）时降级为
//     单进程跟踪 —— 如实降级，不假装整组能力仍在；
//   * isAlive/waitForExit 用 WaitForSingleObject，而不是 PID 查询。

#include "bbcoop/ipc/process_host.h"

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace bbcoop::ipc {
namespace {

constexpr std::size_t kMaxProcesses = 4;
constexpr std::size_t kMaxCommandLine = 2048;

}  // namespace

class ProcessHostWin32 final : public IProcessHost {
public:
    ProcessHostWin32() noexcept = default;
    ~ProcessHostWin32() noexcept override {
        for (std::size_t i = 0; i < kMaxProcesses; ++i) {
            closeSlot(m_slots[i]);
        }
    }

    Error start(const wchar_t* exePath, const wchar_t* args,
                ProcessId& out) noexcept override {
        if (exePath == nullptr || args == nullptr) {
            return Error::InvalidArgument;
        }
        Slot* slot = freeSlot();
        if (slot == nullptr) {
            return Error::Unavailable;
        }

        wchar_t cmd[kMaxCommandLine] = {};
        if (!joinCommandLine(exePath, args, cmd, kMaxCommandLine)) {
            return Error::InvalidArgument;
        }

        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        if (CreateProcessW(exePath, cmd, nullptr, nullptr, FALSE,
                           CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr, nullptr,
                           &si, &pi) == FALSE) {
            return Error::Internal;
        }

        // 尝试建立作业对象；失败则降级（见文件头说明）。
        HANDLE job = CreateJobObjectW(nullptr, nullptr);
        if (job != nullptr) {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
            limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            if (SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits,
                                        sizeof(limits)) == FALSE ||
                AssignProcessToJobObject(job, pi.hProcess) == FALSE) {
                CloseHandle(job);
                job = nullptr;
            }
        }

        if (ResumeThread(pi.hThread) == static_cast<DWORD>(-1)) {
            TerminateProcess(pi.hProcess, 1);
            CloseHandle(job);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            return Error::Internal;
        }
        CloseHandle(pi.hThread);

        slot->pid = pi.dwProcessId;
        slot->process = pi.hProcess;
        slot->job = job;
        out.value = pi.dwProcessId;
        return Error::Ok;
    }

    bool isAlive(ProcessId pid) const noexcept override {
        const Slot* slot = find(pid);
        if (slot == nullptr || slot->process == nullptr) {
            return false;
        }
        return WaitForSingleObject(slot->process, 0) == WAIT_TIMEOUT;
    }

    Error waitForExit(ProcessId pid, std::uint32_t timeoutMs,
                      bool& exited) noexcept override {
        exited = false;
        const Slot* slot = find(pid);
        if (slot == nullptr || slot->process == nullptr) {
            return Error::InvalidArgument;
        }
        const DWORD r = WaitForSingleObject(slot->process, timeoutMs);
        if (r == WAIT_OBJECT_0) {
            exited = true;
            return Error::Ok;
        }
        if (r == WAIT_TIMEOUT) {
            return Error::Ok;
        }
        return Error::Internal;
    }

    Error terminate(ProcessId pid) noexcept override {
        Slot* slot = find(pid);
        if (slot == nullptr) {
            return Error::InvalidArgument;
        }
        if (slot->job != nullptr) {
            if (TerminateJobObject(slot->job, 1) == FALSE) {
                return Error::Internal;  // 整组回收失败：如实返回
            }
            return Error::Ok;
        }
        if (slot->process == nullptr || TerminateProcess(slot->process, 1) == FALSE) {
            return Error::Internal;
        }
        return Error::Ok;
    }

    void release(ProcessId pid) noexcept override {
        Slot* slot = find(pid);
        if (slot == nullptr) {
            return;
        }
        closeSlot(*slot);
    }

private:
    struct Slot {
        std::uint32_t pid = 0;
        HANDLE process = nullptr;
        HANDLE job = nullptr;
    };

    static void closeSlot(Slot& slot) noexcept {
        if (slot.job != nullptr) {
            CloseHandle(slot.job);
            slot.job = nullptr;
        }
        if (slot.process != nullptr) {
            CloseHandle(slot.process);
            slot.process = nullptr;
        }
        slot.pid = 0;
    }

    Slot* freeSlot() noexcept {
        for (std::size_t i = 0; i < kMaxProcesses; ++i) {
            if (m_slots[i].process == nullptr) {
                return &m_slots[i];
            }
        }
        return nullptr;
    }

    Slot* find(ProcessId pid) noexcept {
        for (std::size_t i = 0; i < kMaxProcesses; ++i) {
            if (m_slots[i].process != nullptr && m_slots[i].pid == pid.value) {
                return &m_slots[i];
            }
        }
        return nullptr;
    }

    const Slot* find(ProcessId pid) const noexcept {
        for (std::size_t i = 0; i < kMaxProcesses; ++i) {
            if (m_slots[i].process != nullptr && m_slots[i].pid == pid.value) {
                return &m_slots[i];
            }
        }
        return nullptr;
    }

    // 按 CommandLineToArgvW 的规则给一个参数加引号。
    // 为什么不能裸拼：exe 路径里带空格时（本仓库路径就可能有），子进程的 argv
    // 会被拆错位 —— 可执行文件仍能起来（lpApplicationName 是对的），但 argv[1] 变样了。
    static bool appendQuoted(const wchar_t* arg, wchar_t* out, std::size_t outSize,
                             std::size_t& pos) noexcept {
        bool needsQuote = (*arg == L'\0');
        for (const wchar_t* p = arg; *p != L'\0' && !needsQuote; ++p) {
            needsQuote = (*p == L' ' || *p == L'\t' || *p == L'"');
        }

        if (!needsQuote) {
            for (const wchar_t* p = arg; *p != L'\0'; ++p) {
                if (pos + 1 >= outSize) {
                    return false;
                }
                out[pos++] = *p;
            }
            return true;
        }

        if (pos + 1 >= outSize) {
            return false;
        }
        out[pos++] = L'"';

        std::size_t backslashes = 0;
        for (const wchar_t* p = arg; *p != L'\0'; ++p) {
            if (*p == L'\\') {
                ++backslashes;
                continue;
            }
            const std::size_t repeated = (*p == L'"') ? (backslashes * 2 + 1) : backslashes;
            for (std::size_t i = 0; i < repeated; ++i) {
                if (pos + 1 >= outSize) {
                    return false;
                }
                out[pos++] = L'\\';
            }
            backslashes = 0;
            if (pos + 1 >= outSize) {
                return false;
            }
            out[pos++] = *p;
        }

        // 结尾的反斜杠要加倍：它们后面紧跟闭合引号。
        for (std::size_t i = 0; i < backslashes * 2; ++i) {
            if (pos + 1 >= outSize) {
                return false;
            }
            out[pos++] = L'\\';
        }
        if (pos + 1 >= outSize) {
            return false;
        }
        out[pos++] = L'"';
        return true;
    }

    static bool joinCommandLine(const wchar_t* exePath, const wchar_t* args,
                                wchar_t* out, std::size_t outSize) noexcept {
        std::size_t pos = 0;
        if (!appendQuoted(exePath, out, outSize, pos)) {
            return false;
        }
        if (pos + 1 >= outSize) {
            return false;
        }
        out[pos++] = L' ';
        // args 是宿主自己拼好的参数串（可含多个参数），整体不再加引号。
        for (const wchar_t* p = args; *p != L'\0'; ++p) {
            if (pos + 1 >= outSize) {
                return false;
            }
            out[pos++] = *p;
        }
        out[pos] = L'\0';
        return true;
    }

    Slot m_slots[kMaxProcesses];
};

std::unique_ptr<IProcessHost> createPlatformProcessHost() noexcept {
    return std::unique_ptr<IProcessHost>(new (std::nothrow) ProcessHostWin32());
}

}  // namespace bbcoop::ipc

#else

namespace bbcoop::ipc {

std::unique_ptr<IProcessHost> createPlatformProcessHost() noexcept {
    return nullptr;  // 非 Windows 平台：T-021 只发布 Windows 目标
}

}  // namespace bbcoop::ipc

#endif  // _WIN32
