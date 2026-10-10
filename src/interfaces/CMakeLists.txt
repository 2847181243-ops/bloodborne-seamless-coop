# 接口层目标（T-004 / T-009）。
#
# INTERFACE 库：没有源文件，只有头文件与用法要求。
# 模块通过链接它来获得 include 路径，而不是用相对路径 ../interfaces/ 互相找。
add_library(bbcoop_interfaces INTERFACE)
add_library(bloodcoop::interfaces ALIAS bbcoop_interfaces)

target_include_directories(bbcoop_interfaces INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>)

target_compile_features(bbcoop_interfaces INTERFACE cxx_std_20)

# 接口层必须能独立编译：不依赖 kernel/ 或 modules/ 下的任何头文件。
# 如果哪天有人在这里加了这样的依赖，T-004 的验收就破了 ——
# 由 tests/contract 里的独立编译用例兜住。
