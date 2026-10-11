# REF-005 多人协作与贡献者治理（跨项目先例核对）

> **用途**：为「未来多人一起贡献」提供分阶段机制：权限、角色梯队、复查负载、准入培养、合并自动化、反模式。
> **用在哪些决策**：Issue #40；`docs/standards/协作与复查-改进方案.md` 的 S1~S4。
> **它不是要求**：与任务书、`MAINTAINERS.md`、`AI_POLICY.md` 冲突时以后者为准；先例只用来回答「还有没有更优做法」。
> **最后更新**：2026-10
> **证据等级**：Confirmed = 实际读到一手文件；Likely = 多份一手材料交叉推断；Unverified = 未找到一手来源。
> **引用格式**：`<owner>/<repo>@<分支>:<path>`。

- 范围：1 账号 → 2 账号 → 多贡献者各阶段应开启的平台强制机制与流程约定、角色梯队、复查负载、准入培养、合并自动化、反模式。
- 方法：只读 GitHub API 获取下列仓库默认分支的一手文件；读不到的内容一律标「未核实」，不用记忆补全。
- 分支：kubernetes/community、github/docs、rust-lang/rust-forge、rust-lang/team、rust-lang/bors、probot/dco（现重定向至 dcoapp/app）为 main；godotengine/godot、openssl/openssl 为 master。
- 证据等级：A=一手文档原文直接规定；B=一手文档明确、但条件/数值需按本项目裁剪；C=由多份一手文档归纳，没有单条原文直述。
- 引用格式：`repo@分支:路径#L起-L止`｜关键词。

## 1. 结论摘要（建议采用）

1. **先建「机器护栏」，再加人。** 必需状态检查、PR/issue 模板、`triage`/`good first issue`/`help wanted` 标签、DCO/CLA 决策，在 1 账号阶段就能落地，不依赖第二个审批人。

2. **「必需人工审批」的开启点是第 2 个账号。** GitHub 硬规则是「PR 作者不能批准自己的 PR」；1 账号阶段开 require approvals 会把自己锁死。若仓库在组织下，可用「Allow specified actors to bypass required pull requests」过渡（该白名单只在组织仓库可用）。

3. **尽早把仓库放进 GitHub Organization。** merge queue 只对组织拥有的仓库开放（公开仓库免费，私有仓库需 GHEC），bypass 白名单也仅组织仓库可用；个人账户仓库会把这些能力锁死。

4. **角色三级裁剪并文件化。** 贡献者 → Reviewer → Approver/Owner；写进 OWNERS/CODEOWNERS（k8s 明确用 OWNERS 替代 GitHub Teams，理由是 Teams 变更不公开可审计）；晋级 = 客观计数 + 现任 Approver 提名 + 公示无异议。

5. **复查负载与合并自动化都由「规模触发」，不提前上。** 自动指派 + 每人容量上限 + rotation/休假开关是 3~5 人阶段的必需品；merge queue/bors/tide 要等「CI 已是必需检查、合并开始排队或撞车、有 2~3 名活跃审批者」再上，之前用 auto-merge 过渡即可。

## 2. 逐条机制

### A. 权限与角色

**A1 最小权限分层（Read/Triage/Write/Maintain/Admin）**
- 做法：新人默认 Read；需要管 issue/PR 的给 Triage；能稳定推分支的给 Write；Maintain/Admin 只给 1~2 个核心维护者。官方给每档都标了推荐用途。
- 来源：`github/docs@main:content/organizations/managing-user-access-to-your-organizations-repositories/managing-repository-roles/repository-roles-for-an-organization.md#L24-L29`｜关键词：Read…Triage…Write…Maintain…Admin
- 阶段/代价/证据：1 账号（规划）~2 账号（启用）｜低｜A

**A2 单人阶段不要开「必需人工审批」；组织仓库可用 bypass 过渡**
- 做法：1 个账号时 Require approvals 无法满足（作者不能自批）；要么暂缓，要么用组织仓库的 bypass 白名单，并同时开「Do not allow bypassing the above settings」防 admin 绕过。
- 来源：`github/docs@main:data/reusables/repositories/request-changes-tips.md`（原句：Pull request authors cannot approve their own pull requests）＋`.../managing-a-branch-protection-rule.md#L53,L61-L82`
- 阶段/代价/证据：1 账号｜低｜A

