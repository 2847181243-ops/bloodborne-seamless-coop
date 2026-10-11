// mod.crypto 的骨架声明（T-007）。
// owner: AI-C1。
//
// 模块 ID 与任务书 §3.6 注册表**逐字一致**：`mod.crypto`（Layer 0，加密）。
// 职责：握手、加密、认证。
//
// ⚠️ 这个头文件只给**本模块自己与它的契约测试**用：
//    CMake 里这个目录是 PRIVATE include，不会传播给链接本模块的目标。
//    别的模块需要本模块的能力时，走服务注册表（module_context.h），
//    不要 include 这个文件（§3.6 硬性禁止跨模块 include）。

#pragma once

#include "imodule.h"

// 创建符号的固定命名：`mod.<名字>` -> `bbcoop_create_mod_<名字>`
// （全仓库统一，T-008/T-009 生成的模块表按这个名字取地址，因此不要改名）。
//
// 只导出这一个符号：Kernel 只通过它创建实例，不知道 CryptoModule 这个类型。
bbcoop::IModule* bbcoop_create_mod_crypto() noexcept;
