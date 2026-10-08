# Bloodborne Seamless Co-op Mod

> 把 Bloodborne 原版多人流程改造成**持续存在的多人 Session**：消除召唤、断线、区域切换带来的割裂感，
> 同时完整保留原版的战斗风险、死亡经济、世界状态与 Boss 设计。
>
> **Seamless 消除的是等待和联机流程，不是 Bloodborne 的风险。**

本仓库是《Bloodborne Seamless Co-op Mod AI 开发任务书 v3.0》的工程落地产物，按**多 AI 协作架构**组织：
1 个项目总控（AI-00）+ 13 个职能 Agent，每个 Agent 持有独立分支、独立文档所有权与明确接口契约。

---

## 1. 权威需求来源

| 文档 | 说明 |
|---|---|
| [bloodbrone.markdown](bloodbrone.markdown) | **任务书 v3.0（唯一需求来源 / Single Source of Truth）** |

> 文件名为原始交付名（`bloodbrone`），内容逐字保留、未做任何修改。
> 任何与本 README 冲突之处，**以任务书为准**。

---

## 2. 合规边界（务必先读）

本项目**只**针对合法持有的游戏数据、合法 PC 运行环境与 Mod / 兼容层开发。

**明确禁止**：DRM 绕过、破解、盗版分发、未授权访问机制、进程扫描 / 内存扫描、中心化服务器、官方账号系统、
隐私数据收集、自动封禁、Steam 客户端依赖、PS4 模拟器依赖、2v2 / 3v3 / FreeForAll、房主携带客机进入决斗场。

**仓库中不得提交**：游戏本体文件（`eboot.bin`、`Bloodborne.exe`、PS4 可执行文件）、解密后的商业素材、
任何玩家存档（`.sl2` / `.bloodco`）、任何凭据或 token。参见 [.gitignore](.gitignore)。

---

## 3. 当前状态

| 项目 | 状态 |
|---|---|
| 仓库初始化 / CI 骨架 / 文档骨架 | ✅ Done |
| Phase 0 安全设计（威胁模型 / 安全架构 / 密码学方案） | ⬜ Not Started |
| Phase 1 环境与版本确认 + bbport runtime 逆向 | ⬜ Not Started |
| Phase 2–15 | ⬜ Not Started |

> **Build Passed ≠ Feature Complete.**
> 验收链：`Compile Verified → Runtime Verified → Multiplayer Verified → Regression Verified → Feature Verified`

---

## 4. 角色 / 分支 / 所有权

每个 Agent 只在自己的分支与所有权范围内写文件；跨范围修改必须先由 AI-00 建立 Issue（任务书 §48 / §52）。

| Agent | 角色 | 分支 | 写入范围 |
|---|---|---|---|
| AI-00 | Integration Lead / 总控 | `main` | `docs/interfaces/`、`docs/integration/`、`docs/conflict_log/`、`.github/` |
| AI-A0 | Platform Integration Engineer | `agent/a0-platform-integration` | `mod/platform/` |
| AI-A1 | Senior Game Reverse Engineer | `agent/a1-reverse-engineering` | `docs/re/`、`mod/core/` |
| AI-A2 | Multiplayer Networking / Session Engineer | `agent/a2-session-replication` | `mod/session/`、`mod/replication/` |
| AI-A3 | Game Systems Engineer | `agent/a3-gameplay-systems` | `mod/gameplay/` |
| AI-A4 | Balance & PvP Engineer | `agent/a4-balance-pvp` | `mod/balance/`、`config/` |
| AI-B1 | Network Transport Engineer | `agent/b1-transport` | `mod/network/transport/` |
| AI-B2 | P2P Discovery Engineer | `agent/b2-discovery` | `mod/network/discovery/` |
| AI-B3 | Reputation System Engineer | `agent/b3-reputation` | `mod/network/reputation/` |
| AI-B4 | Relay Node Engineer | `agent/b4-relay` | `relay/` |
| AI-C1 | Cryptography Engineer | `agent/c1-cryptography` | `mod/security/crypto/` |
| AI-C2 | Security Architect | `agent/c2-security-architecture` | `docs/security/` |
| AI-C3 | Security Auditor（一票否决） | `agent/c3-security-audit` | `docs/audit/`、`tools/audit/` |
| AI-C4 | Penetration Tester | `agent/c4-penetration-test` | `docs/pentest/`、`tools/pentest/` |