**A3 组织所有权至少两人、有接班人**
- 做法：组织 owner 权限含删除组织，官方建议限制人数但不低于两人；大项目用管理员小组 + 提名/批准 + 时区覆盖 + SLO/升级。
- 来源：`github/docs@main:content/organizations/managing-peoples-access-to-your-organization-with-roles/roles-in-an-organization.md#L55-L57`；`kubernetes/community@main:github-management/README.md#L24-L47`、`github-management/org-owners-guide.md#L7-L25`
- 阶段/代价/证据：多贡献者｜低｜A

**A4 角色梯队：贡献者 → Reviewer → Approver（→Owner）**
- 做法：保留 Reviewer 管质量与正确性、Approver 管整体接受度/兼容性/是否该合的分工；Owner 由 Approver 兼任。可量化门槛候选：Reviewer=入组≥3 月+5 次主审+20 个实质 PR；Approver=Reviewer≥3 月+10 次主审+30 个 PR；提名 + 公示无异议。小项目按比例下调数字，但保留文件化 + 提名制。
- 来源：`kubernetes/community@main:community-membership.md#L9-L14,L113-L194`｜关键词：Reviewer/Approver 的 Requirements 与 Responsibilities
- 阶段/代价/证据：2 账号起（第一档）/多贡献者（完整）｜中｜A（数字需裁剪）

**A5 角色写进 OWNERS/CODEOWNERS 文件，而不是藏在 Teams 里**
- 做法：k8s OWNERS 支持 approvers/reviewers/emeritus_approvers/labels，别名集中在 OWNERS_ALIASES；GitHub 原生 CODEOWNERS 支持按目录/通配符指定 owner，并在 PR 打开时自动请求评审（draft PR 不会自动请求）。
- 来源：`kubernetes/community@main:contributors/guide/owners.md#L28-L60,L139-L160`；`github/docs@main:content/repositories/managing-your-repositorys-settings-and-features/customizing-your-repository/about-code-owners.md#L18-L24,L34-L54`
- 阶段/代价/证据：1 账号即可先建骨架｜低｜A

**A6 emeritus 与不活跃清理**
- 做法：给休假/离开设计去处：inactive approver 移入 emeritus_approvers（不再可 approve、不再被自动指派，但仍可咨询）；长期无贡献移出组织；OWNERS 挂名者按「一年 少于10 次贡献」降级。目标：不让 PR 派给不响应的人。
- 来源：`kubernetes/community@main:contributors/guide/owners.md#L98-L137`；`community-membership.md#L208-L239`（12 个月无贡献移出，按 DevStats 统计）
- 阶段/代价/证据：多贡献者｜低（季度维护）｜A

**A7 权限单一事实源 + 自动同步（rust 模式，超过5 人再考虑）**
- 做法：团队成员/权限写在一个受 PR 管控的配置仓库，合并后自动同步到 GitHub Teams、合并机器人、邮件列表、Zulip；支持 dry-run 与 `dump-permission` 审计查询。
- 来源：`rust-lang/team@main:README.md#L1-L23,L67-L145`；`docs/toml-schema.md#L48-L124`；`config.toml#L29-L64`
- 阶段/代价/证据：多贡献者｜中高（需自建同步）｜A

### B. 评审流程与负载

**B1 两阶段评审 + 自动化合并门槛**
- 做法：评审者用 `/lgtm`、审批者用 `/approve`，机器人分别打标签；标签齐、无 `do-not-merge/hold`、测试通过后由 Tide 自动合并（可配 `reviewApprovedRequired`）。k8s 明确警告：approver 的 /lgtm 被同时当作 /approve，会破坏双人复核。
- 来源：`kubernetes/community@main:contributors/guide/owners.md#L171-L245,L247-L293`
- 阶段/代价/证据：2 账号起｜中（需 bot）｜A

