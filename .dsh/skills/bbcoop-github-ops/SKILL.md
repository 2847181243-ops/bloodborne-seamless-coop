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

## 3. 分支保护（推送后立刻配）

```bash
gh api -X PUT "repos/$OWNER/$REPO/branches/main/protection" \
  -H "Accept: application/vnd.github+json" --input - <<'JSON'
{
  "required_status_checks": {
    "strict": true,
    "contexts": [
      "必需文件与目录结构",
      "分支命名与写入范围",
      "PR 模板必填章节",
      "安全审计门禁（一票否决）",
      "构建门禁（无代码时占位）"
    ]
  },
  "enforce_admins": true,
  "required_pull_request_reviews": {
    "dismiss_stale_reviews": true,
    "require_code_owner_reviews": true,
    "required_approving_review_count": 1
  },
  "restrictions": null,
  "allow_force_pushes": false,
  "allow_deletions": false,
  "required_conversation_resolution": true
}
JSON
```

⚠️ `contexts` 必须与 job 的 `name:` 完全一致。改过 workflow 的 `name` 后要回来同步，否则状态检查永远 pending。
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

---

## 5. 首次推送（还没做完的部分）

仓库已 init、已提交、13 条 `agent/*` 分支已建，**缺 remote 与凭据**。

```bash
git remote add origin git@github.com:<OWNER>/<REPO>.git   # 或 https://github.com/...
git commit --amend --reset-author --no-edit                # 若换了提交身份
git push -u origin main
git push origin --all                                      # 推 13 条 agent/* 分支
```

**推送前必须替换的占位符**

| 文件 | 占位符 | 处数 |
|---|---|---|
| `.github/CODEOWNERS` | `@YOUR_GITHUB_USERNAME` | 28 |
| `.github/ISSUE_TEMPLATE/config.yml` | `YOUR_GITHUB_USERNAME/REPO_NAME` | 3 |

```powershell
# 替换示例（先确认 OWNER 正确再执行）
$o='<OWNER>'; $r='bloodborne-seamless-coop'
(Get-Content .github\CODEOWNERS -Raw) -replace '@YOUR_GITHUB_USERNAME', "@$o" |
  Set-Content .github\CODEOWNERS -NoNewline -Encoding UTF8
(Get-Content .github\ISSUE_TEMPLATE\config.yml -Raw) -replace 'YOUR_GITHUB_USERNAME/REPO_NAME', "$o/$r" |
  Set-Content .github\ISSUE_TEMPLATE\config.yml -NoNewline -Encoding UTF8
```

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
