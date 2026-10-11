// 单元测试用的极小断言框架（不引入第三方依赖）。
// owner: AI-00（T-021 起；T-005 起两个套件共用，因此放在 tests/unit/ 下）。
//
// 只做两件事：记录检查数、打印失败位置。测试失败时进程返回非零。
//
// 为什么放在共同的上层目录：T-021 与 T-005 各有一个测试目标，
// 共用一份断言宏比每个目录复制一份更不容易漂移（复制品会各自被改）。
// 每个测试目标的 CMakeLists 把 `${CMAKE_CURRENT_SOURCE_DIR}/..` 加进 include 路径。

#pragma once

#include <cstdio>

namespace bbcoop::test {

inline int g_failures = 0;
inline int g_checks = 0;

inline void report(bool ok, const char* expr, const char* file, int line) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("[FAIL] %s:%d: %s\n", file, line, expr);
    }
}

}  // namespace bbcoop::test

#define CHECK(cond) ::bbcoop::test::report((cond), #cond, __FILE__, __LINE__)
#define CHECK_EQ(a, b) \
    ::bbcoop::test::report((a) == (b), #a " == " #b, __FILE__, __LINE__)