**B2 自动指派（最优先做的负载机制）**
- 做法：三选一/组合：GitHub CODEOWNERS 自动请求评审；k8s Blunderbuss 从 OWNERS 选 reviewer 并请求；rust triagebot `[assign.owners]` 按路径匹配并随机派一人。开 PR 即有人，不靠吼。
- 来源：`github/docs@main:…about-code-owners.md#L18-L24,L64`；`kubernetes/community@main:contributors/guide/owners.md#L174-L179,L291-L293`；`rust-lang/rust-forge@main:src/triagebot/pr-assignment.md#L52-L85`
- 阶段/代价/证据：2 账号起｜低~中｜A

**B3 轮换、容量上限、休假开关（防总是一个人审）**
- 做法：GitHub 团队 auto assignment 有 round robin（按最近被请求时间）与 load balance（近 30 天请求量+在办数）两种算法，Busy 状态者不入选；rust triagebot 允许 reviewer 设队列容量 C、rotation on/off、团队级 rotation，以及 `users_on_vacation` 名单——达到容量或关 rotation 即不再自动派单（被别人点名仍会派，但会提示可能不及时）。
- 来源：`github/docs@main:…managing-code-review-settings-for-your-team.md#L29-L43`；`rust-lang/rust-forge@main:src/triagebot/review-queue-tracking.md#L15-L47`、`src/triagebot/pr-assignment.md#L87-L97`
- 阶段/代价/证据：3~5 人起｜低（平台）/中（自建）｜A

**B4 响应时限、失联处理与升级路径**
- 做法：约定 reviewer 合理响应时限，超时改派；长期不可用设 Busy 或移出 OWNERS；作者长期不响应则关闭（k8s 用机器人 90 天）；组织级请求给出 SLO 与升级对象。
- 来源：`kubernetes/community@main:contributors/guide/expectations.md#L47-L58`；`contributors/guide/owners.md#L232-L245`；`github-management/org-owners-guide.md#L7-L25`
- 阶段/代价/证据：多贡献者｜低~中｜A

**B5 「社区评审先行」（小项目低成本变体）**
- 做法：rust 支持先等社区给出 N 个批准，再自动派正式 reviewer，减轻维护者早期负担。本项目可先约定：作者自查清单 + CI 绿 + 至少 1 条非作者实质评论，才请求 Owner 审批。
- 来源：`rust-lang/rust-forge@main:src/triagebot/pr-assignment.md#L99-L111`
- 阶段/代价/证据：2~5 人｜低｜B（机制 A、N 自定）

### C. 准入与培养

**C1 good first issue 是承诺，不是标签装饰**
- 做法：`help wanted` 要求任务清晰、优先级适中、不过期；`good first issue` 额外要求无高级环境/领域门槛、方案已在 issue 写明、给出背景阅读与相似实现、指明相关代码与测试、有现成测试可改；并承诺新人不必自己找 approver、催评审、猜 bot 命令或排查 flake。
- 来源：`kubernetes/community@main:contributors/guide/help-wanted.md#L12-L97`
- 阶段/代价/证据：2 账号起｜低（每个 issue 约 15 分钟）｜A

**C2 新人入口一条龙 + 自助指派**
- 做法：文档写明「找 good first issue → `/assign` → 找对应 SIG → 看 SIG-specific CONTRIBUTING」；rust 提供总表（各子项目贡献指南 + good first issue 链接 + Zulip/论坛入口），并要求大改动先与团队讨论（MCP/RFC/ACP）。
- 来源：`kubernetes/community@main:contributors/guide/first-contribution.md#L20-L61,L115-L127`；`rust-lang/rust-forge@main:src/how-to-start-contributing.md#L9-L47,L64-L89`
- 阶段/代价/证据：2 账号起｜低~中｜A

**C3 导师制：先做 office hours + shadow**
- 做法：k8s 按成本列出：SIG office hours/mentoring → group mentoring cohort → 角色 shadow → GSoC/LFX/Outreachy。小项目先做「每周 1 小时 office hours + 新人 shadow 1 次评审/1 次发布」。
- 来源：`kubernetes/community@main:mentoring/README.md#L14-L60`
- 阶段/代价/证据：3~5 人起｜中｜B

