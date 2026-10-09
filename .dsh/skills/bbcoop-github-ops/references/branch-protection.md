# 分支保护与规则集：完整配置与取舍理由

> 本文件是 `bbcoop-github-ops/SKILL.md` 的详解部分。
> 只在你真的要配置、修改、排查分支保护与规则集时读它。

---

## 1. 为什么用 Rulesets 而不是经典分支保护

**优先用 Rulesets**（Settings → Rules → Rulesets）：

- **Copilot code review 只能作为 ruleset 规则自动触发**；
  经典分支保护的 `/branches/main/protection` 不支持它。
- 两者可以并存，同时存在时**取更严格者**。

⚠️ 若走 UI/API 建规则集，**需要 `administration:write` 权限**。
deploy key（只能读写 git 对象）做不到。

---

## 2. 本仓库当前状态（实测）

```text
规则集名称   main-protection
id           24735428
enforcement  active
作用范围     refs/heads/main
```

包含的规则：

| 规则 | 说明 |
|---|---|
| `deletion` | 禁止删除 main |
| `non_fast_forward` | 禁止强推 |
| `required_linear_history` | 要求线性历史 |
| `pull_request` | 必须走 PR |
| `required_status_checks` | `strict=true` + 6 个必需上下文 |

`pull_request` 规则**刻意**设为：

```text
required_approving_review_count = 0
require_code_owner_review       = false
allowed_merge_methods           = ['squash']
```

**为什么审批数设 0**：CODEOWNERS 是单账号。开启后**作者无法合并自己的 PR**
（GitHub 不允许自审），仓库会彻底卡住。

> 代价：见第 4 节的坑 1 —— 审批数 0 使得一次 `PATCH` 就能把 PR 合并进 main。
> 要把"必须走 PR"变成硬约束，**需要有第二名协作者**并把审批数提到 ≥1。

---

## 3. 合并策略

```text
allow_squash_merge      = true
allow_merge_commit      = false
allow_rebase_merge      = false
allow_auto_merge        = true
delete_branch_on_merge  = true
```

规则集另含 `required_linear_history` 且 `allowed_merge_methods=['squash']`。

**推论（重要）**：

- 合并后的 main 提交信息**取 PR 标题** ⇒
  **PR 标题必须符合 `CONTRIBUTING.md` 第 3 节的 `<scope>: <subject>` 规范**；
  PR 内部各条提交信息**不进 main 历史**。
  所以把力气花在 PR 标题上，而不是纠结提交信息。
- `strict=true` 意味着**分支落后 main 时必须先同步**，否则必需检查不计入。

---

## 4. 两条实测过的坑（勿重蹈）

### 坑 1：API 快进可以绕过 `non_fast_forward` 并把 PR 合并

**实测过程**：

对 `refs/heads/probe-*` 施加 `non_fast_forward` 后：

```text
PATCH /git/refs/heads/probe-*
→ 422 Repository rule violations found / Cannot force-push to this branch
```

说明 API 路径**确实**受规则约束。

**但是**：`PATCH /git/refs/heads/main` 指向某 PR 的 head 是**快进**，
`non_fast_forward` 不适用；又因 `required_approving_review_count=0`，
GitHub 将其识别为「合并该 PR」，从而满足 `pull_request` 规则。

**后果**：一次 `PATCH` 就把 PR 合并进了 main，绕过合并按钮。

> ### ⛔ 硬禁令
>
> **严禁用 `PATCH /git/refs/heads/<受保护分支>` 做任何"试探"。**
> 这不是"看看会怎样"，而是**真的会合并**。

### 坑 2：必需检查与 `if:` 互斥的 job

**历史写法**：曾经把 `build.yml` 的构建拆成两个 `if:` 互斥的 job：

```text
Windows MSVC x64          （有 CMakeLists.txt 时运行）
构建门禁（无代码时占位）  （无代码时运行）
```

设为必需检查后有两种坏情况：

| 情况 | 后果 |
|---|---|
| 必需的是**当前不运行**的那个 job | 该上下文一直没有报告，分支保护停在 `Expected — Waiting for status to be reported` |
| 必需的是**被 `if:` 跳过**的那个 job | 按 GitHub 文档，被条件跳过的 job **报告 Success** —— 门禁被静默绕过 |

**现已统一为单一 job，名恒定为 `构建门禁（Windows MSVC x64）`。**

> ⚠️ **真正会"永远 Pending"的是被路径/分支过滤掉、整个 workflow 不触发的情况**，
> 与上面两种不同。所以这两类都要避开：
>
> | 原因 | 表现 | 例子 |
> |---|---|---|
> | workflow 不触发 | 永远 Pending | `labels.yml` 有 `paths` 过滤 |
> | job 被 `if:` 跳过 | 报告 Success（假通过） | 曾经的占位 job |
>
> **不能作为必需检查**：`同步标签`、`自动标注`、`open-pr`。
>
> 参考：<https://docs.github.com/en/actions/concepts/security/github_token>
> 与 Mergify 对 path filter 与 CI 门禁差异的说明
> <https://mergify.com/blog/path-filters-are-not-a-ci-gate>。

---

## 5. 必需检查列表的维护

必需上下文**必须与 job 的 `name:` 完全一致**（含全角括号）。

改了 workflow 的 `name` 之后要回来同步，否则状态检查永远 pending。

`tools/preci/validate_workflows.py` 会校验 `tools/preci/gates.yml` 里声明的
必需上下文与实际 job `name:` 是否一致 —— 这能挡住大部分不一致，
但**它只看文件，不看 GitHub 上的实际设置**。改完规则集要人工核对一次。

---

## 6. 仓库设置建议

勾选：

- **Allow auto-merge**
- **Automatically delete head branches**

**不要**勾选 "Add a README / .gitignore / license"（建仓库时）——
本地已有提交，勾了会产生一个无共同祖先的 `Initial commit`，push 被拒。
