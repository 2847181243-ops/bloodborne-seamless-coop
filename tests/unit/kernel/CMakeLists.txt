# T-006 Kernel 单元测试：3 个夹具 + 4 个单元测试。
#
# 卡（任务书 §41.1 T-006）要求「4 个单元测试」覆盖**加载 / 故障隔离 / 熔断 / 健康聚合**，
# 所以这四个各建一个可执行目标 —— 这样验收是「数得出来」的，而不是靠读一个装满用例的大文件。
#
# 第 5 个（host_services_test）不在卡的四项里：它覆盖**事件总线**与**日志**，
# 那是任务书 §3.6 原则 1 里 Kernel 的另外两项职责。不并进前四个（会让「4 个用例覆盖
# 4 个领域」名不副实），也不删掉（那是为了对齐一个数字而丢覆盖率）。
#
# 三个夹具在 tests/fixtures/fixture_modules.h（正常 / init 失败 / tick 可注入慢与失败），
# 另含 IAuxLiveness 的假实现（07-kernel-seam §1.6 要求），故障注入（T-013）可以复用同一套。
set(KERNEL_TEST_SOURCES
    load_test.cpp
    fault_isolation_test.cpp
    circuit_breaker_test.cpp
    health_aggregation_test.cpp
    host_services_test.cpp)

foreach(_src IN LISTS KERNEL_TEST_SOURCES)
    get_filename_component(_name ${_src} NAME_WE)
    add_executable(${_name} ${_src})
    # 断言框架在 tests/unit/ 下；夹具在 tests/fixtures/ 下。
    target_include_directories(${_name} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_CURRENT_SOURCE_DIR}/../../fixtures)
    target_link_libraries(${_name} PRIVATE bbcoop_warnings bloodcoop::kernel)
endforeach()

# ⚠️ 测试注册**逐条写明**，不要用循环生成。
#    为什么：CI 的构建门禁里有一条断言核对「`add_test` 声明几条 vs ctest 注册几条」，
#    用来抓「add_test 写在 enable_testing() 之前被 CMake 静默丢弃」那个坑。
#    它的前提是「一条 add_test( 语句 = 一条注册」—— 用循环生成时，
#    源码里只有 1 条语句却注册了 5 条，断言会把**正常的构建**判成「有测试被静默丢弃」。
#    这不是断言的缺陷，而是「可数性」这个前提必须被满足：注册就必须逐条可数。
add_test(NAME load_test COMMAND load_test)
add_test(NAME fault_isolation_test COMMAND fault_isolation_test)
add_test(NAME circuit_breaker_test COMMAND circuit_breaker_test)
add_test(NAME health_aggregation_test COMMAND health_aggregation_test)
add_test(NAME host_services_test COMMAND host_services_test)
