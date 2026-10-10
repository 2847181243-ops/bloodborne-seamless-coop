# REF-006 build_in_harmonyos 评审与协作增量（对照既有提炼文档）

> **用途**：只记录 `docs/standards/参考-build_in_harmonyos-可复用经验.md` **未覆盖**的评审 / 协作机制。
> **前置阅读**：`docs/standards/参考-build_in_harmonyos-可复用经验.md`（第一轮提炼）。
> **用在哪些决策**：Issue #40；S0 的评审清单、审计分级、意见闭环、反馈闭环。
> **它不是要求**：与任务书、`MAINTAINERS.md`、`AI_POLICY.md` 冲突时以后者为准。
> **最后更新**：2026-10
> **证据等级**：Confirmed = 实际读到一手文件；Likely = 多份一手材料交叉推断；Unverified = 未找到一手来源。
> **引用格式**：`<owner>/<repo>@<分支>:<path>`。

> 只读来源：`build_in_harmonyos`（gitcode.com/OpenHarmonyPCDeveloper/build_in_harmonyos，HEAD）。Windows 上仓库含非法文件名，取文件用 `--no-checkout` + `git show`；本条目不复制大段原文。
> 已读：`.gitcode/{review-memory,PULL_REQUEST_TEMPLATE,code-style}.md`、`SKILL.md`、`CONTRIBUTING.md`、`KNOW_YOUR_WHY.md`、
> `docs/{TEST_QUALITY_SPEC,ADAPTATION_GUIDE}.md`、`templates/{audit-prompt,积分赛事PR模板,test-report,build-report,skill-feedback}.md`、
> `scripts/{pre_pr_audit.py,gitcode_pr.py}`、`scripts/ohc/{preflight,submission}.py`（部分）、
> `references/{handoff-protocol,failure-handling,context-trimming-protocol,iron-rules}.md`（部分）、`knowledge/ARCHIVE-SPEC.md`、
> `ci/{scripts/preflight_check.sh,scripts/junk_check.sh,scripts/knowledge_check.sh,conan/hooks/pre-commit,conan/build_and_test.sh}`、
> `tests/{run_all,test_smoke}.py`、`feedback/` 抽读 6 份、`SUMMARY.md` 头部。
> 本文只写 `docs/standards/参考-build_in_harmonyos-可复用经验.md` 未覆盖或需修正的内容；未标注即不作为事实。

## 1) 结论摘要（建议采用 5 条）

1. **评审标准要做成"可整篇注入的短清单"**：`.gitcode/review-memory.md`（30 条、9.5KB）由 `pre_pr_audit.py` 原样塞进审计子任务的输入 JSON。理由：评审依据不靠 Agent"记得"，靠机械注入；我们缺的正是"评审依据可复现"。
2. **审计 = 隔离上下文子任务 + 分级仲裁表 + 证据三元组**：审计输出 `{verdict,P0-P3,rule_id,file,line}`，`templates/audit-prompt.md` 明列"哪些门禁才算阻断"（ki18n #7155 教训）。理由：我们 `docs/audit` 双人签核只验格式、不验属实。
3. **审计 pass 不得盖过机器硬闸（R52 交叉校验）**：pass 结论须附被检文件 sha256；preflight 发现"审计 pass 但硬闸命中"直接判「审计失真」一票否决（#2204/#4237/#4321）。理由：让"判断"与"事实"互相校验，而不是互相替代。
4. **AI 检视意见逐条闭环必须工具化（RV-27）**：accept=回复"已修复"+标记已解决；reject=在讨论串内回理由但**不**自标已解决；只剩"正在生成摘要"=检视未完成、不许合并。理由：我们的 Agent 互评缺"闭环判据"，"看过"会被当成"处理过"。
5. **反馈报告是规则的输入源（R45 四道防线 + 主编按批核验）**：每次流程自动产 feedback；主编分批"全量核验→落地框架修复→规则/backlog 分流"（v12.17 一次核验 20 份）。理由：我们的 C-/任务书教训目前没有固定归宿。

