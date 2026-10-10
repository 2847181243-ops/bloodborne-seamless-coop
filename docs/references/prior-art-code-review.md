# REF-004 代码复查机制（跨项目先例核对）

> **用途**：为「复查薄弱」问题提供外部先例：一次有效复查包含什么、哪些机器可查、哪些必须独立的人、AI 作者怎么披露、变更后怎么作废重审。
> **用在哪些决策**：Issue #40 的分阶段方案；S0 的机器可查项；`docs/standards/协作与复查-改进方案.md`。
> **它不是要求**：与任务书、`MAINTAINERS.md`、`AI_POLICY.md` 冲突时以后者为准；先例只用来回答「还有没有更优做法」。
> **最后更新**：2026-10
> **证据等级**：Confirmed = 实际读到一手文件；Likely = 多份一手材料交叉推断；Unverified = 未找到一手来源。
> **引用格式**：`<owner>/<repo>@<分支>:<path>`。

- 检索方式：仅 GitHub 官方仓库文件（GitHub API contents）；未采用博客/二手转述；分支名经 API `default_branch` 核实。
- 引用格式 `repo@branch:path`；下文 `…` 省略同仓库长路径（首次出现处给完整路径）。
- 证据等级：Confirmed=原文直读；Likely=由原文可合理推得；Unverified=未取到。
- 

## 1) 结论摘要（最值得采用的 5 条）

1. **先启用"不依赖人数"的机器门禁**：必需 PR、必需状态检查（可限定来源 App）、要求解决全部对话、线性历史、签名提交、禁止强推、merge queue。单账号即可用，等价 k8s Tide / Chromium Gerrit / Rust bors 的硬门禁。（对应问题 2）
2. **保留 reviewer→lgtm 与 approver→approve 两个独立角色，但不给自己开自批后门**：k8s 作者不得 `/lgtm` 自己的 PR；Chromium 的 Gerrit 已禁止 self-code-review 绕过；现在把 OWNERS/CODEOWNERS 与"至少 1 个非作者批准"写好，第二个账号一到位即启用。（对应问题 3）
3. **把 AI 作者身份显式化并转成可检查项**：PR 描述强制披露（k8s）、只允许人类 Signed-off-by + Assisted-by（Linux）、`llm-assisted` 标签 + 预先约定复查者（Rust）、🤖 标题前缀（Godot）。均可 CI/label 检查。（对应问题 5）
4. **用"变更后作废 + 重审"对抗橡皮图章**：GitHub dismiss stale approvals / 最近一次 push 须他人批准 / 要求对话解决；Prow 新提交自动撤 lgtm；Linux 补丁实质变化移除 Reviewed-by；Chromium Owners-Override 对自己 CL 不够，仍需另一位 committer。（对应问题 4）
5. **先解决"没人看"再谈严格**：leaf 目录分层 OWNERS（Chromium/k8s）、reviewer 轮换/容量/休假（Rust triagebot）、响应时限（Google 1 个工作日、Chromium 3 轮/工作日、k8s 期望）。没有有带宽的复查者，门禁必然形式化。（对应问题 1）

机制条目共 28 条（A 11 + B 7 + C 5 + D 5）。机器可查清单见第 2 节 B；必须人的判断见 A、C1、C3、C5 与 D 的实质部分。

## 2) 逐条机制

### A. "一次有效复查"的可操作要素

- **A1 复查标准**：以整体代码健康为准；CL 只要明确变得更好就应倾向批准，除紧急情况外不得放行让代码健康变差的改动；不追求完美，追求持续改进。
  - 来源：`google/eng-practices@master:review/reviewer/standard.md`（"favor approving" / "continuous improvement" / "worsen"）
  - 判定：必须人 ｜ 单账号：自评可用但不计独立复查 ｜ 代价：低 ｜ 等级：Confirmed
- **A2 看什么**：设计→功能（含并发/竞态）→复杂度→测试→命名→注释→风格→一致性→文档；且"每一行"都要看懂，看不懂就要求作者澄清，不能假装看过。
  - 来源：`google/eng-practices@master:review/reviewer/looking-for.md`（"Every Line" / "Don't accept CLs that degrade" / "a human must ensure that tests are valid"）
  - 判定：必须人 ｜ 单账号：可作第一遍自审 ｜ 代价：中 ｜ 等级：Confirmed
