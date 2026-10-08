<!--
  PR 模板 —— 对应任务书 §38「每次修改必须回答的五个问题」与 §39「验证标准」。
  CI（.github/workflows/pr-guard.yml）会校验下列章节标题是否存在，请勿删除。
-->

## 改了什么

<!-- 一句话说明这个 PR 做了什么。一个 PR 只解决一件事。 -->

## 为什么改

<!-- 引用任务书章节号 / Issue 编号。没有依据的改动不要提 PR。 -->

- 任务书章节：
- 关联 Issue：#

## 修改了哪个模块

- [ ] Core（HookManager / AddressResolver / VersionChecker / Logger）
- [ ] Network（Session / Transport / Replication / Protocol / Blacklist）
- [ ] Security（E2EE / KeyExchange / IntegrityCheck / Audit）
- [ ] Gameplay（Player / Enemy / Boss / Death / Lantern / World / Invasion / Balance / Loot / Consumable / Shop / NPC / SessionState / Arena）
- [ ] Platform（bbport 适配 / 启动器集成）
- [ ] Docs / CI / Config

## 影响哪些系统

<!-- 死亡 / 重生 / Lantern / World Reset / Boss / 掉落 / 存档 / 网络协议 / 其他 Agent 的接口 -->

## 如何验证

<!-- 贴出实际执行的命令、日志片段、实测结果。不要写“应该可以”。 -->

- [ ] Compile Verified
- [ ] Runtime Verified
- [ ] Multiplayer Verified（至少 2P 实测）
- [ ] Regression Verified
- [ ] Feature Verified

**当前验证等级：** <!-- 从上面选一个，不允许跳级声明 -->

```text
验证命令 / 步骤：

实际结果：

日志片段（脱敏，不得包含 IP / 账号 ID / 硬件信息）：

```

## 是否存在兼容性风险

<!-- 版本兼容 / Hook 冲突 / 协议兼容 / 存档兼容。有则说明，无则写“无”。 -->

## 证据等级（涉及逆向结论时必填）

- [ ] Confirmed
- [ ] Probable
- [ ] Hypothesis
- [ ] Unknown

<!-- 未标注等级的结论在 review 中一律按 Unknown 处理。 -->

## 安全自检（涉及网络 / 存档 / 物品发放时必填）

- [ ] 未新增任何形式的隐私数据采集或出网字段
- [ ] 未新增持久化标识 / 设备指纹
- [ ] 消息附带 HMAC，且有防重放机制（序列号 + 时间戳）
- [ ] 校验失败路径会记录 `[SECURITY]` 日志并拒绝，**没有静默通过**
- [ ] 中继仍无法解密 / 篡改 / 重放 / 记录日志
- [ ] 存档写入已原子化，非法物品被拒绝写入
- [ ] 物品发放有记账，唯一物品不重复

## 变更文件的所有权

<!-- CI 会校验：改动路径必须落在这个 Agent 的所有权范围内（任务书 §48）。 -->

- Agent：AI-
- 分支：`agent/`
- [ ] 本次改动全部落在本 Agent 所有权范围内
- [ ] 存在跨范围改动 → 已由 AI-00 建立 Issue：#___

## 合并就绪声明（必须勾选，缺项不合并）

<!-- 合并方式：squash。规则集 main-protection 已强制 required_linear_history + squash-only。 -->

- [ ] 本 PR **只解决一件事**，标题与内容一致
- [ ] 标题与正文用**人类语言**：不用自造比喻（「XX 剧场」「杜绝…红」之类）、
      不中英夹杂、不用只有作者懂的缩写 —— 见 `CONTRIBUTING.md` 的「写作规范」
- [ ] 已验证「当前验证等级」属实，**未跳级声明**（任务书 §39）
- [ ] 所有失败或跳过的检查已解释，无「先合并再修」的遗留项
- [ ] 涉及网络 / 安全 / 存档 / 物品发放 → 已有 `docs/audit/` 报告，且**双人签核**（C3-a / C3-b）齐全
- [ ] 涉及逆向结论 → 每条结论已标注证据等级（Confirmed / Probable / Hypothesis / Unknown）
- [ ] 无凭据、token、私钥、游戏本体文件、玩家存档进入提交（SECURITY.md §9）
- [ ] 已知残余风险已写入下方「待确认项 / 遗留风险」

## 待确认项 / 遗留风险