## 2) 逐条发现

**F1 评审记忆 = 带 front-matter 的"技能文件"，整篇注入审计上下文**
- 做法：`review-memory.md` 30 条分五类（欺骗性行为/配方质量/文件规范/PR 合规/SKILL_FEEDBACK），每条尽量挂先例（ding-libs PR#7133、#2204/#4237/#4321、PR6096、#2831 等）。
- 出处：`.gitcode/review-memory.md`（"欺骗性行为检测（最高优先级，一律 request changes）"；front-matter `name: ohos-build-review-rules`）。
- 新增点（相对已有文档）：已有文档只提"PR 单目标/双角色"；此处新增"评审规则本身以 skill 文件形态版本化 + 全量注入"这一机制。
- 映射：**直接可用** —— 建 `docs/standards/review-memory.md`，短、按读者意图分类、每条附本项目先例编号（C-xxx / PR #）。
- 证据：Confirmed（读到原文 + `pre_pr_audit.py:load_audit_rules` 全文读取）。

**F2 "评审"实际由四个主体分层完成，判据各不相同**
- 做法：① 提交者本人先跑 W4 隔离子任务审计；② 平台 bot（atomgit-bot）出变更摘要+行级检视意见；③ 真人评审按 `pc-pr-review` 13 条规则判定（反馈样本：CodexBai，rule1~rule11/rule13 PASS、rule12 FAIL）；④ 主编用 `ohc-run.py review` 本地审核后 Squash 合并/Close。
- 出处：`CONTRIBUTING.md`（"主编审核"节）；`scripts/gitcode_pr.py`（monitor-review/get-review-comments/handle-review-opinion 等子命令）；`feedback/SKILL_FEEDBACK_009f9300.md`（"人工评审（CodexBai）按 pc-pr-review 13 条规则判定"）。
- 新增点：已有文档只讲"协作者提 PR→主编审核"；新增"提交前自审、远端 AI 检视、远端真人规则评审"三层且判据不同。
- 映射：**需改造** —— 我们同一个人演多角色，可保留"提交者自审 + 独立复核 + Lead 终审"的判据分离，但自动化那层的角色需自建。
- 证据：Confirmed（四处交叉）。

**F3 审计模板里的"门禁阻断性分级表"——防止把非阻断项误判 P1**
- 做法：`templates/audit-prompt.md` 第 0 步先读分级表：欺骗类/R34/R9/R8/R19/R40=阻断；L0 结构缺失/`file://` 残留/PF7-PF9/R51 申报不符=阻断；**L4 安全 warn、L5 存量遥测=非阻断**（记 P2，注明"不阻断发布"）；审计自身的风格建议=标注 `suggestion`（无规则出处）或 `rule_ref`。
- 出处：`templates/audit-prompt.md`（"先读门禁阻断性分级表（防止把非阻断项误判为 P1 引发返工，ki18n PR #7155 教训）"）。
- 新增点：已有文档讲了失败码与门禁分级；新增"**审计侧**也要做阻断性仲裁，并给非规则建议打 suggestion/rule_ref 标签"。
- 映射：**直接可用** —— 我们的审计报告应加一张"哪些 FAIL 阻断、哪些只记观察项"的表，且要求每条结论标 `rule_ref`。
- 证据：Confirmed。

