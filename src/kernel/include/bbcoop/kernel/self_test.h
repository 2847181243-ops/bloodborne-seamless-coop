// `--self-test` 的 JSON 渲染（T-005 产出，T-014 消费）。
//
// 手写渲染，不引入 JSON 库：第三方依赖白名单任务书没有给出
// （docs/architecture/02-module-contract.md M-6），为一份不到 40 行的输出引入一个库
// 不划算，而 C++ 标准库没有 JSON。
//
// 字段（任务书 §41.1 T-005 要求「至少包含」这些）：
//   interface_version  冻结接口版本，来自 src/interfaces/version.h 的 kInterfaceVersion
//   overall            pass | degraded | fail
//   modules[]          id / source / health / initialized / circuit_open / required /
//                      last_error / unavailable_reason / tick_calls / tick_timeouts
//                      （source 与 unavailable_reason 是 07-kernel-seam §1.5 要求的）
//   skipped[]          未加载的模块：id / reason（disabled | not_implemented）
//   version            宿主版本，由构建注入（CMake 的 PROJECT_VERSION）
//
// ⚠️ 字段名与大小写**还没有被冻结**（K-9 归 T-014）：这里的形状与
// `.github/ISSUE_TEMPLATE/fault_injection.yml` 里写的示例不完全一致
// （那边是小写、且写死 degraded）。本卡只保证「输出的是可解析 JSON + overall 正确」，
// 冻结在 T-014 做。
//
// 判定规则（见 kernel.cpp 的 computeOverall）：
//   fail     至少一个 **required** 模块不是 Ok（未初始化 / health 非 Ok / 已熔断）
//   degraded 没有 required 失败，但至少一个已加载模块不是 Ok，**或一个模块都没加载**
//   pass     所有已加载模块都是 Ok，且至少有一个模块
// 退出码由宿主决定：fail → 非 0（任务书 §41.1 T-014 的验收）。
// 注意 degraded 不是 fail 也不是 pass —— 宿主按仓库既有约定返回 2（有未验证项）。

#pragma once

#include <string>

#include "kernel.h"

namespace bbcoop::kernel {

// 渲染成 JSON 文本（含结尾换行）。version 为空指针时写成 "unknown"。
std::string renderSelfTestJson(const SelfTestReport& report, const char* version);

}  // namespace bbcoop::kernel
