---
name: bbcoop-github-ops
description: 本仓库的 GitHub 自动化运维骨架——标签与所有权的唯一来源、四道门禁、必需检查列表、本机网络绕行（Steam++ 中间人）、推送方式、CI 红灯速查。当你要配置或维护这个仓库、建 Issue/PR、排查 CI 红灯、设置分支保护、同步标签或执行推送时先加载本文件；需要规则集与保护的完整配置细节时再按需加载同目录下的 references/。
---

# GitHub 自动化运维（AI-00）

> **本文件是骨架，不是全部。** 详细配置按需加载：
>
> | 需要什么 | 加载 |
> |---|---|
> | 规则集完整配置、必需检查的取舍理由、合并策略、两条实测过的坑 | [`references/branch-protection.md`](references/branch-protection.md) |
> | 本机网络环境的完整诊断（中间人判定、SSH 配置、HTTPS 为何不能用于认证） | [`references/local-network.md`](references/local-network.md) |
>
> 为什么这样分：本文件每次相关任务都会进上下文，所以只留"不知道就会做错"的东西。

---

## 1. 唯一来源表（改东西之前先看这里）

| 内容 | 唯一来源 | 消费者 |
|---|---|---|
| 60 个标签 | `.github/labels.yml`（`name\|color\|description`） | `.github/workflows/labels.yml` |
| 所有权 | `.github/CODEOWNERS` + `branch-policy.yml` 里的 `SCOPE` | 分支保护、CI |
| PR 必填章节 | `.github/PULL_REQUEST_TEMPLATE.md` | `pr-guard.yml` |
| 审计结论标签 | `audit:passed` / `audit:warning` / `audit:failed` | `pr-guard.yml` |
| 目录结构要求 | `.github/workflows/docs-structure.yml` 的 `required` 数组 | CI |
| 跳过留痕 | `tools/preci/preci.ps1 -SkipReason` → `skip-trace.json` | 人工审计 |

⚠️ **改标签必须改 `.github/labels.yml`**。直接在网页上改会在下次同步时被覆盖。

---

## 2. 四道门禁

| 门禁 | 工作流 | 阻断条件 |
|---|---|---|
| 结构 | `docs-structure.yml` | 必需文件缺任一 |
| 所有权 | `branch-policy.yml` | 分支名不符 `agent/<a0..c4>-*` 或 `lead/*`；改动越出该 Agent 范围 |
| 内容 | `pr-guard.yml` | 缺 7 个必填章节；触及安全路径时缺审计标签 / 缺审计报告 / 结论不一致 / **证据哈希核对不过** / 双人签核不全 |
| 编码 | `repo-integrity.yml` | 非 UTF-8 / 含 U+FFFD / 含 GBK 残留；CODEOWNERS 规则行有问题 |

### 审计门禁的强度必须如实理解

它做四件事：要求标签、要求报告实体、校验结论与标签一致、
**校验报告里的证据行（文件路径 + sha256，CI 会重算并比对）**、要求双人签核。

它**不做**的一件事：证明结论正确。

> ⚠️ **该机制是文本层强制，不是 GitHub 平台级双人审批**
> （个人账号 + 单账号 CODEOWNERS 无法强制两名审批人）。
> **不得对外表述为平台级保证，也不得说"审计结论已被机器验证为真"。**
> 证据核对防的是"随手编一段话"，防不住"抄哈希"与"两人未独立判断"。

### 构建门禁

`build.yml` 在没有 `CMakeLists.txt` 时走「无代码时的说明」步骤并判通过；
有代码后自动在 `windows-latest` 上编译。**单一 job，上下文名恒定** —— 理由见 references。

---

## 3. 必需检查上下文（与 job `name:` 逐字一致）

```text
必需文件与目录结构            docs-structure.yml
分支命名与写入范围            branch-policy.yml
PR 模板必填章节               pr-guard.yml
安全审计门禁（一票否决）      pr-guard.yml
文件编码与 CODEOWNERS 完整性  repo-integrity.yml
构建门禁（Windows MSVC x64）  build.yml
```

⚠️ **`contexts` 必须与 job 的 `name:` 完全一致（含全角括号）。**
改过 workflow 的 `name` 后要回来同步，否则状态检查永远 pending。

⚠️ **不能作为必需检查的上下文**：`同步标签`（有 `paths` 过滤）、
`自动标注`（用 `issues` 事件）、`open-pr`（有 `paths` 过滤）。
被路径/分支过滤掉、整个 workflow 不触发时，该上下文**永远 Pending**。