**F4 R52 的可执行细节（比已有文档更具体）**
- 做法：① `.audit-result.json`/`ci-metadata.json` 等自审文件必须由工具生成、禁止手写；② pass 结论附被检文件 sha256（冻结审计时点的内容）；③ preflight 内自动交叉校验"审计 pass vs PF5/PF7/PF8 硬闸命中"→追加「审计失真」并 fail-closed；④ 审计 issue 必须带 rule_id + file/line 以便交叉复核。
- 出处：`references/iron-rules.md` R52；`scripts/ohc/preflight.py:603`（"语义：verdict=pass 允许携带 P2/P3 警告，但本闸…"）。
- 新增点：已有文档只有"审计失真一票否决"一句；新增"sha256 冻结 + 工具生成 + 交叉校验实现位置 + 结构化字段"。
- 映射：**直接可用**（低改造成本）—— audit 报告升级为 JSON（带 severity/rule/文件哈希），preci 里加一条"审计 pass 与硬闸结果矛盾即 fail"。
- 证据：Confirmed。

**F5 AI 检视闭环规范 RV-27；同时暴露"评审工具本身没被评审"**
- 做法：检视意见无论对错必须逐条响应；不合理也要回复说明、由审核人决定是否标记已解决；工具 `handle-review-opinion --action accept|reject` 强制"回复挂回讨论串 + 仅 accept 自标 resolved"。
- 出处：`.gitcode/review-memory.md` 第 27 条（RV-27）；`scripts/gitcode_pr.py:698`（docstring："AI 只需传入 action 和 reason，脚本保证流程正确"）。
- 新增点：已有文档未覆盖"检视意见闭环"；另外发现真实缺陷——`cmd_monitor_review` 组装输出时引用未定义的 `unreplied_opinions`（第 1066 行），必抛 `NameError`，`submit` 的 AI Review 监控因此不可用；`feedback/SKILL_FEEDBACK_d0c4dfef.md` 独立复现并同样记录。
- 映射：**需改造** —— 闭环口径可直接用；同时提醒我们：**流程咽喉工具必须进回归测试**，否则整条评审链静默失效。
- 证据：Confirmed（代码 + grep 无赋值 + 两份反馈交叉）。

**F6 远端人工评审的规则正文不在仓库，导致"本地全绿、远端判负"**
- 做法：`pc-pr-review` 13 条规则（rule1=模板/前缀匹配、rule6=检视时间戳≥最新提交、rule12=必须有"鸿蒙特有测试/消费者用例"，且"断言清单≠完整消费者用例"）只被引用、正文不落库；作者只能从失败中反推，再用 feedback 建议"把 rule12 加进本地门禁/写进模板"。
- 出处：`feedback/SKILL_FEEDBACK_d0c4dfef.md`（"pc-pr-review 审查规则（2026-09-28 首轮 FAIL 后追加）"）、`..._009f9300.md`、`..._609bde13.md`（"rule12 没有本地门禁…建 PR 后才被评审判不通过，整轮检视和 CI 作废"）。
- 新增点：已有文档未涉及"评审规则可见性"问题；这是"远端判据不对称"的先例。
- 映射：**需改造** —— 我们的评审规则（C-xxx/审计清单）必须与本地预检同源；若引入外部/人工评审，先要求其规则入库并给出本地可跑的近似判据。
- 证据：Confirmed（多份反馈原文）/规则全文 Unverified（见 §4）。

**F7 PR 模板有"评审相关必填项"，且由工具自动填充 + 占位符门禁**
- 做法：PR 模板必填'上游 vs 适配后测试用例统计表、是否修改上游测试、修改逐条说明、产物验证表（编译/产物存在/ELF/运行测试/动态依赖）、测试结论与已知限制、验证命令'；`submit` 从 `conandata.yml/conanfile.py/test-report.md/notes.md/patches/manifest.yaml` 自动填充，残留 `(请填写)` 直接阻断（提交门禁 + KC2 占位符检测）。
- 出处：`.gitcode/PULL_REQUEST_TEMPLATE.md`；`.gitcode/code-style.md` 第 22 条；`scripts/ohc/submission.py`（"PR body completeness gate (no (请填写) allowed)"）。
- 新增点：已有文档提了"上游测试三步法"；新增"模板字段结构化 + 自动填充 + 占位符硬拦"这一组合，以及"申报表是评审第一入口"。
- 映射：**直接可用** —— 我们的 PR 模板加"验证等级/上游 vs 本地测试对比/已知限制"字段，并把"残留占位符=fail"写进 preci。
- 证据：Confirmed。

