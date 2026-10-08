# .dsh/ —— 项目级 DSH 配置

本目录是 DSH（DeepSeek Harness）在**本项目根**读取的配置位置。项目根 = 最近的含 `.git` 的祖先目录。

---

## skills/

`<项目根>/.dsh/skills/<name>/SKILL.md` 是 skill 扫描的**最高优先级根**（rank 100）。

| Rank | 来源 | 路径 |
|---|---|---|
| 100 | `project-dsh` | `<项目根>/.dsh/skills` ← **本目录** |
| 200 | `project-agents` | `<项目根>/.agents/skills` |
| 300 | `custom` | `Config.customSkillDirs` |
| 400 | `user-dsh` | `<DSH_HOME>/skills` |
| 500 | `user-agents` | `<AGENTS_HOME>/skills` |

**规则**

- 发现深度只有一层：`<root>/<name>/SKILL.md` 或 `<root>/<name>.md`；
- frontmatter 必填 `name`（**kebab-case**）与 `description`；
- 目录 bundle 里的 `references/`、`scripts/`、`assets/` 不会被监视，改它们不触发目录刷新；
- 修改 `SKILL.md` 本体（或增删 skill）会立即刷新会话 skill 目录。

**前置条件**：profile 必须启用 skill 加载器，否则文件存在也不会出现在会话目录里：

```bash
# 在 DSH 中开启（本仓库已开启）
# entryId: include:skill-filesystem  →  enabled: true
```

---

## 本项目的 skill

### 自有 skill（任务书 v3.0 的可执行摘要）

| Skill | 用途 | 何时加载 |
|---|---|---|
| [`bbcoop-conventions`](skills/bbcoop-conventions/SKILL.md) | 协作规约：分支所有权、Issue 驱动、证据等级、版安全链、禁止事项、验证链 | 在本仓库做任何代码/文档改动之前 |
| [`bbcoop-re-protocol`](skills/bbcoop-re-protocol/SKILL.md) | 逆向记录协议：函数/地址/Hook 记录格式、版本绑定、交付清单 | 分析 bbport runtime、定位函数、写 `docs/re/` |
| [`bbcoop-github-ops`](skills/bbcoop-github-ops/SKILL.md) | 仓库自动化运维：标签、Issue 表单、PR 门禁、规则集、gh/API 命令 | 配置仓库、建 Issue/PR、排查 CI、执行推送 |

> 这三个 skill 是任务书 v3.0 的**可执行摘要**。与 `bloodbrone.markdown` 冲突时**以任务书为准**。

### 引入的通用工程流程 skill（第三方，MIT）

来自 **[mattpocock/skills](https://github.com/mattpocock/skills)**（"Skills for Real Engineers"），
上游提交 `b0618bc436ad`，**内容未修改**，仅拍平目录以适配 DSH 的一层扫描。
来源、许可、同步方式与注意事项见 [`skills/THIRD-PARTY-mattpocock-skills.md`](skills/THIRD-PARTY-mattpocock-skills.md)。
这 20 个覆盖：`code-review`、`tdd`、`implement`/`implement-spec`、`to-spec`/`to-tickets`、
`diagnosing-bugs`、`domain-modeling`、`codebase-design`、`improve-codebase-architecture`、
`research`、`retro`、`triage`、`wayfinder`、`wizard`、`prototype`、`pr`、`ask-matt`、
`grill-with-docs`、`setup-matt-pocock-skills`。

> ⚠️ **冲突时以 `bbcoop-*` 与任务书为准。** 上游不可能知道本仓库的专有约束：
> 硬编码的中文状态检查上下文名、`docs/audit/` 双人签核格式、`<scope>: <subject>`
> 提交规范（**不是** `type(scope):`）、squash-only 合并、`.dsh/skills` 一层扫描限制。
>
> ⚠️ 上游示例偏 **TypeScript / Node**，本项目是 **C++ / Windows x64 / MSVC**；
> 语言特定实践不适用，故未引入 `skills/misc/` 与 `skills/in-progress/`。

> **修改 skill 后必须重跑同步**：增删 skill 或改 `SKILL.md` 会立即刷新会话 skill 目录；
> 但 skill 目录内的 `references/`、`scripts/`、`assets/` **不被监视**，改动不触发刷新。

---

## 注意

- 本目录**随仓库提交**，所以任何在此协作的 Agent 都会自动获得同一套规约。
- 不要把会话数据、token、本机路径写进这里。
- `.dsh/` 不参与 `.github/workflows/branch-policy.yml` 的 Agent 所有权映射，默认归 AI-00 维护。
