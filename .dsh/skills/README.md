# .dsh/skills/ —— skill 的组织约定

> 这些 skill 是给 Agent 用的操作手册。本文件说明**怎么组织它们**，
> 避免它们越长越大、每次都把上下文吃满。

---

## 1. 为什么分层

一个 skill 的 `SKILL.md` 会在**每次相关任务开始时**被完整加载进上下文。
它越长，每次任务的固定开销越大 —— 但大部分细节在大多数任务里根本用不到。

所以：

| 文件 | 回答什么 | 何时加载 | 该有多长 |
|---|---|---|---|
| `SKILL.md` | **不知道就会做错的事** | 每次任务，自动 | 尽量短 |
| `references/*.md` | 判据细节、完整清单、条文原文 | **按需**，Agent 自己判断 | 可以长 |
| `agents/*.yaml` | 平台元数据 | 由工具读 | — |

> 这条约定借自 `build_in_harmonyos` 的 skill 组织方式
> （它的 `SKILL.md` 开头就写明"骨架 + 要点，详解按需从 references/ 加载"）。
> 见 `docs/standards/参考-build_in_harmonyos-可复用经验.md` 第 1.1 节。

### 判断一条内容该放哪

问自己：**"不看这条，我会不会做错事？"**

```text
会   → SKILL.md（骨架）
不会 → references/（按需）
```

"有可能想知道"不算"不知道就会做错"。
**倾向把东西放进 references/** —— 需要时能读到，才是关键。

### SKILL.md 里必须在场的东西

1. **什么时候该用这个 skill**（frontmatter 的 `description` 要写清触发条件）
2. **references/ 的索引表** —— 否则按需加载变成"永远不加载"
3. **硬性规则与红线**
4. **最容易搞错的那几条**

### SKILL.md 里不该有的东西

1. 完整表格、逐条清单、条文原文 → 移入 references/
2. 与仓库其他文件重复的内容 → 改为**链接**，避免两处不一致
3. 一次性任务的记录 → 那属于 `docs/`

---

## 2. 本目录的 skill 分两类

### 2.1 `bbcoop-*` —— 本仓库自己写的（权威）

| skill | 用途 |
|---|---|
| `bbcoop-conventions` | 协作规约：分支与写入范围、Issue 驱动、安全红线、验证链 |
| `bbcoop-github-ops` | GitHub 侧操作：规则集、门禁、API 调用、CI 红灯速查 |
| `bbcoop-re-protocol` | 逆向工作规程：记录格式、版本安全链、交付物清单 |

这三个是**本项目的权威说明**。与它们冲突时以任务书 `bloodbrone.markdown` 为准。

### 2.2 其余 —— 第三方通用工程流程 skill

来自 [`mattpocock/skills`](https://github.com/mattpocock/skills)（MIT），
每个 `SKILL.md` 头部有 `权威性：非权威` 标记，
来源与许可证见 `THIRD-PARTY-mattpocock-skills.md` 与 `.LICENSE`。

**它们不是本项目的规则。** 用来借用通用流程方法，
与本项目规约冲突时**以 `bbcoop-*` 为准**。

---

## 3. 改 skill 时的注意事项

1. **frontmatter 的 `name` 必须与目录名一致**，否则工具找不到。
2. **改完要跑门禁**：
   ```bash
   python tools/bbcoop.py verify
   ```
   `.dsh/` 归 AI-00，`branch-policy` 会检查写入范围。
3. **不要新增"通用最佳实践"类内容** —— 那属于第三方 skill 的范围，
   本仓库的 skill 只写**本项目特有的、不写就会做错的事**。
4. **不要在 SKILL.md 里复制 `docs/` 的内容**。复制会产生两个版本，
   迟早不一致。用链接。