**C4 准入门槛与 sponsor（可选，后置）**
- 做法：k8s 加入 Org 需 2 名 reviewer/approver 担保、跨公司、贡献须持续长期；本项目可降级为「1 名现有 Approver 提名 + 公示 7 天无异议」，但保留「担保人必须与其共同评审/协作过」的实质要求。
- 来源：`kubernetes/community@main:community-membership.md#L39-L76`；`github-management/new-membership-procedure.md#L36-L74`
- 阶段/代价/证据：3~5 人起（可暂不引入正式成员概念）｜中｜B

### D. 合并自动化

**D1 先决条件：必需的、可信的状态检查**
- 做法：在分支保护/ruleset 中要求指定检查通过（strict=要求分支最新，loose=不要求），并限定检查来源 App，防止任何 write 者伪造状态；`skipped`/`neutral` 按成功处理。
- 来源：`github/docs@main:content/repositories/configuring-branches-and-merges-in-your-repository/managing-protected-branches/about-protected-branches.md#L93-L115`；`content/pull-requests/reference/status-checks.md#L29-L31`
- 阶段/代价/证据：1 账号即可｜低｜A

**D2 auto-merge：最便宜的一步**
- 做法：允许 write 权限者对单个 PR 开启 auto-merge，满足分支保护要求后自动合并；前提是仓库已启用分支保护。1 账号阶段用它替代手动点合并。
- 来源：`github/docs@main:content/repositories/configuring-branches-and-merges-in-your-repository/configuring-pull-request-merges/managing-auto-merge-for-pull-requests-in-your-repository.md#L17-L23`
- 阶段/代价/证据：1 账号起｜低｜A

**D3 merge queue：可用性与前置条件**
- 做法：需要组织拥有的仓库（公开免费；私有仅 GHEC）；CI 必须响应 `merge_group` 事件（三方 CI 监听 `gh-readonly-queue/{base}` 前缀临时分支）；分支保护规则不能使用 `*` 通配符；可调 build concurrency（1~100）、merge limits（1~100）、status timeout；FIFO + 组合验证可消除「各自绿、合起来挂」。
- 来源：`github/docs@main:data/reusables/gated-features/merge-queue.md`；`content/repositories/configuring-branches-and-merges-in-your-repository/configuring-pull-request-merges/managing-a-merge-queue.md#L23-L49,L51-L92`
- 阶段/代价/证据：合并排队/撞车成为瓶颈后｜中~高（CI 改造+组织迁移）｜A（功能）+C（规模阈值）

**D4 bors/Tide 与「不上自动化」的对照**
- 做法：Tide 按标签条件把 PR 放入池、批量测试、重跑过期测试，并写「为什么还不能合」的状态；rust bors 用 `automation/bors/try`、`automation/bors/auto` 分支做 try/auto 构建，权限来自 Team API（try/review）。不上自动化的 OpenSSL 则硬限流：不接受成批提交、单 PR 不捆无关修复、最多同时 3~4 个开放 PR、超出可无评审关闭；合并需至少 2 名 Committer 批准并等 24 小时。
- 来源：`kubernetes/community@main:contributors/guide/owners.md#L247-L260`；`rust-lang/bors@main:README.md#L27,L33-L58`；`openssl/openssl@master:CONTRIBUTING.md#L22-L43,L69-L77`
- 阶段/代价/证据：多贡献者｜高（bors/Tide）/低（限流约定）｜A

### E. PR 卫生与合规

**E1 小 PR / 拆 PR（可机器预警）**
- 做法：k8s 的原则是 100 个小而明确的 PR 胜过 10 个不可评审的巨石；跨整个仓库、触及多个 OWNERS 的改动必须拆。rust 给出量化：甜点区 150~250 行改动，超过 300 行自动警告（排除 tests）。
- 来源：`kubernetes/community@main:contributors/guide/pull-requests.md#L197-L235,L256-L285`；`rust-lang/rust-forge@main:src/triagebot/large-pull-requests.md#L1-L24`
- 阶段/代价/证据：1 账号起（写进模板/机器人）｜低｜A