**F8 `pre_pr_audit.py` 的机器/人分工清晰，但自带一个路径错位缺陷**
- 做法：机器只做四件事——① 取 `origin/main...HEAD` 变更文件并给 W2 原子性**警告**（不阻断）；② 加载 `code-style.md`+`review-memory.md` 全文；③ 收集文件内容与 diff stat 打包 JSON；④ 解析审计 JSON 只统计 P0/P1 并给退出码 0/2。判断本身留给"独立审计子任务"（全新上下文）。⑤ `check` 子命令要求 W3 的 `.ci-result.json`（可用 `--force` 跳过）。
- 出处：`scripts/pre_pr_audit.py`（数据流 4 步 + `verdict == "pass" and p0==0 and p1==0`）。
- 新增点：已有文档讲了 preflight 与门禁分层；新增"**审计输入生成器**"这个可复用组件，以及"人机分工：机器备料、AI 判定、机器复核计数"。
- 映射：**直接可用**（可整体搬进 `tools/preci/`）；但注意缺陷：`check` 查的是 `repo_dir/.ci-result.json`，而 W3 权威路径是 `<archives/.../版本>/.ci-result.json` → 永远报"未找到"（反馈 d0c4dfef 第 5 条独立复现）。
- 证据：Confirmed（读到代码 + SKILL.md 路径表 + 反馈交叉）。

**F9 W0–W5 六道规程 + "产物路径唯一权威"表**
- 做法：W0 依赖发布检查→W0b 历史经验核验→W1 同步 main→W2 单 PR 单软件（CI JC6/PF1 硬拦）→W3 本地 CI 模拟（`.ci-result.json`）→W4 Pre-PR 独立审计（`reports/audit-result.json`）→W5 知识完整性。并明确"错误路径不再试错"：带点前缀的 `.audit-result.json` 是错的，submit 报错直接指出正确路径（task 3659 教训）。
- 出处：`docs/ADAPTATION_GUIDE.md`（W0–W5 表）；`SKILL.md §0.3` 与"产物路径（唯一权威，不要试探）"表。
- 新增点：已有文档覆盖 preflight/单 PR/知识完整性；新增"**产物路径的唯一权威表** + 工具主动纠错"这一防返工机制。
- 映射：**直接可用** —— 给我们的 Agent 定一张"任务书/审计报告/测试报告/结果回执各写哪个路径"的表，并让 preci 检出错位时报正确路径。
- 证据：Confirmed。

**F10 权限边界存在"灰区"，且有在仓库里登记的处置先例**
- 做法：审查规则 21 禁止协作者改 `ci/`，但 `ci/config/build_timeouts.txt` 是"CI 唯一超时通道"，文件头注释明写"属 R21 灰区"；实践中协作者在 PR 中新增超时条目被 bot 判 P2 越权，反馈结论=移除改动、附构建耗时证据、由维护方独立提交条目。
- 出处：`ci/config/build_timeouts.txt:66`；`feedback/SKILL_FEEDBACK_e92f7471.md`（"《review-memory.md》第 21 条文件权限边界"）；`..._17b89b62.md`（rule5/rule6 因并入 `ci/` 改动 FAIL）。
- 新增点：已有文档只给"不可碰清单"；新增"**灰区要在代码里显式登记 + 有既定处置路径**"，而不是让每次评审临时博弈。
- 映射：**需改造** —— 我们 `branch-policy.yml` 的 SCOPE 表可补"灰区文件"列，注明"谁可以改、走哪条路"。
- 证据：Confirmed。

