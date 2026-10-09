# 本机网络环境：完整诊断与绕行方式

> 本文件是 `bbcoop-github-ops/SKILL.md` 的详解部分。
> 只在你遇到"连不上 GitHub / 推送失败 / API 报错"时读它。
>
> **为什么需要这份文件**：本机的网络环境不是默认环境 ——
> `github.com` 被本地 hosts 劫持，443 端口上跑着一个 TLS 中间人。
> 不知道这件事的人会得出完全错误的结论（例如"GitHub 挂了"或"token 有问题"）。

---

## 1. 环境事实

```text
hosts 劫持：github.com / api.github.com / *.githubusercontent.com → 127.0.0.1
中间人：    Steam++.Accelerator 监听 0.0.0.0:443，做 TLS 中间人
根证书：    CN=SteamTools Certificate, O=BeyondDimension
            已装入 LocalMachine\Root ⇒ Windows 认为它可信
22 端口：   Windows OpenSSH Server 在跑 ⇒ ssh git@github.com 会连到本机自己
```

**后果**：任何"正常"发出的凭据（token、密码）**对该代理可见**。

---

## 2. 判定是否被中间人

`Server` 响应头会暴露代理：

```powershell
# 被中间人的输出：Server: github.com,WattToolkit
Invoke-WebRequest https://api.github.com/zen -UseBasicParsing | % Headers

# curl 的 Schannel 后端反而会拒绝该伪造证书（CRYPT_E_NO_REVOCATION_CHECK）
curl.exe https://api.github.com/zen        # -> exit 35
```

**注意第二行容易误判**：`curl.exe` 报错**不代表网络不通**，
而是它比 Windows 更严格地拒绝了中间人证书。
**"curl 失败但浏览器能开"正是被中间人的特征。**

---

## 3. REST API：用 `--resolve` 钉真实 IP

```powershell
# ~/.config/dsh/gh-api.ps1 提供 Invoke-GitHubApi；profile 已 dot-source
Invoke-GitHubApi '/repos/OWNER/REPO/rulesets'
Invoke-GitHubApi -Method POST -Path '/repos/OWNER/REPO/rulesets' -Body $json
```

它内部用 `curl.exe --resolve api.github.com:443:<真实 IP>` 绕开 hosts 劫持。

⚠️ **POST/PATCH/PUT 带 body 时必须带 `Content-Type: application/json; charset=utf-8`。**
漏掉 `charset=utf-8` 会返回 **404**（不是 400）—— 这是一个很容易被误判成
"路径写错了"的坑。

`gh` CLI **未安装**；即使安装，`gh` 走 HTTPS 同样会被中间人拦截，
所以不能简单地"装上 gh 就好了"。

---

## 4. Git 推送：走 `ssh.github.com:443`

**不能用 `github.com:22`**：本机 22 端口跑着 Windows OpenSSH Server，
`ssh git@github.com` 会连到**本机 sshd**，拿到它的主机密钥
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

当前接线状态：

```text
origin     = git@github-bbcoop:2847181243-ops/bloodborne-seamless-coop.git
user.name  = 2847181243-ops
user.email = 2847181243-ops@users.noreply.github.com
认证方式    = SSH deploy key（ED25519，私钥 ~/.ssh/id_ed25519_bbcoop）
```

### `known_hosts` 必须用 GitHub 官方公布的密钥

来源：GitHub Docs → *SSH key fingerprints*。

**不要**用 `StrictHostKeyChecking accept-new`：
在劫持环境下 TOFU 会把**本机 sshd 的密钥**固化进 `known_hosts`，
之后既验不过真 GitHub，也失去了告警意义 ——
等于把这个检测机制永久废掉。

---

## 5. HTTPS 路径不可用于认证

`Steam++.Accelerator` 的根证书已在系统信任库里，所以：

| 做法 | 结果 |
|---|---|
| Git 自带 openssl CA bundle | 报 `unable to get local issuer certificate` |
| `git config http.sslBackend schannel` | "修好"了 —— **但那正是信任了中间人** |

> ### ⛔ 不要把 `http.sslBackend schannel` 当成修复
>
> 它让 Git 接受中间人的证书。失败症状消失了，安全属性也一起消失了。
> **认证一律走 SSH。**

---

## 6. 凭据存放

```text
~/.config/dsh/token.txt     ACL 已收紧为仅当前用户 + SYSTEM + Administrators
~/.config/dsh/gh.ps1        读入 $env:GITHUB_TOKEN
~/.config/dsh/gh-api.ps1    提供 Invoke-GitHubApi
```

**凭据一律不得入库**（任务书 §2.4 / `SECURITY.md` §9）。

⚠️ 本地预检的凭据扫描能抓明文与常见模式，
**抓不住 base64 内嵌的凭据**（见 `docs/standards/bypass-and-gates.md`）。
不要因为"门禁是绿的"就认为可以临时放一个进去。

---

## 7. 常见误判速查

| 现象 | 容易误判成 | 实际原因 |
|---|---|---|
| `curl.exe https://api.github.com/...` 退出 35 | GitHub 挂了 / 网络不通 | curl 拒绝了中间人证书（**它是对的**） |
| API 返回 404 | 路径写错了 | 少写 `Content-Type: ...; charset=utf-8` |
| `ssh git@github.com` 连上但主机密钥不对 | GitHub 换了密钥 | 连到了**本机 sshd**（22 端口被占） |
| 浏览器能开 GitHub，命令行不行 | 命令行工具坏了 | hosts 劫持只影响按域名解析的程序；浏览器可能在用代理 |
