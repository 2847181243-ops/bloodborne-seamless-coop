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

## 搬迁时唯一的结构改动：拍平目录

DSH 的 skill 发现**深度只有一层**（`<root>/<name>/SKILL.md`），而上游是
`skills/engineering/<name>/SKILL.md`（两层），因此整体上移一层，其余原样保留。

> 保留的文件包括各 skill 的配套引用文件（`tdd/mocking.md`、`tdd/tests.md`、
> `codebase-design/DESIGN-IT-TWICE.md`、`wizard/template.sh` 等）以及
> `agents/openai.yaml`（面向 Codex / OpenAI harness 的元数据；DSH 不读取，但保留
> 以便与上游逐文件 diff，不做有损裁剪）。

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