**F11 HANDOFF 结构化交接 + 上下文裁剪协议（脚本→Agent 的多人协作接口）**
- 做法：脚本失败时输出定长交接块：`EXIT_REASON/EXIT_CODE(2 可恢复,1 致命)/STEP_FAILED/RESUME_CMD/KNOWLEDGE_HIT/GAP/LOG_FILE/AI NEXT ACTIONS`；R14 明确"脚本失败=手动模式入口，非终止"，修好后 `--resume-from=StepN` 继续。上下文裁剪 4 步：沉淀知识→notes.md 写索引行→声明可遗忘→后续先查图谱（保留"问题一句话+最终方案+索引号"）。
- 出处：`references/handoff-protocol.md`、`references/iron-rules.md` R14、`references/context-trimming-protocol.md`。
- 新增点：已有文档提过"preflight/止损"；新增"**可恢复 vs 致命**的显式退出码 + 续跑命令 + 交接字段"以及"调试过程可遗忘、索引必留"的上下文管理。
- 映射：**直接可用** —— 我们的 Agent 失手/接管场景可直接用这套字段（我们的 `--resume` 等价物是任务书断点）。
- 证据：Confirmed。

**F12 反馈闭环的机械细节：不是"写报告"，而是"四道防线 + 主编核验批次"**
- 做法：R45 四道工具化防线——① PR 创建瞬间与 result 草稿同步生成 feedback **骨架**（幂等）；② HANDOFF/CI 红绿/pr_created 经单一咽喉自动追加**事件时间线**；③ 终态写盘时自动补骨架（失败任务同样产经验，防幸存者偏差）；④ 闭环后自动 commit+push 进 PR；AI 只填 4 个建议占位符。主编侧按批核验（v12.17：20 份全量核验→5 项框架修复+2 项规则决议+backlog；落地结果回写 SKILL.md 变更日志），报告可事后追加"复审轮补充"（d0c4dfef）。
- 出处：`references/iron-rules.md` R45；`templates/skill-feedback.md`；`SKILL.md` v12.17/v12.20 变更日志；`feedback/SKILL_FEEDBACK_d0c4dfef.md`（"复审轮补充（2026-09-29）——响应评审意见"）。
- 新增点：已有文档完全没有"反馈如何变成规则"的机制；新增"自动骨架+事件累积+终态兜底+按批核验+变更日志回写"完整链。
- 映射：**需改造**（去掉平台钩子，保留机制）—— 建 `docs/feedback/` + 事件时间线 + Lead 定期核验批次，把教训分流为"约束/Cheat-sheet/backlog"。
- 证据：Confirmed。

**F13 编号与失败码存在多套并存且已漂移（已有文档的"加失败码"建议需补一句）**
- 做法/现象：至少四套编号同时在用——`iron-rules.md` R1–R52；`review-memory.md` 自己的编号（R8/R9/R34/R52 与 W2）；CI 实现里的 `L0a-L0n/L0m/L0n/L0p`；仓库外 `pc-pr-review rule1–13`。且**同名不同义**：spec 的 `L0b`=test conanfile，pre-commit 钩子的 `L0b`=sha256；spec 写失败码 `invalid_patch_format`/`env_var_dependency`/`version_conflict`/`missing_commands_json`，CI 实际输出 `bad_patch_format`/`shell_env_var_in_run`/`version_not_incremented`/`commands_json_r38`。项目自己踩过编号冲突：v12.17 的"R49 回执完整性"与既有 R49 冲突，v12.20 重编号为 R50 并规定"以 iron-rules.md 为权威"。
- 出处：`docs/TEST_QUALITY_SPEC.md`（L0 表）vs `ci/conan/hooks/pre-commit`（"L0a: conanfile.py…L0b: sha256"）vs `ci/conan/build_and_test.sh`（`L0_ERRORS="…bad_patch_format"`）；`references/iron-rules.md` R50 的"编号说明"；`.gitcode/review-memory.md` 第 5 条（"口径以 references/iron-rules.md R8 为唯一权威（ding-libs PR #7133 教训）"）。
- 新增点：已有文档建议"门禁加稳定失败码"；新增反例约束——**失败码/编号必须单源生成或有测试比对**，否则文档与实现会各说各话。
- 映射：**直接可用** —— 我们若给 C-xxx/门禁加码，需同时定"唯一权威文件 + 文档-实现一致性测试"。
- 证据：Confirmed。

