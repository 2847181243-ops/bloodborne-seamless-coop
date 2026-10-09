---
name: bbcoop-conventions
description: Bloodborne Seamless Co-op Mod 仓库的强制协作规约骨架——你在哪条分支、能写哪些文件、Issue 驱动、安全红线、验证链。在本仓库做任何代码或文档改动之前先加载本文件；需要详细判据或条文原文时再按需加载同目录下的 references/。
---

# Bloodborne Seamless Co-op Mod —— 协作规约（骨架）

权威需求来源是仓库根目录的 `bloodborne.markdown`（任务书 v3.0，54 节）。
本 skill 是它的工程化摘要。**任何冲突以任务书为准。**

> **本文件是骨架，不是全部。** 详细判据按需加载：
>
> | 需要什么 | 加载 |
> |---|---|
> | 证据等级 / 版本安全链 / 验证链 / 冲突裁决 的详细条文 | [`references/conventions.md`](references/conventions.md) |
> | 安全红线 / 明令禁止 / 游戏设计约束 的完整清单 | [`references/hard-constraints.md`](references/hard-constraints.md) |
>
> 为什么这样分：本文件每次任务都会进上下文，所以只留"不知道就会做错"的东西。
> 详解留在 references/ 里按需读，不占用每次任务的固定开销。

---

## 1. 先确认：你在哪条分支、能写哪些文件

任务书 §48：**每个 Agent 只修改自己分支内的文件。**
CI（`.github/workflows/branch-policy.yml`）会阻断越界 PR。

| Agent | 分支 | 只能写 |
|---|---|---|
| AI-00 | `lead/*` → `main` | `docs/interfaces/`、`docs/integration/`、`docs/conflict_log/`、`docs/standards/`、`.github/`、`.dsh/`、`tools/preci/`、根级 md、构建标准文件 |
| AI-A0 | `agent/a0-*` | `mod/platform/` |
| AI-A1 | `agent/a1-*` | `docs/re/`、`mod/core/` |
| AI-A2 | `agent/a2-*` | `mod/session/`、`mod/replication/` |
| AI-A3 | `agent/a3-*` | `mod/gameplay/` |
| AI-A4 | `agent/a4-*` | `mod/balance/`、`config/` |
| AI-B1 | `agent/b1-*` | `mod/network/transport/` |
| AI-B2 | `agent/b2-*` | `mod/network/discovery/` |
| AI-B3 | `agent/b3-*` | `mod/network/reputation/` |
| AI-B4 | `agent/b4-*` | `relay/` |
| AI-C1 | `agent/c1-*` | `mod/security/crypto/` |
| AI-C2 | `agent/c2-*` | `docs/security/` |
| AI-C3 | `agent/c3-*` | `docs/audit/`、`tools/audit/` |
| AI-C4 | `agent/c4-*` | `docs/pentest/`、`tools/pentest/` |

> **唯一权威是本文件指向的那张表**：`.github/workflows/branch-policy.yml` 的 `SCOPE`。
> 上表是摘要，可能与它不同步。**动了写入范围就要同步两边**，否则门禁与文档会打架。
> 不确定时以 `SCOPE` 为准，或直接跑 `python tools/bbcoop.py check` 让它告诉你。

**三条硬规则**

1. AI-C1 的密码学代码**不允许**被其他 Agent 修改。
2. AI-C3 的审计代码**不允许**被开发 Agent 修改。
3. 任何网络代码未通过 AI-C3 审计，**不得合并**。

越界改动不要硬来 —— 先建 Issue（`.github/ISSUE_TEMPLATE/cross-agent-task.yml`）由 AI-00 裁决。
**不要为了绕过越界而扩大自己的写入范围。**

---

## 2. 提交前必须跑什么

```bash
python tools/bbcoop.py verify      # 先本地预检，再跑回归矩阵
```

这条是硬性的：**本地预检与回归矩阵都通过，才可以请求合并。**
不跑就推上去等 CI，只是把发现问题的成本推后，并且占用了唯一的 CI 通道。

---

## 3. 四个必须记住的判据

### 3.1 证据等级（写进核心逻辑之前必须标注）

`Confirmed` / `Probable` / `Hypothesis` / `Unknown`。
**未标注等级的结论在 review 中一律按 `Unknown` 处理。**
**禁止把猜测直接当成事实写入核心逻辑。** 详见 references。

### 3.2 验证链（不允许跳级声明）

```text
Compile Verified → Runtime Verified → Multiplayer Verified → Regression Verified → Feature Verified
```

> `Build Passed ≠ Feature Complete`

"多人验证"必须在**真实运行时 + 至少 2P 实测**下完成。
**一次只能声明一个等级**，且必须属实。

### 3.3 安全永远优先

隐私不可协商；AI-C3 的 `FAIL` 一票否决，AI-00 不得推翻。
任何以"性能"或"功能"为由削弱安全的设计，必须建 Issue 交 AI-C3 裁决。
完整红线见 [`references/hard-constraints.md`](references/hard-constraints.md)。

### 3.4 六条明令禁止

为了编译通过而**删除功能** / 为了消除报错而**大面积注释代码** /
用 **Mock 冒充真实功能** / 用**日志输出冒充网络同步** /
用**假数据冒充逆向结论** / **编译成功就宣布功能完成**。

---

## 4. 最容易搞错的游戏设计约束

只列**错了就会写出相反逻辑**的四条；完整清单见 references。

- 玩家死亡是 **Player-Level Event**；只有 `Lantern Rest` / `Party Wipe` 是 **World-Level Reset**。
  → **单人死亡不触发 World Reset。**
- **玩家死亡次数绝不能让敌人 / Boss 的 HP 增加。** Scaling 与死亡次数完全独立。
- 客机加入时**临时**使用主机世界状态（**仅内存，不写存档**）。
- 反作弊**只限 PvP**：PvE 不检测、不校验、不踢人。

---

## 5. Issue 驱动：这些情况必须建 Issue

- AI-A1 无法确认关键函数 / 地址 / 原版行为；
- AI-A2 需要改 AI-A1 已确认的核心接口；
- AI-A3 发现原版机制与假设冲突；
- 大规模修改 `Player / World / Boss / Save / 网络协议 / 死亡经济 / Inventory / Reward`；
- `Crash / Deadlock / Memory Corruption / Save Corruption / Duplicate Reward / State Desync / Infinite Respawn / Infinite Enemy Reset`；
- 消耗品复制、商店购买复制、NPC 保护范围不清、世界状态被写入客机存档、反作弊误判；
- 决斗场 Hook 方案未定、区域切换状态异常、独立存档损坏；
- **安全审计未通过**。

Issue 必含：`Title / Background / Observed / Expected / Evidence / Affected Modules / Possible Cause / Risk / Proposed Solutions / Verification Plan`。

---

## 6. 每次改动必须回答的五个问题

1. 改了什么？ 2. 为什么改？ 3. 修改了哪个模块？ 4. 影响哪些系统？ 5. 如何验证？是否存在兼容性风险？

这五个问题就是 `.github/PULL_REQUEST_TEMPLATE.md` 的骨架，CI 会校验它们存在。

---

## 7. 冲突裁决

| 冲突类型 | 裁决方 | 优先级 |
|---|---|---|
| 接口冲突 | AI-00 | 开发优先 |
| 安全 vs 开发 | AI-C3 | **安全优先** |
| 性能 vs 安全 | AI-C2 | **安全优先** |
| 功能 vs 安全 | AI-C3 | **安全优先** |
| 版本兼容冲突 | AI-00 | 兼容优先 |

裁决必须留痕到 `docs/conflict_log/`，不允许口头结论直接生效。