- **A3 顺序与粒度**：先读描述与主文件，重大设计问题立即提，避免后续白做；一个 CL 只做一件事，约 100 行合适、1000 行通常过大，评审者可仅因过大直接拒绝并要求拆分。
  - 来源：`google/eng-practices@master:review/reviewer/navigate.md`、`review/developer/small-cls.md`（"one self-contained change" / "100 lines" / "reject ... too large"）
  - 判定：判断必须人；行数/文件数可机器标注 ｜ 单账号：可自审 ｜ 代价：低 ｜ 等级：Confirmed
- **A4 速度与轮次**：不在专注任务中打断自己；一个工作日内必须首次响应；典型 CL 一天内可多轮；LGTM with comments 仅在明确条件下使用且要说明意图。
  - 来源：`google/eng-practices@master:review/reviewer/speed.md`（"One business day is the maximum" / "LGTM With Comments"）
  - 判定：时限可机器度量/提醒，是否达标必须人 ｜ 单账号：可执行 ｜ 代价：低 ｜ 等级：Confirmed
- **A5 评论与异议**：评论代码不评论人；解释为什么；用 Nit/Optional/FYI 标注强制程度；解释要落到代码/注释而非只留评论区；作者可礼貌反驳，僵持则升级而不是挂着。
  - 来源：`google/eng-practices@master:review/reviewer/comments.md`、`review/reviewer/pushback.md`、`review/developer/handling-comments.md`（"Label comment severity" / "Don't let a CL sit around"）
  - 判定：必须人 ｜ 单账号：可用于自审与未来协作规范 ｜ 代价：低 ｜ 等级：Confirmed
- **A6 紧急通道要窄**：只有"小改动 + 生产/安全/法务硬截止"才算紧急，可压缩流程但事后必须补完整复查；"想赶发布/周五想合完"等不算。
  - 来源：`google/eng-practices@master:review/emergencies.md`（"What Is NOT An Emergency?" / 事后 "more thorough review"）
  - 判定：必须人 ｜ 单账号：易滥用，需预先定义清单 ｜ 代价：低 ｜ 等级：Confirmed
- **A7 两阶段双角色（k8s）**：reviewer 看质量与正确性并 `/lgtm`；approver 看整体接受度（兼容性、API/flag、跨特性依赖）并 `/approve`；"接受贡献至少需要一位 approver 加上被指派的 reviewer"。
  - 来源：`kubernetes/community@main:contributors/guide/owners.md`（"two-phase" / "at least two suggested reviewers"）、`community-membership.md`（"at least one approver in addition to the assigned reviewers"）
  - 判定：标签/角色可机器校验，质量判断必须人 ｜ 单账号：作者若在 OWNERS 可自 `/approve`，但不能自 `/lgtm`，仍拿不到 lgtm 门禁 ｜ 代价：中 ｜ 等级：Confirmed
- **A8 Rust 的 r+**：必须由懂该代码的人给出；"不要给你不熟悉的代码 r+"；功能发生实质变化通常需要重审；PR 不会在没有 Rust 团队批准时合入；不允许手工合并。
  - 来源：`rust-lang/rust-forge@main:src/compiler/reviews.md`（"Expectations for r+" / "another set of eyes"）、`rust-lang/rustc-dev-guide@main:src/pr-lifecycle.md`（"r+" / "never merged by hand"）
  - 判定：权限与队列机器可查，是否真懂必须人 ｜ 单账号：来源未明说禁止自 r+，但自 r+ 等于没有第二双眼睛，不应视为有效复查 ｜ 代价：中 ｜ 等级：Confirmed
- **A9 Linux 的署名声明**：`Reviewed-by` 有正式声明（做了技术复查、问题已反馈且答复满意、无已知阻塞）；补丁实质变化要移除旧 `Reviewed-by`；`Tested-by` 只声明测过；`Acked-by` 只是同意合入。
  - 来源：`torvalds/linux@master:Documentation/process/submitting-patches.rst`（"Reviewer's statement of oversight" (a)-(d)；substantial change 时移除 tag）、`5.Posting.rst`（tag 定义）
  - 判定：trailer 可机器校验，声明真实性必须人 ｜ 单账号：自签 Reviewed-by 等于自审冒充担保，应禁止或仅作记录 ｜ 代价：低 ｜ 等级：Confirmed
