# T-021 单元测试（T-012 会把它接进分层 CI）。
#
# 单一可执行文件里同时含有跨进程用例：它会用 --aux-child 参数把自己再启动一次，
# 充当真实的辅助进程（见 test_channel.cpp）。因此这里不需要第二个目标。
add_executable(bbcoop_ipc_tests
    test_main.cpp
    test_channel.cpp)

target_link_libraries(bbcoop_ipc_tests
    PRIVATE bbcoop_warnings bloodcoop::ipc)

add_test(NAME bbcoop_ipc_tests COMMAND bbcoop_ipc_tests)
