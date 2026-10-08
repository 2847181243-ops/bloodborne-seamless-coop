# Bloodborne Seamless Co-op Mod — 顶层构建脚本
#
# ⚠️ 这个文件目前**没有任何源文件**：项目还在 Phase 0（安全设计）阶段。
#    它的作用是**在写第一行代码之前**把编译标准的闸门立好。
#    理由：零警告现在是免费的；等有了几千行代码再补 `/WX`，就得一次修几百个警告，
#    那时人就会开始加 `/wd` 把警告关掉 —— 那才是真正的技术债起点。
#
# 改动本文件前先读 docs/standards/build-standards.md。
# `tools/preci/check_style.py` 会核对本文件里的关键开关是否还在，
# 防止有人为了让构建通过而**悄悄削掉**警告等级。

cmake_minimum_required(VERSION 3.21)

# ── 生成器：默认沿用 CI 使用的 Visual Studio 生成器 ──────────────────────────
#
# CI 的 .github/workflows/build.yml 用的是：
#     cmake -S . -B build -A x64 -DCMAKE_BUILD_TYPE=Release
# `-A x64` 只有 Visual Studio 生成器支持，**Ninja 不支持**。
#
# 所以我们**不**为了 compile_commands.json 去改 CI 的构建命令 —— 那会拿
# 现有构建门禁去换一个尚未用到的静态分析能力，不划算。
#
# 需要 clang-tidy 时（它要求 compile_commands.json，而该文件只有
# Makefile/Ninja 生成器会产出）显式开 Ninja：
#
#     cmake -S . -B build-ninja -G Ninja -DCMAKE_BUILD_TYPE=Release \
#           -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl
#
# 注意：用 Ninja + cl 之前必须先进入 MSVC 开发者环境
# （运行 vcvars64.bat，或在 CI 里用 microsoft/setup-msbuild 之类先配好环境），
# 否则找不到 cl.exe。本机目前**没有装 cmake 与 MSVC**（见 build-standards.md）。

project(BloodborneSeamlessCoop
    VERSION 0.1.0
    DESCRIPTION "Bloodborne Seamless Co-op Mod"
    LANGUAGES CXX)

# ── C++ 标准 ────────────────────────────────────────────────────────────────
# C++20；不启用编译器扩展（/permissive- 与此配合，防止 MSVC 的宽松解析）。
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 默认 Release；CI 显式传 -DCMAKE_BUILD_TYPE=Release，本地不带也能构建。
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# ── compile_commands.json ───────────────────────────────────────────────────
# clang-tidy 需要它才能知道每个源文件用什么参数编译。
#
# ⚠️ 只有 Makefile 与 Ninja 生成器会产出它；Visual Studio 生成器**不会**
#    （CMake 官方文档明确说明）。所以默认的 VS 生成器下这个设置是空转，
#    需要静态分析时改用 Ninja。
#
#    本地没有 MSVC 时，可以用 setup-toolchain.ps1 装的 zig 当编译器：
#
#        powershell -NoProfile -File tools/toolchain/setup-toolchain.ps1
#        cmake -S . -B build-ninja -G Ninja -DCMAKE_BUILD_TYPE=Release \
#              "-DCMAKE_CXX_COMPILER=<python 路径>;<仓库>/tools/toolchain/zig-cxx.py"
#        clang-tidy -p build-ninja <源文件>
#
#    ⚠️ zig 不是 MSVC，ABI 不同。本地只适合冒烟测试，权威构建在 CI。
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 产物统一放到 bin/，方便 CI 的 artifact 上传与本地查找。
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# ── 关键：把编译标准做成一个「只能加强、不能削弱」的接口目标 ─────────────────
#
# 用法：以后每个模块都 `target_link_libraries(<target> PRIVATE bbcoop_warnings)`。
# 这样标准**在一次地方定义**，模块不会各自为政地决定要不要警告。
add_library(bbcoop_warnings INTERFACE)