- **A10 谁看/几个人**：Chromium 每个被改目录都必须由 owner 复查；非 committer 的提交需 2 名 committer `Code-Review+1`，作者已是 committer 时至少再 1 名其他 committer。k8s 自动建议至少 2 名 reviewer、每个 leaf OWNERS 一名 approver。
  - 来源：`chromium/chromium@main:docs/code_reviews.md`（"an owner must provide a review for each directory" / "two committers"）、`kubernetes/community@main:contributors/guide/owners.md`（"at least two suggested reviewers"）
  - 判定：平台可强制计数，实质复查必须人 ｜ 单账号：不可满足 ｜ 代价：中 ｜ 等级：Confirmed
- **A11 何时必须拦**：不得合入降低代码健康的改动（非紧急）；新增复杂度必须本次清掉，"以后清理"几乎不会发生；与复查者无法达成一致要升级，不能耗着。
  - 来源：`google/eng-practices@master:review/reviewer/standard.md`、`review/reviewer/pushback.md`（"clean up ... now" / escalation）、`torvalds/linux@master:Documentation/process/6.Followthrough.rst`（忽略复查意见的补丁 "go nowhere"）
  - 判定：阻塞判断必须人；hold/draft 可机器拦 ｜ 单账号：可对自己 hold（无法替代他人拦） ｜ 代价：低 ｜ 等级：Confirmed

### B. 机器可查（单账号现在就能做）

- **B1 GitHub ruleset 基础门禁**：要求所有变更走 PR、必需状态检查（可限定来源 App）、要求解决全部对话、线性历史、签名提交、禁止强推、merge queue。
  - 来源：`github/docs@main:content/repositories/configuring-branches-and-merges-in-your-repository/managing-rulesets/available-rules-for-rulesets.md`（"Require a pull request before merging" / "Require status checks" / "Require merge queue"）
  - 判定：机器可查 ｜ 单账号：可用（把必需审批数设为 0） ｜ 代价：低 ｜ 等级：Confirmed
- **B2 审批类规则（等第二人）**：必需审批 0–10 人；可要求 code owner 批准、要求"最近一次 push 的批准人不是 push 者"、新提交时 dismiss 过期批准；还可按路径要求指定 team 的批准（`Required reviewers`，组织/企业版功能，最多 15 个 team）。
  - 来源：`github/docs@main:…/managing-rulesets/available-rules-for-rulesets.md`（"Require approval from someone other than the last person to push" / "dismiss stale"）、`github/docs@main:…/customizing-your-repository/about-code-owners.md`（"Require review from Code Owners"）
  - 判定：机器可查 ｜ 单账号：不可满足（作者不能批准自己的 PR，见第 3 节），会永久阻塞；应写进配置但暂不启用 ｜ 代价：中（阻塞） ｜ 等级：Confirmed
- **B3 OWNERS / CODEOWNERS 分层**：按目录/模式自动请求 owner 复查；k8s 支持 reviewers / approvers / required_reviewers / labels / emeritus；Chromium 要求优先找 leaf owner；GitHub CODEOWNERS 放 `.github/`、root 或 `docs/`，同一行才并列多个 owner，owner 必须有写权限。
  - 来源：`kubernetes/kubernetes@master:OWNERS`（filters/required_reviewers 实例）、`kubernetes/community@main:contributors/guide/owners.md`、`chromium/chromium@main:docs/code_reviews.md`、`github/docs@main:…/about-code-owners.md`
  - 判定：机器可路由/校验 ｜ 单账号：可先建表（自指也能收请求，但批不了） ｜ 代价：低 ｜ 等级：Confirmed
- **B4 标签门禁与自动作废（Prow 模式）**：`lgtm` + `approved` 双标签、`do-not-merge/hold`、WIP 阻止自动合并；新提交自动移除 lgtm（tree hash 变化）；作者不能 `/lgtm` 自己的 PR。
  - 来源：`kubernetes-sigs/prow@main:pkg/plugins/lgtm/lgtm.go`（"Author cannot LGTM own PR" / tree hash）、`kubernetes/community@main:contributors/guide/owners.md`（Tide `labels`/`missingLabels`）
  - 判定：机器可查 ｜ 单账号：PR 永远缺 lgtm → 不会自动合并（安全默认） ｜ 代价：中（需自建等价 CI/脚本） ｜ 等级：Confirmed
- **B5 合并队列**：入队后对合并结果再跑全部必需检查；Rust 用 bors 全平台测试且禁止手工合并；GitHub merge queue 可要求队列每项通过检查并设超时。
  - 来源：`rust-lang/rustc-dev-guide@main:src/pr-lifecycle.md`（"merge queue" / "never merged by hand"）、`github/docs@main:…/available-rules-for-rulesets.md`（"Require merge queue"）
  - 判定：机器可查 ｜ 单账号：可用 ｜ 代价：中 ｜ 等级：Confirmed
