# T-021：IPC 层（基础设施，不是 15 个模块之一）。
#
# 依赖方向：ipc -> interfaces（单向）。Kernel（T-005）会依赖 ipc。
# 每个目标都必须链接 bbcoop_warnings —— 编译标准只能在 bbcoop_warnings 里定义。
add_library(bbcoop_ipc STATIC
    src/ring_buffer.cpp
    src/liveness_registry.cpp
    src/supervisor.cpp
    src/log_sink.cpp)
add_library(bloodcoop::ipc ALIAS bbcoop_ipc)

target_include_directories(bbcoop_ipc PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)

target_link_libraries(bbcoop_ipc
    PRIVATE bbcoop_warnings
    PUBLIC bloodcoop::interfaces)

target_compile_features(bbcoop_ipc PUBLIC cxx_std_20)
