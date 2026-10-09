---
name: research
description: Investigate a question against high-trust primary sources and capture the findings as a Markdown file in the repo. Use when the user wants a topic researched, docs or API facts gathered, or reading legwork delegated to a background agent.
---

> **权威性：非权威（第三方，上游未修改内容的通用工程流程 skill）。**
> 本仓库的权威来源是 `.dsh/skills/bbcoop-*`（项目规约）与任务书 `bloodborne.markdown`。
> **冲突时以 bbcoop-* 与任务书为准**；本 skill 不得覆盖、放宽或替代其中任何强制条款
> （例如提交信息格式、合并方式、状态检查名、`docs/audit/` 双人签核、隐私红线）。
> 来源与许可见 `.dsh/skills/THIRD-PARTY-mattpocock-skills.md`。

Spin up a **background agent** to do the research, so you keep working while it reads.

Its job:

1. Investigate the question against **primary sources** (official docs, source code, specs, first-party APIs), not a secondary write-up of them. Follow every claim back to the source that owns it.
2. Write the findings to a single Markdown file, citing each claim's source.
3. Save it where the repo already keeps such notes; match the existing convention, and if there is none, put it somewhere sensible and say where.