- **B6 透明性字段的自动检查**：Linux `Signed-off-by` 只能人类、AI 贡献用 `Assisted-by`；k8s 要求 PR 描述披露 AI、禁止 AI co-author / `assisted-by` trailer / AI 生成的 commit message；Rust 用 `llm-assisted` 标签；Godot 要求 AI 在标题前加 🤖 并在描述写明（该条目前在 HTML 注释内）。
  - 来源：`torvalds/linux@master:Documentation/process/coding-assistants.rst`（"MUST NOT add Signed-off-by"）、`kubernetes/community@main:contributors/guide/pull-requests.md`（"## AI Guidance"）、`rust-lang/rust-forge@main:src/policies/llm-usage.md`（"llm-assisted"）、`godotengine/godot@master:CONTRIBUTING.md`（🤖 / AI disclosure）
  - 判定：标题/label/trailer 可机器检查，真实性必须人 ｜ 单账号：完全可用 ｜ 代价：低 ｜ 等级：Confirmed（Godot 注释状态为例外）
- **B7 复查工作量与轮换（多人后）**：Rust triagebot 按路径表自动指派、支持 `r?`、vacation、rotation on/off、队列容量上限、community reviews 至少 2 个社区批准后才指派；k8s Blunderbuss 自动指派 reviewer；Chromium 明确"负荷不可持续就扩 owner，不要用 slow/emeritus 劝退"。
  - 来源：`rust-lang/rust-forge@main:src/triagebot/pr-assignment.md`、`src/triagebot/review-queue-tracking.md`（"capacity" / "rotation mode" / "minimum_approvals"）、`chromium/chromium@main:docs/code_reviews.md`（"expand the number of owners"）
  - 判定：机器可查 ｜ 单账号：可先把路径表/OWNERS 写好，多人时立即生效 ｜ 代价：低-中 ｜ 等级：Confirmed

### C. 复查质量与反橡皮图章

- **C1 LGTM 的语义责任**：LGTM 必须意味着"此代码达到标准"，要花够时间确认；评审者要自问"我是否有赶着批准的压力 / 我是否足够公正"。
  - 来源：`google/eng-practices@master:review/reviewer/speed.md`（"certain their LGTM means..."）、`rust-lang/rust-forge@main:src/compiler/reviews.md`（"Do I feel pressure to quickly approve?"）
  - 判定：必须人 ｜ 单账号：不可自证独立 ｜ 代价：低 ｜ 等级：Confirmed
- **C2 变更后旧批准失效**：新 push 会 dismiss 旧批准；可要求最后 push 由他人批准；Prow 树哈希变化即撤 lgtm；Linux 实质变化移除 Reviewed-by；Rust 功能变化重审。
  - 来源：B2/B4/A8/A9 同源；`github/docs@main:data/reusables/repositories/request-changes-tips.md`（"the approval is dismissed"）
  - 判定：机器可强制 ｜ 单账号：可用 ｜ 代价：低 ｜ 等级：Confirmed
- **C3 角色门槛与可追责**：k8s reviewer/approver 有明确资格统计（member ≥3 月、primary reviewer ≥5/10 个实质 PR、reviewed/merged ≥20/30）；Chromium owner 需 committer + ≥3 月 + 90 天内有实质贡献 + 有带宽；Rust r+ 是受权限约束的动作。
  - 来源：`kubernetes/community@main:community-membership.md`、`chromium/chromium@main:docs/code_reviews.md`（"Expectations of owners"）、`rust-lang/rust-forge@main:src/compiler/reviews.md`
  - 判定：资格事实可机器统计，实质判断必须人 ｜ 单账号：可先定义门槛 ｜ 代价：中 ｜ 等级：Confirmed
- **C4 例外通道必须仍有第二人**：Chromium 的 `Owners-Override` 不能单独作用于自己的 CL，仍需另一位 committer LGTM 或（仅对干净 revert）bot 的 `Bot-Commit+1`；Rubber Stamper 永不给 OWNERS 批准，只对可程序证明 benign 的文件自动通过，理由是避免"任何人静默 revert 安全补丁"。
  - 来源：`chromium/chromium@main:docs/code_review_owners.md`（"Rubber Stamper never provides OWNERS approval" / "abuse vector"）、`docs/code_reviews.md`（"Owners-Override by itself is not enough on your own CLs"）
  - 判定：例外规则机器可查，是否属于例外必须人 ｜ 单账号：不可用 ｜ 代价：中 ｜ 等级：Confirmed
