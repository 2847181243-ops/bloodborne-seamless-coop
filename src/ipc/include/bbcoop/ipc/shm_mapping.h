// T-021：共享内存通道的命名映射（08-ipc.md D-1/D-12）。
// owner: AI-00（T-021）。
//
// 分工：宿主侧 `create`（建映射 + 初始化布局），辅助侧 `open`（打开 + 校验布局）。
// 校验不通过一律 fail-closed（D-12）——宁可拒绝启动，也不要两个不同版本的产物互相说话。
//
// 安全姿态（重要，别把它读成「已经安全了」）：
//   * 名字在 `Local\` 命名空间里，对**同会话**的其它进程是可见的 —— 名字本身**不是**
//     安全边界，只是寻址方式；
//   * 真正的门是创建时的 DACL：只放行对象所有者 / Local System / Administrators
//     （见 .cpp 的 kMappingSddl）。同用户的其它进程仍然是「所有者」，所以这**不**
//     阻止同用户下的恶意进程；
//   * 完整加固（受限令牌 + 作业对象 + 不可猜的名字）随启动器落地（T-009，
//     06-process-boundary.md §5.3）。本文件只如实做到 DACL 这一层。

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include "bbcoop/ipc/protocol.h"
#include "bbcoop/ipc/shm_layout.h"

namespace bbcoop::ipc {

// 映射操作的结果。自检报告要能区分「没找到」和「版本不符」（07-kernel-seam §1.5）。
enum class MappingResult : std::uint8_t {
    Ok = 0,
    InvalidArgument = 1,   // 名字/缓冲区不合法
    NotFound = 2,          // 打开时名字不存在（辅助进程比宿主先跑起来）
    LayoutMismatch = 3,    // 魔数/布局版本/尺寸不符 —— A-5 的 fail-closed 路径
    PlatformUnavailable = 4,  // 非 Windows 平台：T-021 只发布 Windows 目标
    AlreadyExists = 5,     // 创建时名字已被占用：不接管、不初始化对方的布局
    Failed = 6,            // 其它系统调用失败
};

constexpr const char* to_string(MappingResult r) noexcept {
    switch (r) {
        case MappingResult::Ok: return "ok";
        case MappingResult::InvalidArgument: return "invalid_argument";
        case MappingResult::NotFound: return "not_found";
        case MappingResult::LayoutMismatch: return "layout_mismatch";
        case MappingResult::PlatformUnavailable: return "platform_unavailable";
        case MappingResult::AlreadyExists: return "already_exists";
        case MappingResult::Failed: return "failed";
    }
    return "unknown";
}

// 命名映射的所有者：析构时解除映射并关闭句柄。两侧用法完全对称。
class ChannelMapping {
public:
    ChannelMapping(const ChannelMapping&) = delete;
    ChannelMapping& operator=(const ChannelMapping&) = delete;
    ~ChannelMapping() noexcept;

    // 宿主侧：创建（或打开已存在的）映射并初始化布局。
    static std::unique_ptr<ChannelMapping> create(const wchar_t* name,
                                                  MappingResult& result) noexcept;

    // 辅助侧：打开已存在的映射，校验布局后返回；校验失败返回 nullptr。
    static std::unique_ptr<ChannelMapping> open(const wchar_t* name,
                                                MappingResult& result) noexcept;

    SharedChannelLayout* layout() const noexcept { return m_layout; }

private:
    ChannelMapping() noexcept = default;
    void close() noexcept;

    void* m_handle = nullptr;                            // HANDLE
    void* m_view = nullptr;                              // 映射视图
    SharedChannelLayout* m_layout = nullptr;
};

// 通道名统一前缀。名字只用于寻址；访问控制由 DACL 负责（见文件头）。
inline constexpr const wchar_t kMappingNamePrefix[] = L"Local\\bbcoop-ipc-";
inline constexpr std::size_t kMappingNamePrefixLength =
    (sizeof(kMappingNamePrefix) / sizeof(wchar_t)) - 1;

// 把通道 id 编成映射名（暂定规则；多实例隔离与不可猜性随 T-009 的启动器再定）。
// 返回写入的字符数（不含结尾 0）；缓冲区不足返回 0。
std::size_t formatChannelMappingName(ChannelId channel, wchar_t* out,
                                     std::size_t outSize) noexcept;

}  // namespace bbcoop::ipc
