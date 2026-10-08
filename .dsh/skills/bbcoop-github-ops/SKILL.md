---
name: bbcoop-github-ops
description: 本仓库的 GitHub 自动化运维手册——标签唯一来源、4 个 Issue 表单、3 道 PR 门禁、分支保护配置、gh CLI 命令、首次推送与占位符替换流程。当你要配置或维护这个仓库、建 Issue/PR、排查 CI 红灯、设置分支保护、同步标签或执行推送时加载。
---

# GitHub 自动化运维（AI-00）

---

## 1. 唯一来源表（改东西之前先看这里）

| 内容 | 唯一来源 | 消费者 |
|---|---|---|
| 60 个标签 | `.github/labels.yml`（`name\|color\|description`） | `.github/workflows/labels.yml` 推送到仓库 |
| 所有权 | `.github/CODEOWNERS` + `branch-policy.yml` 里的 `SCOPE` | 分支保护、CI |
| PR 必填章节 | `.github/PULL_REQUEST_TEMPLATE.md` | `pr-guard.yml` |
| 审计结论标签 | `audit:passed` / `audit:warning` / `audit:failed` | `pr-guard.yml` |
| 目录结构要求 | `.github/workflows/docs-structure.yml` 的 `required` 数组 | CI |

⚠️ **改标签必须改 `.github/labels.yml`**，直接在网页上改会在下次同步时被覆盖（`gh label create --force`）。

---

## 2. 三道 PR 门禁

| 门禁 | 工作流 | 阻断条件 |
|---|---|---|
| 结构 | `docs-structure.yml` | 29 个必需文件缺任一 |
| 所有权 | `branch-policy.yml` | 分支名不匹配 `agent/<a0..c4>-*` 或 `lead/*`；改动文件越出该 Agent 范围 |
| 内容 | `pr-guard.yml` | PR 描述缺 7 个必填章节；或触及 `mod/security/*`、`mod/network/*`、`relay/*`、`tools/audit/*`、`tools/pentest/*` 却没有 `audit:passed`/`audit:warning`；或带了 `audit:failed` |

`build.yml` 在仓库没有 `CMakeLists.txt` 时自动跳过（不会红叉），有代码后自动在 `windows-latest` 上编译。
另有 `labels.yml`（标签同步）与 `issue-triage.yml`（Issue 自动标注路由）。

---

## 3. 分支保护 / 规则集（推送后立刻配）

> **优先用 Rulesets**（Settings → Rules → Rulesets），因为 **Copilot code review
> 只能作为 ruleset 规则自动触发**；经典分支保护的 `/branches/main/protection` 不支持它。
> 两者可以并存，同时存在时取更严格者。若走 UI/API 建规则集，**本步骤需要
> `administration:write` 权限**，deploy key（只能读写 git 对象）做不到。
>
> 注意：本仓库**当前 0 个规则集、分支保护状态未验证**（查询见下）。

### 必须设为必需的检查上下文（与 job `name:` 逐字一致）

```text
必需文件与目录结构            docs-structure.yml
分支命名与写入范围            branch-policy.yml
PR 模板必填章节               pr-guard.yml
安全审计门禁（一票否决）      pr-guard.yml
文件编码与 CODEOWNERS 完整性  repo-integrity.yml
构建门禁（Windows MSVC x64）  build.yml
```

⚠️ **绝对不要**把 `build.yml` 的构建步骤拆成两个互斥 job（历史坑）：曾经写成
`Windows MSVC x64`（有代码时）与 `构建门禁（无代码时占位）`（无代码时）两个
`if:` 互斥的 job，结果无论把哪个设为必需，另一种状态下该上下文**永不出现**，
PR 永久 pending。现在已统一为单一 job，名恒定为 `构建门禁（Windows MSVC x64）`。

⚠️ `contexts` 必须与 job 的 `name:` 完全一致（含全角括号）。改过 workflow 的
`name` 后要回来同步，否则状态检查永远 pending。

⚠️ `require_code_owner_reviews: true` 与**单账号 CODEOWNERS** 组合会让唯一的仓库
账号无法合并自己开的 PR（GitHub 不允许自审）。要么加第二个协作者，要么先不开这一项。
另建议在仓库设置里勾选 **Allow auto-merge** 与 **Automatically delete head branches**。

---

## 4. 常用命令

```bash
# 标签：本地清单 → 仓库
gh label list --limit 100
gh label create "audit:passed" --color 0e8a16 --description "安全审计通过" --force

# Issue
gh issue create --template cross-agent-task.yml
gh issue list --label needs-audit
gh issue edit 12 --add-label "audit:passed"

# PR
gh pr create --base main --head agent/a1-reverse-engineering --fill
gh pr edit 34 --add-label "audit:warning"
gh pr checks 34

# 工作流排查
gh run list --limit 20
gh run view <run-id> --log-failed
gh workflow run labels.yml
```

### ⚠️ 本机 HTTPS 走 `api.github.com` 会被 Steam++ 中间人（必读）

本机 `hosts` 把 `github.com` / `api.github.com` 指向 `127.0.0.1`，而
`Steam++.Accelerator` 监听 `0.0.0.0:443` 做 TLS 中间人，其根证书
`CN=SteamTools Certificate, O=BeyondDimension` 已装入 `LocalMachine\Root`，
所以 Windows 认为它可信。**任何"正常"发出的 token 都被该代理可见。**