- **C5 质量度量**：本报告引用的各项目政策文档都没有给出"自动检测橡皮图章"的指标；能自动化的只有批准是否存在/是否过期/由谁给出/耗时/负载与资格计数。质量靠署名声明 + 变更重审 + 资质/带宽约束间接保证。
  - 来源：上述全部；owner 维护与不活跃移除（`chromium/chromium@main:docs/code_reviews.md` "Removal of owners"；`kubernetes/community@main:contributors/guide/owners.md` "unresponsive"）、Rust triage 2 周主动跟进（`rust-lang/rustc-dev-guide@main:src/pr-lifecycle.md`）
  - 判定：部分机器可查（事实），质量本身必须人 ｜ 单账号：只能记录事实 ｜ 代价：低 ｜ 等级：Confirmed（"没有质量指标"是原文缺失，不是未核实）

### D. AI 作者/复查（来源明确覆盖的部分）

- **D1 k8s**：可用 AI 辅助，但作者必须理解每一处改动并披露；禁止 AI co-author/co-signer、`assisted-by`/`co-developed` 等 trailer，禁止大型 AI 生成 PR 和 AI 生成 commit message；不得把 AI 改动的首轮复查留给 reviewer；回复评论不得依赖 AI，否则关闭 PR。
  - 来源：`kubernetes/community@main:contributors/guide/pull-requests.md`（"## AI Guidance"）
  - 判定：部分可 CI 检查，责任必须人 ｜ 单账号：可用 ｜ 代价：低 ｜ 等级：Confirmed
- **D2 Linux**：AI agent 不得添加 `Signed-off-by`（DCO 只能人类认证）；人类提交者负责审查全部 AI 代码并自负责任；用 `Assisted-by: LLM [tools]` 归属；工具生成内容要披露工具/输入/prompt/测试；维护者可额外审查、降优先级或直接拒收。
  - 来源：`torvalds/linux@master:Documentation/process/coding-assistants.rst`、`generated-content.rst`（"assume these guidelines apply" / maintainer discretion）
  - 判定：trailer 可机器检查，法律认证必须人 ｜ 单账号：可用 ｜ 代价：低 ｜ 等级：Confirmed
- **D3 Rust LLM 实验条款**：LLM 创建的 PR 必须预先与复查者约定（新贡献者必须先找到同一复查者）、打 `llm-assisted` 标签、非关键/高质量/充分测试/充分复查；成员 review 不能替代作者自审，作者在每次改动前后都要自审；LLM review bot 必须用独立、明确标注 LLM 的账号，维护者一次性批准，评论非阻塞，人类明确背书后才能阻塞；6 周窗口 LLM PR 合并占比 >50% 时熔断。
  - 来源：`rust-lang/rust-forge@main:src/policies/llm-usage.md`（"Experiment: LLM-created code changes intended for review" / "Circuit breaker"）
  - 判定：label/比例可机器统计，约定与背书必须人 ｜ 单账号：不可用（核心是"预先约定的人类复查者"） ｜ 代价：中 ｜ 等级：Confirmed
- **D4 GitHub 平台**：Copilot code review 默认不计入必需审批，文档明确要求"必须补充人类复查"；未归属个人的 Copilot PR 在预览规则下会自动多要求 1 个审批；若启用 Copilot 自动批准（预览），新 push 后其批准也会被 dismiss。
  - 来源：`github/docs@main:content/copilot/concepts/agents/code-review.md`（"do not count toward required approvals" / "Supplement ... with a human review"）、`github/docs@main:…/available-rules-for-rulesets.md`（"Additional approval for unattributed Copilot pull requests"）
  - 判定：平台规则机器可查 ｜ 单账号：可作为非阻塞辅助，但不能充当人类批准 ｜ 代价：低-中 ｜ 等级：Confirmed（preview 状态）
- **D5 来源未覆盖、需要自定**：AI 作者账号是否算"独立的人"、AI reviewer 的独立账号命名/权限、自审清单与证据、何时强制第二人、AI PR 占比阈值、紧急例外由谁批。Rust/k8s/Linux 只给了原则，没有覆盖"AI 作为作者 + 单账号仓库"的完整方案。

## 3) 专节："作者 = 复查者"冲突

