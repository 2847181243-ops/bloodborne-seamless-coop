// T-021：ChannelMapping 的平台实现（Win32；非 Windows 返回 PlatformUnavailable）。
// owner: AI-00（T-021）。
//
// 关键点：
//   * 创建时挂 SECURITY_ATTRIBUTES：DACL 只放行所有者 / Local System / Administrators，
//     避免同会话里任意进程都能打开这块内存（见 shm_mapping.h 的安全姿态说明）；
//   * 布局在共享内存里用 placement new 构造一次（创建者），打开者只校验后解释；
//   * 打开者校验魔数 / 布局版本 / 布局尺寸，任一不符 → LayoutMismatch，fail-closed。

#include "bbcoop/ipc/shm_mapping.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <sddl.h>

#include <new>
#endif

namespace bbcoop::ipc {
namespace {

#ifdef _WIN32
// 所有者 + Local System + Administrators 可完全访问；DACL 受保护（P）。
constexpr const wchar_t* kMappingSddl = L"D:P(A;;GA;;;OW)(A;;GA;;;SY)(A;;GA;;;BA)";
#endif

}  // namespace

ChannelMapping::~ChannelMapping() noexcept {
    close();
}

void ChannelMapping::close() noexcept {
#ifdef _WIN32
    if (m_view != nullptr) {
        UnmapViewOfFile(m_view);
        m_view = nullptr;
    }
    if (m_handle != nullptr) {
        CloseHandle(static_cast<HANDLE>(m_handle));
        m_handle = nullptr;
    }
#endif
    m_layout = nullptr;
}

#ifdef _WIN32

std::unique_ptr<ChannelMapping> ChannelMapping::create(const wchar_t* name,
                                                       MappingResult& result) noexcept {
    result = MappingResult::Failed;
    if (name == nullptr || name[0] == L'\0') {
        result = MappingResult::InvalidArgument;
        return nullptr;
    }

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (ConvertStringSecurityDescriptorToSecurityDescriptorW(kMappingSddl, SDDL_REVISION_1, &sd,
                                                             nullptr) == FALSE) {
        return nullptr;  // 拿不到受限 DACL 就不建映射，不退回「默认权限」
    }
    sa.lpSecurityDescriptor = sd;

    const DWORD size = static_cast<DWORD>(sizeof(SharedChannelLayout));
    HANDLE handle = CreateFileMappingW(INVALID_HANDLE_VALUE, &sa, PAGE_READWRITE, 0, size, name);
    const DWORD createError = GetLastError();
    LocalFree(sd);
    if (handle == nullptr) {
        return nullptr;
    }
    if (createError == ERROR_ALREADY_EXISTS) {
        // 同名映射已经存在：可能是上一次运行残留，也可能是别的东西占了名字。
        // **不能接管**：接管就要初始化布局，那会抹掉对方的握手与环形状态。
        CloseHandle(handle);
        result = MappingResult::AlreadyExists;
        return nullptr;
    }

    void* view = MapViewOfFile(handle, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    if (view == nullptr) {
        CloseHandle(handle);
        return nullptr;
    }

    // 创建者负责构造布局：值初始化把全部原子置零（映射页本身也是零填充）。
    auto* layout = new (view) SharedChannelLayout{};
    initSharedLayout(*layout);

    std::unique_ptr<ChannelMapping> mapping(new (std::nothrow) ChannelMapping());
    if (!mapping) {
        UnmapViewOfFile(view);
        CloseHandle(handle);
        return nullptr;
    }
    mapping->m_handle = handle;
    mapping->m_view = view;
    mapping->m_layout = layout;
    result = MappingResult::Ok;
    return mapping;
}

std::unique_ptr<ChannelMapping> ChannelMapping::open(const wchar_t* name,
                                                     MappingResult& result) noexcept {
    result = MappingResult::Failed;
    if (name == nullptr || name[0] == L'\0') {
        result = MappingResult::InvalidArgument;
        return nullptr;
    }

    HANDLE handle = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, name);
    if (handle == nullptr) {
        const DWORD error = GetLastError();
        result = (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
                     ? MappingResult::NotFound
                     : MappingResult::Failed;
        return nullptr;
    }

    void* view = MapViewOfFile(handle, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    if (view == nullptr) {
        CloseHandle(handle);
        return nullptr;
    }

    auto* layout = static_cast<SharedChannelLayout*>(view);
    if (!sharedLayoutMatches(*layout)) {
        // fail-closed：版本/尺寸不符的映射一律不使用（D-12 / A-5）。
        UnmapViewOfFile(view);
        CloseHandle(handle);
        result = MappingResult::LayoutMismatch;
        return nullptr;
    }

    std::unique_ptr<ChannelMapping> mapping(new (std::nothrow) ChannelMapping());
    if (!mapping) {
        UnmapViewOfFile(view);
        CloseHandle(handle);
        return nullptr;
    }
    mapping->m_handle = handle;
    mapping->m_view = view;
    mapping->m_layout = layout;
    result = MappingResult::Ok;
    return mapping;
}

#else

std::unique_ptr<ChannelMapping> ChannelMapping::create(const wchar_t* name,
                                                       MappingResult& result) noexcept {
    (void)name;
    result = MappingResult::PlatformUnavailable;
    return nullptr;
}

std::unique_ptr<ChannelMapping> ChannelMapping::open(const wchar_t* name,
                                                     MappingResult& result) noexcept {
    (void)name;
    result = MappingResult::PlatformUnavailable;
    return nullptr;
}

#endif  // _WIN32

std::size_t formatChannelMappingName(ChannelId channel, wchar_t* out,
                                     std::size_t outSize) noexcept {
    if (out == nullptr || outSize <= kMappingNamePrefixLength + 1) {
        return 0;
    }

    std::size_t pos = 0;
    for (std::size_t i = 0; i < kMappingNamePrefixLength; ++i) {
        out[pos++] = kMappingNamePrefix[i];
    }

    wchar_t digits[10];  // u32 最多 10 位十进制
    std::size_t count = 0;
    std::uint32_t value = channel.value;
    do {
        digits[count++] = static_cast<wchar_t>(L'0' + (value % 10U));
        value /= 10U;
    } while (value != 0U);

    if (pos + count + 1 > outSize) {
        return 0;
    }
    while (count > 0) {
        out[pos++] = digits[--count];
    }
    out[pos] = L'\0';
    return pos;
}

}  // namespace bbcoop::ipc