if(MSVC)
    # ── 警告等级：4 级（最高常用等级）+ 警告即错误 ────────────────────────
    # /W4   打开绝大多数有用警告（/Wall 噪音过大，不可用）
    # /WX   任何警告都当错误 —— 这一条是「零警告」的真正执行者。
    #       没有 /WX，/W4 只是建议。
    target_compile_options(bbcoop_warnings INTERFACE /W4 /WX)

    # ── 标准符合性 ────────────────────────────────────────────────────────
    # /permissive-  关闭 MSVC 的宽松解析，向标准靠拢。
    #               代价：某些旧代码写不出来（那正是我们要的）。
    target_compile_options(bbcoop_warnings INTERFACE
        $<$<COMPILE_LANGUAGE:CXX>:/permissive->)

    # ── 源码编码 ──────────────────────────────────────────────────────────
    # /utf-8  源码与执行字符集都按 UTF-8。
    #         没有它，MSVC 会按系统代码页（本机 GBK）解释源文件里的中文注释与
    #         字符串 —— 这正是本项目历史上踩过的坑（GBK 误码入库）。
    target_compile_options(bbcoop_warnings INTERFACE /utf-8)

    # ── 外部头文件不产生警告 ──────────────────────────────────────────────
    # /external:W0 /external:anglebrackets
    #   把 <...> 形式的第三方头文件当外部代码，不报它们的警告。
    #   这**不是**为了掩盖自己的问题：没有它，Boost/Detours 之类的头文件会
    #   把 /WX 直接顶爆，人就只能去关掉整个警告等级。
    #   ⚠️ 它**不管** /analyze；代码分析要另外用 /analyze:external- 或 CAExcludePath。
    target_compile_options(bbcoop_warnings INTERFACE
        /external:W0 /external:anglebrackets)

    # ── 链接期加固 ────────────────────────────────────────────────────────
    # /guard:cf        控制流保护
    # /DYNAMICBASE     ASLR
    # /NXCOMPAT        数据执行保护（DEP）
    # /HIGHENTROPYVA   64 位高位随机化
    # 这些都是「默认值也可能已开，但显式写出来才不会被某次配置改动悄悄关掉」。
    target_link_options(bbcoop_warnings INTERFACE
        /guard:cf /DYNAMICBASE /NXCOMPAT /HIGHENTROPYVA)
else()
    # 非 MSVC（例如 Linux 下用 Clang 做静态分析）时的等价设置。
    # 本项目**发布目标只有 Windows MSVC x64**；这里只是为了本机分析方便。
    target_compile_options(bbcoop_warnings INTERFACE
        -Wall -Wextra -Werror
        $<$<COMPILE_LANGUAGE:CXX>:-Wpedantic>)
endif()

# ── 模块 ────────────────────────────────────────────────────────────────────
#
# 源码尚不存在（Phase 0）。以下目录**故意**先不 add_subdirectory，
# 否则 CMake 会因为目录为空而报错，构建门禁会红。
# 每个模块落地时按下表加入，并**必须**链接 bbcoop_warnings：
#
#   mod/core/          AI-A1   HookManager / AddressResolver / VersionChecker / Logger
#   mod/platform/      AI-A0   bbport 适配 / 启动器集成
#   mod/session/       AI-A2   会话与状态复制
#   mod/replication/   AI-A2   状态复制
#   mod/gameplay/      AI-A3   玩家 / 敌人 / Boss / 死亡 / Lantern / 世界
#   mod/balance/       AI-A4   数值与 PvP
#   mod/security/      AI-C1   E2EE / 密钥交换 / 完整性校验
#   mod/network/       AI-B1..B3  传输 / 发现 / 信誉
#   relay/             AI-B4   中继
#
# 加入时写成：
#     add_subdirectory(mod/core)
# 并在该模块的 CMakeLists.txt 里写：
#     target_link_libraries(<target> PRIVATE bbcoop_warnings)
#
# 做「编译标准是否生效」的自检：配置阶段就断言 /WX 在位。
# 这样一旦有人删掉它，配置阶段就会失败，而不是等到某天发现警告变多了。
if(MSVC)
    get_target_property(_warn_opts bbcoop_warnings INTERFACE_COMPILE_OPTIONS)
    set(_flat "${_warn_opts}")
    if(NOT _flat MATCHES "/WX")
        message(FATAL_ERROR
            "编译标准被削弱了：bbcoop_warnings 里找不到 /WX。"
            "读 docs/standards/build-standards.md 再改，不要为了让构建通过而关掉警告。")
    endif()
    if(NOT _flat MATCHES "/W4")
        message(FATAL_ERROR
            "编译标准被削弱了：bbcoop_warnings 里找不到 /W4。见 build-standards.md。")
    endif()
endif()

message(STATUS "Bloodborne Seamless Co-op Mod ${PROJECT_VERSION}")
message(STATUS "  C++ 标准        : CXX_STANDARD=${CMAKE_CXX_STANDARD}  扩展关闭")
message(STATUS "  编译器          : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  构建类型        : ${CMAKE_BUILD_TYPE}")
message(STATUS "  警告标准        : MSVC 下为 /W4 /WX /permissive- /utf-8（见 bbcoop_warnings）")
message(STATUS "  产物目录        : ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
