# T-021 单元测试（T-012 会把它接进分层 CI）。
add_executable(bbcoop_ipc_tests
    test_main.cpp)

target_link_libraries(bbcoop_ipc_tests
    PRIVATE bbcoop_warnings bloodcoop::ipc)

add_test(NAME bbcoop_ipc_tests COMMAND bbcoop_ipc_tests)
