// Kernel 的模块表（T-005 产出）。
//
// 本文件属于 Kernel 内部接口：模块**不得** include（见 policy.h 的说明）。
//
// ── 表从哪来：T-005 不读配置 ────────────────────────────────────────────────
//
// 模块清单的唯一真相源是 `config/modules.yaml`（任务书 §3.6），但那是 **T-008** 的产出，
// 而任务卡顺序是 T-005 → T-006 → T-007 → T-008：T-005 不能依赖一个还不存在的文件。
// 所以这里只定义**表的形状**，谁填表由下游决定：
//
//   * T-005：`bloodcoop-host` 用一张静态表（当前为空 —— T-007 还没有产出模块）；
//   * T-008 / T-009：按 `placement`（local / remote）把 YAML 里的模块分派到两个目标，
//     并在配置阶段核对「YAML 与 CMake 的 placement 一致」。
//
// 草案分支 `wip/kernel-draft` 里有一个 `tools/gen_module_table.py`（把 YAML 渲染成
// `module_table.inc`）。它没有被搬过来：**生成器属 T-008/T-009 的范围**，
// 在没有 YAML 与 schema 的现在搬过来只会变成一段没人能验证的代码。

#pragma once

#include <cstddef>
#include <cstdint>

#include "imodule.h"

namespace bbcoop::kernel {

// 未加载的模块及其原因。
//
// reason 是两个固定取值之一：
//   "disabled"        —— modules.yaml 里 enabled: false
//   "not_implemented" —— enabled: true 但 implemented: false（还没有实现）
//
// 自检输出的 skipped 字段用它。为什么需要它：一个模块「没出现在 modules 里」
// 有两种完全不同的原因，只剩下 silent 的缺席就无法区分「被关掉了」与「构建时漏了」，
// 而 T-012 的模块隔离判定恰恰要求看见「某个模块确实被禁用了」。
struct SkippedModule {
    const char* id = nullptr;
    const char* reason = nullptr;
};

// Kernel 启动时使用的模块表。
//
// loadable 与 requiredFlags 的长度都是 loadableCount；
// skippedIds 与 skippedReasons 的长度都是 skippedCount（两个数组一一对应）。
//
// ⚠️ requiredFlags 可以为 nullptr（表示「没有 required 信息」）：
// 核心模块清单任务书没有给（docs/architecture/01-kernel.md §4），
// 在裁决之前，T-005 的宿主把**已注册模块一律视为 required**，
// 并把这件事写进自检输出，而不是自己发明一份「核心模块清单」。
//
// 这里刻意不合并成一个「条目结构体」：加载用的仍然是冻结接口里的 ModuleDescriptor
// 本身，其余数组是它的附加信息。
struct ModuleTable {
    const ModuleDescriptor* loadable = nullptr;
    const bool* requiredFlags = nullptr;
    std::size_t loadableCount = 0;

    // 进程外的模块：它们没有 `ModuleDescriptor`（没有 IModule 实例），
    // 只有「ID + 一条 IPC 通道」。存活事实由 IPC 回调驱动（07-kernel-seam §1.1）。
    struct RemoteEntry {
        const char* id = nullptr;
        std::uint32_t channel = 0;  // ChannelId::value
    };
    const RemoteEntry* remote = nullptr;
    std::size_t remoteCount = 0;

    const char* const* skippedIds = nullptr;
    const char* const* skippedReasons = nullptr;
    std::size_t skippedCount = 0;
};

}  // namespace bbcoop::kernel
