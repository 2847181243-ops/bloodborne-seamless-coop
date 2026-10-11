// 本文件是 v1 冻结接口的一部分（任务书 §3.6 原则 3）。
//
// 冻结后的任何变更按 §52-c 的流程走：
//   建 Issue -> 评估影响面 -> 提供过渡方案 -> AI-00 裁决 -> 同步 docs/interfaces/
// **禁止**为了让某个模块方便而直接改这里。
//
// 两条贯穿全局的约束：
//   * 跨模块边界的函数全部 noexcept —— 异常穿过边界会让一个模块的失败
//     变成整个进程的失败（§3.6 原则 5）。
//   * 接口层不提供任何「先连上再说」的路径 —— crypto 失败即拒绝联机（原则 6）。

#pragma once

#include <cstdint>

#include "errors.h"
#include "version.h"

namespace bbcoop {

// 游戏系统能力的公共契约。由 Layer 2 的模块提供
// （`mod.death` / `mod.world_reset` / `mod.enemy_boss` / `mod.loot` /
//  `mod.pvp` / `mod.save`，任务书 §3.6 注册表）。
//
// 为什么 Layer 2 需要一个公共接口，而不是各写各的：
// 任务书 §7 要求把「Session 世界状态覆盖」与「个人进度隔离」分开，
// 而这条边界横跨全部 Layer 2 模块。把它做成接口上可查询的属性，
// 比在每个模块的实现里各自记得要容易核对。
class IGameplay {
public:
    static constexpr const char* kServiceName = "bbcoop.gameplay";
    static constexpr int kServiceVersionMajor = 1;

    IGameplay() = default;
    virtual ~IGameplay() = default;
    IGameplay(const IGameplay&) = delete;
    IGameplay& operator=(const IGameplay&) = delete;

    // 本模块是否会写入**主机**的会话世界状态。
    //
    // 为 true 时，Kernel 与客机侧要按「世界状态可能被覆盖」处理。
    virtual bool affects_world_state() const noexcept = 0;

    // 本模块是否会写入**客机的个人存档**。
    //
    // 任务书 §7 要求世界状态不得写入客机个人存档，§31 要求存档写入原子化。
    // 允许本方法返回 true 的模块需要单独说明理由并接受审计。
    virtual bool affects_personal_save() const noexcept = 0;
};

}  // namespace bbcoop
