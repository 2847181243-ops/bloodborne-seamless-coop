// T-021 单元测试用的极小断言框架（不引入第三方依赖）。
// owner: AI-00（T-021）。
//
// 只做两件事：记录检查数、打印失败位置。测试失败时进程返回非零。

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