**F14 机器可读元数据 + 防伪哈希**
- 做法：CI 为每个包写 `ci-metadata.json`（`source_trace{url,sha256}`、`build_trace{profile,hostname,timestamp,runner,commit}`、`test_report{L0,L1,L2,L4,L5:{result,detail}}`）；`.ci-result.json` 带 integrity_hash，submit Gate 2 校验不匹配即判"疑似伪造"并拒绝；状态文件禁止提交（JC1 拦截运行时状态文件）。
- 出处：`ci/conan/build_and_test.sh`（`cat > "$METADATA_FILE"` 段）；`scripts/ohc/submission.py:1013`（"integrity_hash 不匹配（疑似伪造）"）；`ci/scripts/junk_check.sh` JC1。
- 新增点：已有文档提过"偷懒检测/破坏性反向验证"；新增"**结果文件本身的可信性**用哈希 + 机器元数据来保"。
- 映射：**需改造** —— 我们的测试/审计报告可加"内容哈希 + 环境指纹（commit/runner/时间）"，防止 Agent 复用旧报告。
- 证据：Confirmed。

**F15 回归测试写法：用"事故命名 + 源码字符串断言"锁住流程管道**
- 做法：`tests/test_v12xx.py` 按版本号套件 + `tests/run_all.py` 一命令全回归；`test_smoke.py` 的类名直接写事故来源（`TestEditorAudit202609`："v12.17 主编反馈审计批次的防回归"），并断言"源码里必须含三种 git 措辞""submission.py 不得再引用 `knowledge/graph/draft`"等；`test_v1220.py` 用假的 `.audit-result.json` 测 PF 交叉校验行为。
- 出处：`tests/run_all.py`、`tests/test_smoke.py`（`test_commit_nochange_wordings`/`test_submit_paths`/`test_preflight_accepts_str_repo_dir`）。
- 新增点：已有文档未涉及测试组织；新增"**把每条事故/反馈落成一条可回退断言**"，成本极低。
- 局限（同一份材料反证）：这套测试**没覆盖** `monitor-review`（F5 的 NameError 才会漏出），说明"评审链的关键子命令必须点名进回归"。
- 映射：**直接可用** —— 我们的 preci/审计脚本可加"源码契约断言 + 每案一条回归"。
- 证据：Confirmed。

**F16 知识/杂物门禁：草稿泄漏与 PR 范围三档**
- 做法：KC1 拦"草稿泄漏正式库"（正式 `E###.kv` 含 `draft: true`/`contributor_id`/文件名带 draft）；KC5 校 `index.tsv` 行数=`.kv` 数；JC1–JC4 拦运行时状态/二进制/tarball/构建产物；JC5 用"隐藏文件白名单"（新增工作流标记必须先登记，未登记一律 fail）；范围选取三档 `prdiff > incremental > full`，并记录盲区（PR #3145 `.build-closed` 漏拦：`git apply` 不产生 commit、新文件不进 index、gitignore 文件 status 也看不见）。
- 出处：`ci/scripts/knowledge_check.sh`（KC1/KC5）、`ci/scripts/junk_check.sh`（JC5_ALLOWED、三档说明）。
- 新增点：已有文档提过"知识先草稿"和"索引必须同步"；新增"**草稿泄漏是被机器检出的**"+"PR 变更范围取值本身就是一类可失败的判据"。
- 映射：**需改造** —— 我们的 preci 可加"范围正确性"检查（无已提交改动时不要全仓扫描——反馈实证 full 模式会命中他包历史遗留而误拦）。
- 证据：Confirmed。

