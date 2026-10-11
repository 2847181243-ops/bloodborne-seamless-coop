# T-005 Kernel 单元测试（T-006 会把它扩成 3 夹具 + 4 用例的完整套件）。
add_executable(bbcoop_kernel_tests
    test_kernel.cpp)

# 断言框架在 tests/unit/ 下；夹具在 tests/fixtures/ 下（07-kernel-seam §1.6 要求
# IAuxLiveness 的假实现放在那里，好让 T-013 的故障注入也用同一套）。
target_include_directories(bbcoop_kernel_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../../fixtures)

target_link_libraries(bbcoop_kernel_tests
    PRIVATE bbcoop_warnings bloodcoop::kernel)

add_test(NAME bbcoop_kernel_tests COMMAND bbcoop_kernel_tests)
