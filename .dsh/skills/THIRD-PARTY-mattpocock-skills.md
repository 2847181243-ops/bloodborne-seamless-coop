# 第三方 skill：来源与同步

以下 skill 来自 **[mattpocock/skills](https://github.com/mattpocock/skills)**
（"Skills for Real Engineers"），**MIT 许可**，**内容未做任何修改**。

- 上游提交：`b0618bc436ad`
- 同步日期：2026-10-09
- 上游路径：`skills/engineering/<name>/`
- 许可全文：同目录 `THIRD-PARTY-mattpocock-skills.LICENSE`

## 已收录（20 个）

  - `ask-matt`
  - `code-review`
  - `codebase-design`
  - `diagnosing-bugs`
  - `domain-modeling`
  - `grill-with-docs`
  - `implement`
  - `implement-spec`
  - `improve-codebase-architecture`
  - `pr`
  - `prototype`
  - `research`
  - `retro`
  - `setup-matt-pocock-skills`
  - `tdd`
  - `to-spec`
  - `to-tickets`
  - `triage`
  - `wayfinder`
  - `wizard`

## 权威性：**非权威**

```text
权威性层级（冲突时按此裁决）
1. bloodbrone.markdown               ← 任务书 v3.0，唯一需求来源（最高）
2. bbcoop-conventions / bbcoop-re-protocol / bbcoop-github-ops   ← 项目规约
3. 本目录下引入的第三方工程流程 skill  ← 仅作方法论参考，非权威
```

本目录的 skill **不得**覆盖、放宽或替代上述任何强制条款。已在这些 skill 的
`SKILL.md` 正文开头各插入一行 `权威性：非权威` 声明，**随正文一起被加载**，
因此不依赖任何 agent 主动去读本文件。

本仓库已知与上游存在差异、且**必须以上游为错**的条目：

| 项 | 本仓库（权威） | 上游常见做法 |
|---|---|---|
| 提交信息 | `<scope>: <subject>`，scope 为 `a3-gameplay`/`ci` 等 | `type(scope):` |
| 合并方式 | squash-only + `required_linear_history` | 未限定 |
| 状态检查 | 硬编码中文上下文名（含全角括号） | 不感知 |
| 审计 | `docs/audit/` 双人签核（C3-a / C3-b） | 不感知 |
| 语言栈 | C++ / Windows x64 / MSVC（bbport） | 示例偏 TypeScript / Node |

## 搬迁时做的两处改动

### 1. 拍平目录（结构改动）

DSH 的 skill 发现**深度只有一层**（`<root>/<name>/SKILL.md`），而上游是
`skills/engineering/<name>/SKILL.md`（两层），因此整体上移一层。

> 保留的文件包括各 skill 的配套引用文件（`tdd/mocking.md`、`tdd/tests.md`、
> `codebase-design/DESIGN-IT-TWICE.md`、`wizard/template.sh` 等）以及
> `agents/openai.yaml`（面向 Codex / OpenAI harness 的元数据；DSH 不读取，但保留
> 以便与上游逐文件 diff，不做有损裁剪）。

### 2. 每个 SKILL.md 正文开头插入权威性声明（内容改动）

除拍平目录外，**唯一的正文改动**：在每个引入的 `SKILL.md` 的 frontmatter 之后
插入如下一行块（共 20 处）：

```text
> **权威性：非权威（第三方，上游未修改内容的通用工程流程 skill）。**
> 本仓库的权威来源是 .dsh/skills/bbcoop-*（项目规约）与任务书 bloodbrone.markdown。
> **冲突时以 bbcoop-* 与任务书为准**；本 skill 不得覆盖、放宽或替代其中任何强制条款
> （例如提交信息格式、合并方式、状态检查名、docs/audit/ 双人签核、隐私红线）。
> 来源与许可见 .dsh/skills/THIRD-PARTY-mattpocock-skills.md。
```

> 为什么改正文而不是只写在本文件里：skill 正文是**被加载进上下文**的那部分，
> 本 README 不是。只写在 README 会导致「权威性」在真正用到 skill 时不可见。
> 该插入可重复执行且幂等；重新同步上游时需一并重放。

## 上游行为已保留：仅手动调用的 skill

下列 skill 带 `disable-model-invocation: true`，**只允许用户手动调用**，不会被自动触发。
DSH 识别该 frontmatter 键（已在 DSH 运行时资源中确认存在该字段处理）：

  - `ask-matt`
  - `grill-with-docs`
  - `implement`
  - `implement-spec`
  - `improve-codebase-architecture`
  - `retro`
  - `setup-matt-pocock-skills`
  - `to-spec`
  - `to-tickets`
  - `triage`
  - `wayfinder`

## 与仓库自有 skill 的关系

- 这 20 个是**通用工程流程** skill，与 `bbcoop-*`（项目规约）**互补**。
- **冲突时以 `bbcoop-*` 与任务书 `bloodbrone.markdown` 为准**（任务书是唯一需求来源）。
- 本仓库有若干**专有约束**是上游 skill 不可能知道的，例如：
  硬编码的中文状态检查上下文名、`docs/audit/` 双人签核格式、`<scope>: <subject>`
  提交规范（**不是** `type(scope):`）、squash-only、`.dsh/skills` 一层扫描限制。
  使用上游 skill 时若与之冲突，**按 bbcoop-* 执行**。

## 如何更新

1. 先读上游 `CHANGELOG.md`（该项目主动维护，且用 changesets 管理变更）。
2. 重新运行 vendor 脚本，得到新的上游 SHA 与文件差异。
3. 逐文件 review 差异后再提交，不要盲目覆盖。

## 一个必须知道的坑

上游 skill 的**示例与约定偏 TypeScript / Node 生态**（作者是 TypeScript 教育者）。
本项目是 **C++ / Windows x64 / MSVC（bbport）**，因此涉及语言特定实践的部分
（如 `setup-pre-commit`、`migrate-to-shoehorn`、测试框架选型）**不适用于本仓库**，
已刻意未收录 `skills/misc/` 与 `skills/in-progress/` 两个目录。