**E2 自动生成/AI 生成 PR 必须披露，Owner 有权拒绝**
- 做法：k8s 要求说明生成方式（全局替换/linter/AI），并明确评审成本大于收益时可直接关闭；Godot 要求 AI agent 贡献在标题加机器人标识并在描述披露；trivial 修改建议先通读整文件，不要为单个错别字开 PR。
- 来源：`kubernetes/community@main:contributors/guide/pull-requests.md#L583-L625`；`godotengine/godot@master:CONTRIBUTING.md#L11-L14`；`rust-lang/rust-forge@main:src/how-to-start-contributing.md#L49-L55`
- 阶段/代价/证据：1 账号起（Owner 含 AI，建议对等披露）｜低｜A

**E3 DCO/CLA：签名合规护栏**
- 做法：DCO 用 commit 的 `Signed-off-by`（邮箱须与作者一致）+ 必需状态检查强制执行；probot/dco 支持 `require.members:false` 让组织成员免签、个人账户仓库 owner 免签，以及 write 权限者 override；也可用 CLA（k8s 用 EasyCLA）——两者选一。
- 来源：`probot/dco@main:README.md#L8-L11,L28-L32,L64-L78,L137-L138`；`docs/required-statuses.md#L6-L17`
- 阶段/代价/证据：1 账号起（先决策）｜低｜A

## 3. 分阶段落地建议表

表中【强】=平台可强制、建议尽早开；【约】=流程约定/文档；触发条件除注明 A 级外均为建议阈值（C 级），需用本项目 2~4 周数据校准。

| 阶段 | 开启什么 | 触发条件 | 机器可查项 |
|---|---|---|---|
| S0 现在：1 账号 | 【强】公开仓库的 protected branch/ruleset + 必需状态检查（lint/build/test）；【约】PR/issue 模板 + 小 PR/拆分/AI 披露条目；【约】标签 triage、good first issue、help wanted；【约】DCO 或 CLA 二选一 | CI 能在可接受时长内稳定变绿 | `GET /repos/{o}/{r}/branches/{b}/protection`；`GET /repos/{o}/{r}/rulesets`；`GET /repos/{o}/{r}/labels`；`GET /repos/{o}/{r}/codeowners/errors` |
| S1 准备第二人 | 【约】把仓库迁到 GitHub Organization；建 OWNERS/CODEOWNERS 骨架 + CONTRIBUTING.md；第二人权限只给 Write | 已确定第二个真人贡献者 | 仓库 owner.type 是否为 Organization；文件存在性 API；collaborator permission API |
| S2 2 账号 | 【强】Require approvals=1；Require review from Code Owners；Dismiss stale approvals（或 Require approval of most recent reviewable push）；Do not allow bypassing；【约】不给 Admin/Maintain | 第二人能独立完成一次实质评审 | protection API 的 `required_pull_request_reviews.*`、`enforce_admins` |
| S3 2~3 人 | 【强】团队 auto assignment（先用 round robin）+ Busy 排除；【约】每周固定 triage 时间；【约】休假 = 移出 OWNERS / 关 rotation | 出现 PR 超过 3 天无人认领，或同一人承担多数评审 | PR/review 计数（GET /pulls + reviews）；团队 code review 设置（多为 UI） |
| S4 3~5 人 | 【约】三级角色写入子目录 OWNERS；晋级条件与提名流程；good first issue 质量标准；【强】开启 auto-merge | 出现第二个 Approver；合并动作开始排队 | 审批/review 计数；OWNERS diff；`allow_auto_merge` 设置 |
| S5 多贡献者且合并频繁 | 【强】merge queue（组织仓库 + CI 支持 `merge_group`）；【强】大改动 2 approvals；【约】merge queue 参数（build concurrency、merge limits、status timeout）调优；【约】PR 大小预警（超过 300 行，排除 tests） | 主干冲突或 CI 排队成为实际瓶颈 | 分支保护的 Require merge queue；`merge_group` workflow；rulesets API；CI 运行时长 |
| S6 长期健康 | 【约】emeritus/不活跃清理；组织 owner 至少 2 人；季度评审集中度复盘；SLO 与升级路径 | 有人离开或连续 3 个月不活跃 | org members API；OWNERS diff；review 分布统计 |

