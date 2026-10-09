---
name: implement
description: "Implement a piece of work based on a spec or set of tickets."
disable-model-invocation: true
---

> **权威性：非权威（第三方，上游未修改内容的通用工程流程 skill）。**
> 本仓库的权威来源是 `.dsh/skills/bbcoop-*`（项目规约）与任务书 `bloodborne.markdown`。
> **冲突时以 bbcoop-* 与任务书为准**；本 skill 不得覆盖、放宽或替代其中任何强制条款
> （例如提交信息格式、合并方式、状态检查名、`docs/audit/` 双人签核、隐私红线）。
> 来源与许可见 `.dsh/skills/THIRD-PARTY-mattpocock-skills.md`。

Implement the work described by the user in the spec or tickets.

If the user passes a ticket reference, fetch it from the issue tracker and state its title before starting. If the reference is ambiguous, ask.

Call the Skill tool with "tdd" where possible, at pre-agreed seams.

Run typechecking regularly, single test files regularly, and the full test suite once at the end.

Once done, call the Skill tool with "code-review" to review the work.

Commit your work to the current branch.