- **平台硬约束**：GitHub 明确"PR 作者不能批准自己的 PR"（`github/docs@main:data/reusables/repositories/request-changes-tips.md`）。单账号下"必需审批 ≥1"永远无法满足——不要用自批/机器人绕过，应把它当作"等第二人"的开关。
- **自审 ≠ 独立复查**：GitHub 官方单账号教程鼓励先自审（`content/get-started/start-your-journey/reviewing-your-proposed-changes.md`）；`content/pull-requests/concepts/helping-others-review-your-changes.md` 要求"先复查自己的 PR"。Rust 明确"成员的 review 不能替代作者自审"（`llm-usage.md`）。但两者都只把自审当作者侧质量步骤，不计入独立批准。
- **必须第二人的机制**：GitHub 必需审批 / code owner 批准 / "最近 push 须他人批准"（B2）；Prow lgtm（作者禁止自 lgtm，B4）；Chromium owner 审批 + 第二 committer（A10、C4）；k8s reviewer + approver 两阶段（A7）。
- **单账号可用补偿（从强到弱）**：
  1. 机器门禁拉满（B1）：PR 模板、必需检查、对话解决、线性历史、签名、禁强推、merge queue——不依赖人数。
  2. 显式 `draft`/`hold`/`do-not-merge` 标签，禁止自己合并（B4）；把"可合并"状态留给未来他人。
  3. 独立 AI reviewer 账号仅作非阻塞建议：Rust 规定 bot 必须独立账号、公开标注、维护者一次性批准、评论非阻塞、人类背书后才阻塞（D3）；GitHub Copilot 默认不计审批（D4）。不要把它伪装成独立人类复查。
  4. 强制自审与披露清单：PR 描述必填"我理解了哪些、测了什么、AI 用在哪"（k8s/Linux/Godot 模式，B6），并用 CI 检查标题/label/trailer。
  5. 现在就写好 OWNERS/CODEOWNERS 与规则配置，但审批数保持 0；第二账号加入当天改为 approvals≥1 + last-push approval + dismiss stale（B2/B3/B7）。
  6. 明确例外与升级人：紧急通道窄定义（A6），override 仍需另一人见证（Chromium C4）；争议升级而不是挂着（A11）。
- **反面教训**：k8s 明确指出 approver 的 `/lgtm` 同时被当作 `/approve` "违背至少两双眼睛"的初衷（`owners.md` Quirks）；Chromium 明确 self-code-review 绕过已被 Gerrit 禁止（`docs/code_review_owners.md` 开头）。因此"作者本人也是 approver"永远不能算独立复查。

## 4) 未核实清单

- **Godot review guidelines 页面**：`godotengine/godot-docs` 的 `contributing/development/workflows` 路径 404，未取到 review_guidelines 正文；`godotengine/godot@master:CONTRIBUTING.md` 的 AI 披露条款目前在 HTML 注释 `<!-- ... -->` 内，是否正式生效需确认 → Unverified。
- **Chromium OWNERS 插件规范**：`docs/code_reviews.md` 把 OWNERS 语法细节指向 Gerrit 上游 `plugins_code-owners` 文档，本次未读取该仓库 → Unverified（本报告引用的 Chromium 规则均来自 chromium/chromium 官方 docs，等级 Confirmed）。
- **Linux "reviewing patches" 文件**：`Documentation/process/reviewing-patches.rst` 在当前 master 返回 404；评审规则分散在 `submitting-patches.rst`、`6.Followthrough.rst` 等，未找到单一 reviewer guide → Unverified（是否存在替代文件未穷尽）。
- **Rust "reviewer 轮换政策"**：triagebot 的 rotation mode / queue capacity / vacation 已核实；但没有找到官方"强制任期/轮换"条款 → 未核实是否存在。
- **GitHub "未归属 Copilot PR 额外审批"**：官方文档标注 public preview，可能变化 → 状态未定。
- **Copilot 自动批准**：官方一处说默认不计入必需审批、可开启为计入（preview），另一处 reusables 说 Copilot 批准"不影响合并要求"；存在版本/设置差异 → 需按目标仓库设置确认，本报告只采信"默认不计入 + 必须补人类复查"。
- **评审质量指标**：未找到任何一手项目定义"橡皮图章率/复查深度"的量化指标 → 来源未覆盖，需要本项目自定（建议只做事实记录：批准人、时间、diff 版本、变更后是否仍有效）。
- **AI 专门条款**：k8s/Linux/Rust/Godot/GitHub 有 AI 政策；Google、Chromium 在本次已读文件中没有 AI 专门条款 → 对"AI 作者 + 多人"的完整流程，来源未覆盖，需要我们自己定。
