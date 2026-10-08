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

| Skill | 用途 | 何时加载 |
|---|---|---|
| [`bbcoop-conventions`](skills/bbcoop-conventions/SKILL.md) | 协作规约：分支所有权、Issue 驱动、证据等级、版安全链、禁止事项、验证链 | 在本仓库做任何代码/文档改动之前 |
| [`bbcoop-re-protocol`](skills/bbcoop-re-protocol/SKILL.md) | 逆向记录协议：函数/地址/Hook 记录格式、版本绑定、交付清单 | 分析 bbport runtime、定位函数、写 `docs/re/` |
| [`bbcoop-github-ops`](skills/bbcoop-github-ops/SKILL.md) | 仓库自动化运维：标签、Issue 表单、PR 门禁、分支保护、gh 命令 | 配置仓库、建 Issue/PR、排查 CI、执行推送 |

> 这三个 skill 是任务书 v3.0 的**可执行摘要**。与 `bloodbrone.markdown` 冲突时**以任务书为准**。

---

## 注意

- 本目录**随仓库提交**，所以任何在此协作的 Agent 都会自动获得同一套规约。
- 不要把会话数据、token、本机路径写进这里。
- `.dsh/` 不参与 `.github/workflows/branch-policy.yml` 的 Agent 所有权映射，默认归 AI-00 维护。