> 完整取舍理由、两条实测过的坑（API 快进绕过 `non_fast_forward`、
> squash-only 与线性历史）见 [`references/branch-protection.md`](references/branch-protection.md)。
> **其中"严禁用 `PATCH /git/refs/heads/<受保护分支>` 试探"是一条硬禁令，动手前务必读。**

---

## 4. 常用命令

```bash
# 标签：本地清单 → 仓库
gh label list --limit 100
gh label create "audit:passed" --color 0e8a16 --description "安全审计通过" --force

# Issue
gh issue create --template cross-agent-task.yml
gh issue edit 12 --add-label "audit:passed"

# PR
gh pr create --base main --head agent/a1-reverse-engineering --fill
gh pr checks 34

# 工作流排查
gh run list --limit 20
gh run view <run-id> --log-failed
gh workflow run labels.yml
```

⚠️ **本机 `gh` 未安装，且即使安装也会被中间人拦截。**
本机的实际做法是用 `Invoke-GitHubApi`（已封装，profile 已 dot-source）——
见下一节与 [`references/local-network.md`](references/local-network.md)。

---

## 5. 本机网络（必读，不读就会踩）

本机 `hosts` 把 `github.com` / `api.github.com` / `*.githubusercontent.com`
指向 `127.0.0.1`，而 **Steam++（Watt Toolkit）** 监听 `0.0.0.0:443` 做 TLS 中间人，
其根证书已装入 `LocalMachine\Root`，所以 Windows 认为它可信。

**任何"正常"发出的 token 都被该代理可见。**

| 场景 | 正确做法 |
|---|---|
| REST API | 用 `Invoke-GitHubApi`（内部用 `--resolve` 钉真实 IP 绕开中间人） |
| Git 推送 | 走 `ssh.github.com:443`（SSH deploy key），**不要用 HTTPS** |
| 判定是否被中间人 | `Invoke-WebRequest https://api.github.com/zen`，看 `Server` 头是否含 `WattToolkit` |

**绝不要**用 `git config http.sslBackend schannel` "修好" HTTPS ——
那正是信任了中间人。

Token 在 `~/.config/dsh/token.txt`（ACL 已收紧）。**凭据一律不得入库**（任务书 §2.4）。

完整诊断步骤与 SSH 配置见 [`references/local-network.md`](references/local-network.md)。

---

## 6. 推送

```bash
git push -u origin main
git push origin --all      # 13 条 agent/* 分支
```

> 建仓库时若勾了 "Add a README"，远端会有一个与本地**无共同祖先**的
> `Initial commit`，直接 push 会被拒。处理：`git fetch origin main` 后
> `git rebase -X theirs origin/main main`（README 冲突取本地版本）。

**不要**勾选 "Add a README / .gitignore / license" —— 本地已有提交，勾了会 non-fast-forward。

---

## 7. CI 红灯速查

| 现象 | 原因 | 处理 |
|---|---|---|
| `docs-structure` 报缺文件 | 删/改名了必需文件 | 对照 `required` 数组补回 |
| `branch-policy` 报越界 | 改了别的 Agent 的文件 | 建 cross-agent Issue，或把改动挪回本 Agent 范围。**不要扩大自己的 SCOPE 来掩盖越界** |
| `branch-policy` 报分支名 | 用了 `feature/*` 之类 | 重命名为 `agent/<id>-<topic>` 或 `lead/<topic>` |
| `pr-guard` 报缺章节 | PR 描述被清空/改写 | 用模板重新填写 |
| `pr-guard` 报缺审计标签 | 动了网络/安全代码 | 走 `security-audit.yml`，报告进 `docs/audit/`，再打 `audit:passed` |
| `pr-guard` 报证据哈希不符 | 报告写完后文件被改过，或哈希是编的 | 重算 `sha256sum <文件>` 并更新报告 |
| `repo-integrity` 报非 UTF-8 | 文件被 ANSI/GBK 编辑器改过 | 转成 UTF-8（.ps1 需要带 BOM，见 `docs/standards/build-standards.md`） |
| `labels` 报标签不存在 | 没跑过 labels 工作流 | `gh workflow run labels.yml` |
| 检查一直 Pending | 该 workflow 被 path/branch 过滤，根本不触发 | 见第 3 节的「不能作为必需检查」清单 |

### 排查顺序（省时间）

1. `python tools/bbcoop.py check` —— 本地预检能覆盖绝大多数红灯
2. 还不行再 `python tools/bbcoop.py test` —— 回归矩阵
3. 都不是，才去看 CI 日志：`gh run view <id> --log-failed`

**不要一有红灯就重推。** 先本地跑预检 —— 它存在的唯一理由就是这个。