**F17 选型/立项阶段也有评审：历史经验核验 + CI 能力矩阵（PR6096 教训）**
- 做法：适配前先读历史 notes.md 并做两项核验——① 环境语义可迁移性（"设备原生成功"不等于"CI clean 环境可复现"；mcfly 的 rustc 案例 → CI 无 Rust 工具链则 `env_unsupported` 止损，禁止硬编）；② 历史绕坑命令必须逐项比对遵循。配套一张 CI 能力矩阵（Rust❌ / OCaml 需 tool_requires / gcc-clang-cmake OK / 冷门工具链先小包试跑）。
- 出处：`SKILL.md §0.3` W0b（"历史经验核验 + CI 能力矩阵（选型/立项阶段，PR6096 教训）"）。
- 新增点：已有文档的"上游测试三步法"在市场/测试阶段；此处新增"**选型阶段的前置评审**"，把止损点提前到动手之前。
- 映射：**需改造** —— 我们做"参考先例可用性"判断时同样要问"这条先例是在什么环境、什么规模下成立的"。
- 证据：Confirmed。

## 3) 明确"不适用"清单

- **Conan/制品仓族规则**（R26/R32/R33/R36/R37/R38/R49、L0j/L0k、upload 安全边界、cnb.cool 制品仓）：我们是 Windows 原生游戏进程/模组工程，不发布 Conan 包；只保留其"边界写死在门禁里"的思路。
- **GitCode / AtomCode / atomgit-bot 平台接口**：我们环境是 GitHub + 本地 Agent，`gitcode_pr.py` 的 API/评论协议不可移植；但"检视意见逐条闭环 + 不自标已解决"的口径可借。
- **积分赛事 PR 模板与标题前缀规则（【积分赛】等）**：平台运营产物，与交付无关。
- **主编/协作者双角色权限模型**：本项目是同一个人扮演多角色，权限分离只是纪律、不是机制；直接照搬会制造假闸门。
- **2500 包规模的知识图谱/草稿引擎/E 编号体系**：规模与领域都不匹配，已有文档已判定"过早"，本研究未发现新理由推翻。
- **zsh/readelf/conan/`${CC}` 等 Linux 工具链条目**：Windows + MSVC/bash(Git Bash) 环境不可用。
- **`pc-pr-review` 的 rule12"鸿蒙特有测试"字面要求**：概念依赖鸿蒙/上游测试语境，不可照搬；可借的是其底层判据"断言清单 ≠ 端到端消费者用例"。

## 4) 未核实清单

1. `review-memory.md` 的**维护责任人/更新流程**无明文（只能从 SKILL.md v12.17/v12.20 变更日志推断为"主编在框架 PR 中更新"）→ Likely，流程 Unverified。
2. `pc-pr-review` 13 条规则的**正文不在本仓库**，仅在 feedback 中被引用（可确认其存在与 rule1/rule6/rule12 的行为；早期反馈写"11 规则"、后期写"13 条"，规则集本身在演进）→ 规则全文 Unverified。
3. `review-memory.md` 无"上次更新日期/复审周期"字段 → 是否存在过期复审机制 Unverified。
4. `feedback/` 共 302 个文件，我只抽读 6 份；未见统一的"已处理/未处理"状态字段（SKILL.md v12.17 称 20 份已核验并清理内容）→ 整体闭环率 Unverified。
5. 主编本地 `ohc-run.py review` 的判定逻辑未逐行读（`scripts/ohc/submission.py` 2516 行，只读了 docstring/门禁段）→ Unverified。
6. v12.20 自报指标"新闸+扩面可在建 PR 前拦截约 60% bot 检视意见量、门禁类失败 529→65 条/PR 段"是项目自述，未独立复算 → Unverified（但**把评审意见量当指标**这一做法本身 Confirmed）。
