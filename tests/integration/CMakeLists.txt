# 集成测试（T-012 起）。
#
# 与 tests/unit/ 的区别：单元测试验的是某个函数/类的行为，这里验的是**一条链路**。
# 目前只有一条：`bloodcoop-host --self-test`。
add_test(NAME bloodcoop_host_self_test COMMAND bloodcoop-host --self-test)

# 为什么用 PASS_REGULAR_EXPRESSION 而不是靠退出码：
# 现在模块表是空的（T-007/T-008/T-009 还没落地），自检按设计返回 `degraded` → 退出码 2
# （2 的含义是「没有失败，但不等于通过」，见 09-kernel-host.md §8）。
# ctest 默认把非零退出码当失败，所以改成匹配输出：
#   * 输出里出现 overall = pass / degraded → 通过（不管退出码是 0 还是 2）；
#   * 出现 overall = fail、或者程序崩了没输出 → 匹配不上 → 失败。
# 这样「能跑起来 + stdout 是自检 JSON + 结论不是 fail」这条链路就被 CI 盯住了，
# 而不是只在我自己的机器上成立。
#
# ✅ 已被 CI 真正执行：`.github/workflows/build.yml` 的「跑测试（ctest）」步骤在
#    必需的构建门禁里跑 ctest，并且会先核对「add_test 声明几条 vs ctest 注册几条」——
#    因为 add_test 写在 enable_testing() 之前的子目录里会被 CMake **静默丢弃**（实测踩过）。
#
# T-014 冻结字段与退出码之后，这条断言应当收紧成「必须 pass 且退出码 0」。
set_tests_properties(bloodcoop_host_self_test PROPERTIES
    PASS_REGULAR_EXPRESSION "\"overall\": \"(pass|degraded)\"")