功能可用性提示：CODEOWNERS/分支保护/rulesets 在公开仓库 Free 可用，私有仓库需 Pro/Team 及以上；merge queue 仅组织拥有的仓库（公开免费，私有需 GHEC）。来源见 A2/A5/D3 的引用。

## 4. 反模式与对策

| 反模式 | 症状/机器可查 | 对策 | 来源 |
|---|---|---|---|
| 复查瓶颈（单点） | 一个账号承担大部分 review；PR 等待时长上升 | 自动指派 + 容量上限 + rotation；把评审响应写进期望；不活跃者移出 OWNERS | k8s owners L18-L21/L232-L245；rust queue L29-L38 |
| 橡皮图章（自批或同人一次完成 lgtm+approve） | 同一人在同一 PR 既 review 又 approve；作者自己的 PR 被自己批准 | 平台层作者不可自批；流程层要求 lgtm 与 approve 由不同人完成；把 approver 的自 lgtm 列为警告 | github request-changes-tips；k8s owners L211-L231 |
| 超大 PR / 全仓库扫描式改动 | 单 PR 改动行数远超阈值；跨多个 OWNERS 目录 | 拆 PR；超过 300 行自动警告（排除 tests）；模板要求说明生成方式与范围 | k8s PR L197-L235/L256-L285；rust large L1-L24 |
| 维护者过载 | 开放 PR 数持续超限；review 队列积压 | 明确 WIP 上限（OpenSSL 3~4 个）；容量上限 C；Busy/vacation；必要时无评审关闭 | openssl L22-L43；rust queue L29-L38 |
| 僵尸 PR / 作者失联 | PR 长期无作者更新 | 机器人 90 天关闭；定期 triage | k8s owners L242-L245 |
| Reviewer 失联 | 被指派的 review 长期无响应 | 允许 /unassign 后改派；Busy 状态自动不派；emeritus 清理 | k8s owners L232-L240；expectations L47-L58 |
| AI/批量生成 PR 洪水 | 大量低价值、超大、无说明的 PR | 强制披露（k8s 说明生成方式；Godot 机器人标识）；保留关闭权；提高 good first issue 门槛以减少噪声 | k8s PR L601-L625；godot L11-L14 |
| 权限过粗 / 单人 bus factor | 只有 1 个 admin；write 者可手动打 lgtm 标签绕过流程 | 最小权限；组织 owner 至少 2 人；把可手动绕过的路径收敛到自动化合并 | k8s permissions L73-L84；github roles L55-L57 |
| 流程与合并脱钩（靠人点按钮） | 满足条件却长期不合并；或 admin 手动合并绕过保护 | auto-merge/merge queue/Tide；开 Do not allow bypassing；合并状态由机器人解释 | github auto-merge；k8s owners L201-L209 |

## 5. 未核实清单

- 具体规模阈值：没有一手文档给出「多少 PR/多少人必须上 merge queue」的定量门槛；第 3 节触发条件为基于一手材料的建议（C 级）。
- 本项目仓库现状：是否已有 CONTRIBUTING/CODEOWNERS/CI/标签，未核实（本条目写作时未读取本项目仓库）。
- 法律与合规：DCO 与 CLA 对 Bloodborne 同人 Mod（涉及第三方 IP）的适用性，一手文档只描述机制，不构成法律意见。
- kubernetes/community 的 `github-management/setting-up-cla-check.md` 只确认存在，未逐行核对。
- rust-lang/team 的 `[permissions]` 完整权限模型（try/review 之外）只从 schema 与 bors README 片段推断，未通读 forge 的 team-maintenance 全文。
- GitHub 团队 auto-assignment 的设置是否全部可通过 REST API 读写，未核实（官方文档只给 UI 路径）。
- 各引用行号基于本次取回的默认分支快照（main/master）；GitHub Docs 使用 reusable 模板，行号可能随仓库更新变化，复核时建议用具体 commit 或重新列目录。
- 本报告未验证 Bloodborne 联机 Mod 自身的 CI 时长与合并频率，因此第 3、4 节的触发阈值需用本项目数据校准。