**硬规则**

- AI-C1 的密码学代码**不允许**被其他 Agent 修改。
- AI-C3 的审计代码**不允许**被开发 Agent 修改。
- 任何网络代码未通过 AI-C3 审计，**不得合并**。
- 冲突裁决：接口冲突 → AI-00；**安全 vs 开发 / 性能 / 功能 → 安全优先**。

---

## 5. 目录结构

```text
.
├── bloodbrone.markdown            # 任务书 v3.0（权威需求）
├── README.md / CONTRIBUTING.md / SECURITY.md
├── .github/
│   ├── CODEOWNERS
│   ├── PULL_REQUEST_TEMPLATE.md
│   ├── ISSUE_TEMPLATE/            # 跨 Agent 任务 / 逆向结论 / 运行时缺陷 / 安全审计
│   └── workflows/                 # 文档结构校验 / 分支策略 / PR 门禁 / Issue 自动标注 / 构建
└── docs/
    ├── interfaces/                # 接口契约（AI-00）
    ├── re/                        # 逆向文档（AI-A1）
    ├── security/                  # 安全文档（AI-C2）
    ├── audit/                     # 审计报告（AI-C3）
    ├── pentest/                   # 渗透测试报告（AI-C4）
    ├── integration/               # 集成测试报告（AI-00）
    └── conflict_log/              # 冲突记录与裁决（AI-00）
```

`docs/` 每个子目录的必填内容与所有者见 [docs/README.md](docs/README.md)。

---

## 6. 如何开始工作

```bash
git clone <仓库地址>
cd <仓库目录>

# 切到自己的 Agent 分支
git switch agent/a1-reverse-engineering

# 只改自己所有权范围内的文件
# ...

git push -u origin agent/a1-reverse-engineering
# 然后开 PR，套用 PR 模板（CI 会校验分支名与写入范围）
```

- 提交前先读 [CONTRIBUTING.md](CONTRIBUTING.md)（分支模型 / Issue 驱动 / 证据等级 / 文档要求）。
- 安全相关问题先读 [SECURITY.md](SECURITY.md)（安全一票否决 / 隐私红线 / 披露流程）。

---

## 7. 证据等级（强制）

任务书中每条逆向结论必须标注，**禁止把猜测当事实写入核心逻辑**：

| 标记 | 含义 |
|---|---|
| `Confirmed` | 已通过源码、反汇编、交叉引用、运行时日志或行为实验确认 |
| `Probable` | 存在较强证据，但尚未完全确认 |
| `Hypothesis` | 当前仅为推测 |
| `Unknown` | 暂无足够证据 |

---

## 8. 核心设计结论（速查）

- 玩家死亡是 **Player-Level Event**；`Lantern Rest` / `Party Wipe` 才是 **World-Level Reset Event**。
- 玩家死亡后从 Lantern 重生，**单人死亡不触发 World Reset**，已死亡敌人不刷新。
- 死亡惩罚在 Lantern 重生后施加（Max HP 惩罚池，可叠加，最低保留 30%），在 Lantern Rest / 世界重置时清除。
- Blood Echoes 掉落 / 回收 / 敌人持有等原版死亡经济机制**保持不变**，且**个人独立**；
  再次死亡只保留最后一次掉落，World Reset 不清除。
- `Enemy/Boss Scaling` 与玩家死亡次数**完全独立**；`PvE Scaling` 与 `PvP Scaling` **完全独立**。
- PvP 中单个玩家死亡**不是结算点**，只有合作队全灭或入侵队全灭才进入结算。
- 随机掉落独立；固定拾取与 Boss 掉落默认每人一份；唯一物品不得重复。
- 每个玩家消耗品库存独立，使用后只影响使用者自身；商店交互按个人处理。
- 客机加入时临时使用主机世界状态（仅内存，不写入存档），离开或中断时恢复个人进度。
- 区域切换跟随 Host；Boss / PvP 期间禁止切换。
- 使用独立存档格式（如 `.bloodco`），与原版 `.sl2` 完全隔离，写入必须原子化。
- 反作弊**仅限 PvP**；PvE 不检测、不踢人、不记录。
- **不收集、不存储、不上传任何玩家隐私数据。**
- 安全设计先行，安全审计独立，安全一票否决。

---

## 9. 许可

尚未确定。在 LICENSE 落地前，本仓库默认保留所有权利。
