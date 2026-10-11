// mod.replication_core 的契约测试（T-007）。
//
// 契约本体在 tests/fixtures/module_contract.h —— 17 个模块共用一份。
// 这里只做三件事：带上本模块的声明、调用共用契约、返回退出码。
//
// 为什么这么薄：契约说的是骨架阶段的**对外约定**（ID 逐字一致、init 返回
// Unavailable、不发布服务、tick 不记日志、shutdown 幂等）。复制 17 份的结果
// 一定是 17 份各自漂移，那时「契约测试」这个名字就不成立了。

#include "module.h"

#include "module_contract.h"

int main() {
    bbcoop::fixtures::run_module_contract("mod.replication_core", &bbcoop_create_mod_replication_core);
    return bbcoop::test::finish();
}