判定方法（`Server` 头会暴露代理）：

```powershell
# 被中间人：Server: github.com,WattToolkit
Invoke-WebRequest https://api.github.com/zen -UseBasicParsing | % Headers
# curl 的 Schannel 后端反而会拒绝该伪造证书（CRYPT_E_NO_REVOCATION_CHECK）
curl.exe https://api.github.com/zen        # -> exit 35
```

**必须用 `--resolve` 钉住真实 IP 绕过它**（已在本机封装好）：

```powershell
# ~/.config/dsh/gh-api.ps1 提供 Invoke-GitHubApi；profile 已 dot-source
Invoke-GitHubApi '/repos/OWNER/REPO/rulesets'
Invoke-GitHubApi -Method POST -Path '/repos/OWNER/REPO/rulesets' -Body $json
```

Token 存放：`~/.config/dsh/token.txt`（ACL 已收紧为仅当前用户 + SYSTEM + Administrators），
由 `~/.config/dsh/gh.ps1` 读入 `$env:GITHUB_TOKEN`。**凭据一律不得入库**（任务书 §2.4 / SECURITY.md §9）。

`gh` CLI **未安装**；若安装，也需同样处理 hosts 劫持（`gh` 走 HTTPS 同样被中间人）。

---

## 5. 首次推送（还没做完的部分）

**当前状态：remote、提交身份、占位符均已接线完成。**

```text
origin     = git@github-bbcoop:2847181243-ops/bloodborne-seamless-coop.git
user.name  = 2847181243-ops
user.email = 2847181243-ops@users.noreply.github.com
认证方式    = SSH deploy key（ED25519，私钥 ~/.ssh/id_ed25519_bbcoop）
```

### ⚠️ 本机必须走 `ssh.github.com:443`，不能用 `github.com:22`

本机 hosts 被 **Steam++（Watt Toolkit）** 劫持：`github.com` / `api.github.com` /
`*.githubusercontent.com` 全部指向 `127.0.0.1`，而本机 22 端口跑着 Windows OpenSSH Server。
直接 `ssh git@github.com` 会连到**本机 sshd**，拿到它的主机密钥
（`SHA256:sM3CgwfaYH312L5+uAsNq4EZAMk4mjoRyHy06yjN5Mc`）而不是 GitHub 的。

```text
Host github-bbcoop
    HostName ssh.github.com     # 不在 hosts 劫持列表，解析到真实 GitHub
    Port 443                    # 绕开被本机 sshd 占用的 22
    User git
    IdentityFile ~/.ssh/id_ed25519_bbcoop
    IdentitiesOnly yes
    StrictHostKeyChecking yes
```

`known_hosts` 必须使用 **GitHub 官方公布**的密钥（GitHub Docs → *SSH key fingerprints*），
**不要**用 `StrictHostKeyChecking accept-new` —— 在劫持环境下 TOFU 会把本机 sshd 的
密钥固化进 known_hosts，之后既验不过真 GitHub，也失去告警意义。

### ⚠️ HTTPS 路径不可用于认证

`Steam++.Accelerator` 监听 `0.0.0.0:443` 做 TLS 中间人，根证书
`CN=SteamTools Certificate, O=BeyondDimension` 已装入 `LocalMachine\Root`。
走 HTTPS 的一切凭据对它可见：Git 自带 openssl CA bundle 会报
`unable to get local issuer certificate`；改用 `git config http.sslBackend schannel`
能"修好"，但**那正是信任了中间人**。**认证一律走 SSH。**

### 推送

```bash
git push -u origin main
git push origin --all      # 13 条 agent/* 分支
```

> 建仓库时若勾了 "Add a README"，远端会有一个与本地**无共同祖先**的 `Initial commit`，
> 直接 push 会被拒。处理方式：`git fetch origin main` 后
> `git rebase -X theirs origin/main main`（README 冲突取本地版本）。

---

## 6. 建仓库时的坑

- **不要**勾选 "Add a README / .gitignore / license" —— 本地已有提交，勾了会 non-fast-forward。
- 私有仓库的 Actions 分钟数有限额（免费 2000 分钟/月），公开仓库不限量。当前 CI 几乎不耗分钟（都是 shell 步骤）。
- 私钥、token、游戏本体文件、玩家存档**一律不得入库** —— `.gitignore` 已经覆盖，但提交历史是永久的。

---

## 7. CI 红灯速查

| 现象 | 原因 | 处理 |
|---|---|---|
| `docs-structure` 报缺文件 | 删/改名了必需文件 | 对照 `required` 数组补回 |
| `branch-policy` 报越界 | 改了别的 Agent 的文件 | 建 cross-agent Issue，或把改动挪回本 Agent 范围 |
| `branch-policy` 报分支名 | 用了 `feature/*` 之类 | 重命名为 `agent/<id>-<topic>` 或 `lead/<topic>` |
| `pr-guard` 报缺章节 | PR 描述被清空/改写 | 用模板重新填写 |
| `pr-guard` 报缺审计标签 | 动了网络/安全代码 | 先走 `.github/ISSUE_TEMPLATE/security-audit.yml`，审计报告进 `docs/audit/`，再打 `audit:passed` |
| `labels` 报标签不存在 | 没跑过 labels 工作流 | `gh workflow run labels.yml` |
