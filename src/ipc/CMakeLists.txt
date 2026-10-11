# T-021：IPC 层（基础设施，不是 15 个模块之一）。
#
# 依赖方向：ipc -> interfaces（单向）。Kernel（T-005）会依赖 ipc。
# 每个目标都必须链接 bbcoop_warnings —— 编译标准只能在 bbcoop_warnings 里定义。
add_library(bbcoop_ipc STATIC
    src/ring_buffer.cpp
    src/liveness_registry.cpp
    src/supervisor.cpp
    src/log_sink.cpp
    src/watchdog.cpp
    src/proxy.cpp
    src/stub.cpp
    src/call_dispatch.cpp
    src/shared_channel.cpp
    # 平台适配（K-10：机制收在 T-021 内的窄平台层）。
    # 两个文件都自带 #ifdef _WIN32 的降级分支，因此在非 Windows 上也能编译，
    # 只是返回 PlatformUnavailable / nullptr。
    src/shm_mapping_win32.cpp
    src/process_host_win32.cpp)
add_library(bloodcoop::ipc ALIAS bbcoop_ipc)

target_include_directories(bbcoop_ipc PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)

target_link_libraries(bbcoop_ipc
    PRIVATE bbcoop_warnings
    PUBLIC bloodcoop::interfaces)

# Win32 映射的 DACL 用 ConvertStringSecurityDescriptorToSecurityDescriptorW（advapi32）。
# MSVC 与 MinGW 的默认链接集里都有 advapi32，但显式写出来，避免以后换工具链才发现。
if(WIN32)
    target_link_libraries(bbcoop_ipc PRIVATE advapi32)
endif()

target_compile_features(bbcoop_ipc PUBLIC cxx_std_20)
