# mod.death 的构建脚本（T-007 骨架）。
#
# 目标结构（两个可执行目标、按 placement 分派、模块间非法链接的配置期断言）属 **T-009**，
# 本文件不做那些 —— 只保证「这个模块能独立编译，并且有它自己的契约测试」。
#
# 依赖方向单向（src/README.md）：modules/* -> interfaces/ <- kernel/

# 模块本体。include 目录是 **PRIVATE**：17 个模块都有个叫 module.h 的头文件，
# 一旦 PUBLIC 传播出去，将来把它们链进同一个可执行文件时就会出现同名头文件互相顶掉。
add_library(bbcoop_mod_death STATIC module.cpp)
target_include_directories(bbcoop_mod_death PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bbcoop_mod_death
    PRIVATE bbcoop_warnings
    PUBLIC  bloodcoop::interfaces)

# ── 契约测试（任务书 §41.1 T-007 的文件清单）─────────────────────────────────
#
# 契约本体在 tests/fixtures/module_contract.h —— 17 个模块共用一份，
# 免得 17 份「契约」各自漂移成 17 个冒烟测试。
add_executable(bbcoop_test_death_contract tests/contract_test.cpp)
target_include_directories(bbcoop_test_death_contract PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/tests/fixtures
    ${CMAKE_SOURCE_DIR}/tests/unit)
target_link_libraries(bbcoop_test_death_contract
    PRIVATE bbcoop_mod_death bloodcoop::interfaces bbcoop_warnings)

# ⚠️ add_test 逐条写明，不要用循环：CI 的构建门禁用「add_test 声明几条 vs
#    ctest 注册几条」来发现「测试被静默丢弃」，而循环会让那个计数算不对。
add_test(NAME death_contract COMMAND bbcoop_test_death_contract)
