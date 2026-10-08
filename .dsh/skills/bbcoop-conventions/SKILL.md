---
name: bbcoop-conventions
description: Bloodborne Seamless Co-op Mod 仓库的强制协作规约——分支与写入所有权、Issue 驱动、证据等级、版本安全链、安全一票否决、禁止事项与验证链。在本仓库做任何代码或文档改动之前加载；当你需要判断"这个文件我能不能改""这个结论能不能当事实用""这个改动要不要建 Issue"时加载。
---

# Bloodborne Seamless Co-op Mod —— 协作规约

权威需求来源是仓库根目录的 `bloodbrone.markdown`（任务书 v3.0，54 节）。
本 skill 是它的工程化摘要。**任何冲突以任务书为准。**

---

## 1. 先确认：你在哪条分支、能写哪些文件

任务书 §48：**每个 Agent 只修改自己分支内的文件。** CI（`.github/workflows/branch-policy.yml`）会阻断越界 PR。

| Agent | 分支 | 只能写 |
|---|---|---|
| AI-00 | `lead/*` → `main` | `docs/interfaces/`、`docs/integration/`、`docs/conflict_log/`、`.github/`、根级 md、`.dsh/` |
| AI-A0 | `agent/a0-platform-integration` | `mod/platform/` |
| AI-A1 | `agent/a1-reverse-engineering` | `docs/re/`、`mod/core/` |
| AI-A2 | `agent/a2-session-replication` | `mod/session/`、`mod/replication/` |
| AI-A3 | `agent/a3-gameplay-systems` | `mod/gameplay/` |
| AI-A4 | `agent/a4-balance-pvp` | `mod/balance/`、`config/` |
| AI-B1 | `agent/b1-transport` | `mod/network/transport/` |
| AI-B2 | `agent/b2-discovery` | `mod/network/discovery/` |
| AI-B3 | `agent/b3-reputation` | `mod/network/reputation/` |
| AI-B4 | `agent/b4-relay` | `relay/` |
| AI-C1 | `agent/c1-cryptography` | `mod/security/crypto/` |
| AI-C2 | `agent/c2-security-architecture` | `docs/security/` |
| AI-C3 | `agent/c3-security-audit` | `docs/audit/`、`tools/audit/` |
| AI-C4 | `agent/c4-penetration-test` | `docs/pentest/`、`tools/pentest/` |

**三条硬规则**

1. AI-C1 的密码学代码**不允许**被其他 Agent 修改。
2. AI-C3 的审计代码**不允许**被开发 Agent 修改。
3. 任何网络代码未通过 AI-C3 审计，**不得合并**。

越界改动不要硬来 —— 先建 Issue（`.github/ISSUE_TEMPLATE/cross-agent-task.yml`）由 AI-00 裁决。

---

## 2. Issue 驱动：这些情况必须建 Issue

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

## 3. 证据等级（写进核心逻辑之前必须标注）

| 标记 | 含义 |
|---|---|
| `Confirmed` | 源码 / 反汇编 / 交叉引用 / 运行时日志 / 行为实验确认 |
| `Probable` | 较强证据，未完全确认 |
| `Hypothesis` | 仅推测 |
| `Unknown` | 暂无足够证据 |

**禁止把猜测直接当成事实写入核心逻辑。** 未标注等级的结论在 review 中一律按 `Unknown` 处理。

---

## 4. 版本安全链（任务书 §5.5）

```text
VersionChecker → AddressResolver → HookManager → Game Systems
```

- 版本不匹配 → **禁止**使用旧地址；
- Signature 不匹配 → **禁止**强行 Hook；
- Hook 失败必须明确记录日志，**不允许静默失败**；
- **禁止**因为"可能是这个地址"就直接写入内存。

---

## 5. 安全红线与一票否决

**隐私（不可协商）**：不收集、不存储、不上传 IP / 平台账号 ID / 硬件指纹 / Mod 自发生成持久 ID / 设备信息 / 系统信息 / 地理位置。不扫进程、不读内存、不监控系统。
本地黑名单**只记游戏内显示名称**，必须承认"同名误伤、改名绕过"是隐私优先的必然代价。

**一票否决**：AI-C3 的 `FAIL` 阻断合并，AI-00 不得推翻。审计结论只有 `PASS / PASS_WITH_WARNING / FAIL`。

**中继**：只能看加密包和临时节点标识；不能解密 / 篡改 / 重放 / 追踪；必须不存储、不记日志、可随时退出。

**反作弊只限 PvP**：PvE 不检测、不校验、不踢人、不写反作弊日志、不启用黑名单自动拦截。

**性能 vs 安全**：安全永远优先。任何以"性能"或"功能"为由削弱安全的设计，必须建 Issue 交 AI-C3 裁决。

---

## 6. 明令禁止

- 为了编译通过而**删除功能**；
- 为了消除报错而**大面积注释代码**；
- 用 **Mock 冒充真实功能**；
- 用**日志输出冒充网络同步**；
- 用**假数据冒充逆向结论**；
- **编译成功就宣布功能完成**。

---

## 7. 验证链（不允许跳级声明）

```text
Compile Verified → Runtime Verified → Multiplayer Verified → Regression Verified → Feature Verified
```

> `Build Passed ≠ Feature Complete`

"多人验证"必须在**真实运行时 + 至少 2P 实测**下完成。

---

## 8. 每次改动必须回答的五个问题

1. 改了什么？ 2. 为什么改？ 3. 修改了哪个模块？ 4. 影响哪些系统？ 5. 如何验证？是否存在兼容性风险？

这五个问题就是 `.github/PULL_REQUEST_TEMPLATE.md` 的骨架，CI 会校验它们存在。

---

## 9. 冲突裁决

| 冲突类型 | 裁决方 | 优先级 |
|---|---|---|
| 接口冲突 | AI-00 | 开发优先 |
| 安全 vs 开发 | AI-C3 | **安全优先** |
| 性能 vs 安全 | AI-C2 | **安全优先** |
| 功能 vs 安全 | AI-C3 | **安全优先** |
| 版本兼容冲突 | AI-00 | 兼容优先 |

裁决必须留痕到 `docs/conflict_log/`，不允许口头结论直接生效。

---

## 10. 最容易搞错的游戏设计约束

- 玩家死亡是 **Player-Level Event**；`Lantern Rest` / `Party Wipe` 才是 **World-Level Reset Event**。
- **单人死亡不触发 World Reset**，已死亡敌人不刷新，队友继续战斗。
- 死亡惩罚 = 死亡玩家**自身** Max HP 降低（可叠加，最低保留 30%），Lantern Rest 才清除。
- **玩家死亡次数绝不能让敌人 / Boss 的 HP 增加。** Scaling 与死亡次数完全独立。
- Blood Echoes 原版机制不变，且**个人独立**；再次死亡只留最后一次；`World Reset` **不清除**地面掉落。
- PvP 单人死亡**不是结算点**，只有整队全灭才结算。
- 客机加入时**临时**使用主机世界状态（**仅内存，不写存档**），离开或中断时恢复个人进度。
- 决斗场仅 1v1，必须**主动离开 Session**单独进入，房主不得携带客机进入。
