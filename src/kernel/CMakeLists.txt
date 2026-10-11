# Kernel（模块宿主）—— T-005。
#
# ⚠️ 目标结构与模块分派**不在这里**：`07-kernel-seam.md` §2 已裁决「方案 B」
#    （两个可执行目标 + 按 placement 分派 + 配置阶段断言隔离），那是 **T-009** 的范围。
#    草案分支 `wip/kernel-draft` 的 CMakeLists 里有一套 `bbcoop_add_module()` 与
#    隔离断言，**没有搬过来**，理由有两条：
#      1. 它把每个模块**无条件**链进 `bloodcoop_host`，而 `mod.transport` 这类模块
#         属进程外（06 §1）—— 照搬等于把进程外模块放进游戏进程映像；
#      2. 它的隔离断言只在 `bbcoop_add_module()` 返回前扫一遍目标清单，
#         **最后一次扫描之后加的链接不会被检查**（07 §3 记录了这个洞，实测复现过）。
#    这两条都要在 T-009 里按「注入非法链接后配置阶段必须失败」重做。
#
# 本文件只建 T-005 需要的两个目标：
#   bbcoop_kernel   —— Kernel 库（不依赖任何模块：删掉任意模块目录都还能构建）
#   bloodcoop-host  —— 宿主可执行文件（--self-test）
#
# 依赖方向：kernel -> ipc -> interfaces（单向；08-ipc.md §7 明写是为了避免 CMake 环）。
add_library(bbcoop_kernel STATIC
    src/kernel.cpp
    src/host_services.cpp
    src/event_bus.cpp
    src/self_test.cpp)
add_library(bloodcoop::kernel ALIAS bbcoop_kernel)

target_include_directories(bbcoop_kernel PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)

# bloodcoop::ipc 是 PUBLIC：Kernel 的公开头文件里出现 ipc::ChannelId / IAuxLiveness，
# 消费者（宿主与测试）也要能看到它们。
target_link_libraries(bbcoop_kernel
    PRIVATE bbcoop_warnings
    PUBLIC bloodcoop::interfaces bloodcoop::ipc)

target_compile_features(bbcoop_kernel PUBLIC cxx_std_20)

add_executable(bloodcoop-host host/main.cpp)
target_link_libraries(bloodcoop-host PRIVATE bbcoop_warnings bloodcoop::kernel)

# 版本串由构建注入，而不是在源码里写死 —— 自检 JSON 的 version 字段用它。
target_compile_definitions(bloodcoop-host PRIVATE
    BBCOOP_VERSION_STRING="${PROJECT_VERSION}")
