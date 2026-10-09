# 本地预检（pre-CI）

> **目的：让门禁失败在本地就暴露，而不是推上去之后。**
> 本项目曾在同一轮里连续 3 次「推上去才发现红」。CI 反馈慢、消耗 Actions 额度，
> 而这些失败**绝大多数是纯本地可判定的**。

---

## 从哪开始

**不要记下面那些命令，用单一入口：**

```bash
python tools/bbcoop.py verify      # 提交前跑这个（预检 + 回归矩阵）
python tools/bbcoop.py check       # 只跑预检
python tools/bbcoop.py test        # 只跑回归矩阵
python tools/bbcoop.py doctor      # 环境与工具链现状
python tools/bbcoop.py status      # 当前分支 / 改动 / 归属
```

**退出码在整条链上含义一致**（`bbcoop.py`、`preci.ps1`、`tests/run_all.py`）：

| 码 | 含义 |
|---|---|
| `0` | 通过（含"已记录理由的跳过"，见下） |
| `1` | 有失败 |
| `2` | **有未验证项（跳过）** —— 不等于通过 |
| `3` | 无法启动（参数 / 环境问题，含跳过理由不合规） |

> `2` 是刻意独立于 `0` 的。把"本机没有工具所以跳过"当成"检查过且没问题"，
> 就是制造虚假信心。

### 带着跳过项继续：`-SkipReason`

跳过本身**无法区分**「有意为之」与「没人注意到」—— 而后者才是危险的。
所以只有在**写明了理由**的情况下，跳过才被当作可接受：

```powershell
powershell -NoProfile -File tools/preci/preci.ps1 -SkipReason "本机缺少 cmake 与 MSVC，
构建检查无法在本地复现；CI 的 Windows runner 会真编译，该跳过已写进 PR 说明。"
```

| 输入 | 退出码 | 说明 |
|---|---|---|
| 不给理由 | `2` | 跳过仍算未验证 |
| 理由太短（< 12 字） | `3` | 没说明任何东西 |
| 套话（`不适用` / `n/a` / `以后` / `todo` …） | `3` | 无信息量 |
| **实质理由** | `0` | 记录到 `tools/preci/skip-trace.json`，应写进 PR 供审计 |

**为什么套话也要拒**：如果任何跳过都能用一句敷衍放过去，这个机制就是摆设。
被拒的套话清单是硬编码的，可在 `preci.ps1` 里扩充。

`skip-trace.json` 是**运行产物**（在 `.gitignore` 里）：
内容写进 PR，文件本身不入库 —— 否则会随每次运行产生无意义的 diff。

该机制由 `tools/preci/tests/test_skip_reason.py` 覆盖
（7 个用例，含 4 种必须被拒的输入）。

---

## 文件

| 文件 | 作用 |
|---|---|
| `preci.ps1` | 预检主程序。**UTF-8 带 BOM**（理由见下） |
| `gates.yml` | 单一事实来源：每个检查、对应 CI 上下文、阶段归属、`never_require` |
| `validate_workflows.py` | 用真实 YAML 解析器校验 workflow 结构 |
| `fetch_pr.py` | 阶段 2 取 PR 描述与标签（**独立进程**，理由见下） |
| `check_style.py` | 代码规范与注释纪律（扫**已提交的 blob**，不是工作区） |
| `messages.json` | 全部中文文案（UTF-8 显式读取） |
| `tests/` | **回归矩阵** —— 见下 |
| `README.md` | 本文件 |

### `tests/` 为什么必须存在，且必须在 CI 里跑

这些用例此前只存在于开发机的临时目录，路径写死、没人重跑。

**一个从不重跑的检查等于装饰** —— 它可能早已失效而无人知道。
本项目**实测发生过**：检查里出现「自指」缺陷（要找的字符串也出现在它自己的报错
文案里），于是永远返回通过，把真正的编译选项删掉也拦不住。

现在它们：

- 在仓库里（协作者可见、可改、可加）；
- 路径全部相对推导，换机器照样跑；
- 由 `.github/workflows/pr-guard.yml` 的 `回归矩阵` job 在**每个 PR** 上执行；
- 由 `python tools/bbcoop.py verify` 在本地执行。

`tests/run_all.py` 里的 `needs` 字段声明了每个套件需要什么能力
（bash / PyYAML）。**缺能力时整套报「跳过」并计入未验证，而不是静默算通过。**

### 为什么 `fetch_pr.py` 是独立进程

取 PR 数据在 Windows PowerShell 5.1 里**连续失败三次**，每次都是不同层面的对象封送问题：

| 尝试 | 结果 |
|---|---|
| `Invoke-RestMethod` | `cannot convert PSCustomObject to Int32`（嵌套集合被索引时） |
| `[System.Net.WebRequest]` + 手工解析 | 单独跑正常，放进脚本仍触发同一转换错误 |
| `curl.exe` + `ConvertFrom-Json` | `ArgumentTransformationMetadataException`（错误指向 `ConvertFrom-Json` 那一行） |

与其继续猜 PowerShell 的封送规则，不如把取数放进一个**只负责写两个文件**的独立进程 ——
它可以单独测试，接口小到不会出错。这是刻意的工程取舍，不是偷懒。

---

## 怎么用

```powershell
# 阶段 1：push 之前必须全绿
powershell -NoProfile -File tools/preci/preci.ps1

# 阶段 2：开 PR 之后、请求合并之前必须全绿
powershell -NoProfile -File tools/preci/preci.ps1 -Stage 2 -Pr <PR 编号>

# 或者直接给一个 PR 描述文件（不需要 token）
powershell -NoProfile -File tools/preci/preci.ps1 -Stage 2 -PrBody .\pr-body.md

# 把「无法验证」也当失败（CI 前最后一次自查建议加）
powershell -NoProfile -File tools/preci/preci.ps1 -Strict
```

**退出码**：`0` 通过；`1` 有失败；`2` 通过但存在**未验证项**（`-Strict` 下视为失败）。

**为什么分两个阶段**：阶段 2 的检查依赖 PR 描述与标签，push 之前不可能有。
45 个阻断项里有 29 个属于阶段 2，硬要合成一步只能靠猜，那等于没有预检。

---

## 覆盖了什么

阶段 1（push 前，8 项）：

| # | 检查 | 对应的 CI 上下文 |
|---|---|---|
| 1 | 分支命名 `agent/<id>-<topic>` 或 `lead/<topic>` | 分支命名与写入范围 |
| 2 | 写入范围（改动是否落在该 Agent 的 SCOPE 内） | 分支命名与写入范围 |
| 3 | 文件编码（UTF-8 / 无 BOM / 无 U+FFFD / 无 GBK 误码） | 文件编码与 CODEOWNERS 完整性 |
| 4 | CODEOWNERS 完整性（规则不缩进、有所有者、必需条目齐全） | 文件编码与 CODEOWNERS 完整性 |
| 5 | workflow `run body` 通过 `bash -n`；YAML 可解析、`needs` 目标存在、无 Pending 陷阱 | （CI 隐含） |
| 6 | 必需文件与目录结构 | 必需文件与目录结构 |
| 7 | 凭据扫描（只扫 diff 新增行） | PR 模板必填章节 |
| 8 | **代码规范与注释纪律**（见下） | （暂无 CI 上下文，本地执行） |
| 9 | 构建（有 `CMakeLists.txt` 时真编译；没有则明确跳过） | 构建门禁（Windows MSVC x64） |

### 第 8 项：代码规范与注释纪律

依据**两处已有但此前无人执行**的东西，不自创标准：

| 依据 | 内容 |
|---|---|
| `.editorconfig`（早已存在） | 末尾换行、换行符、行尾空白、缩进、行宽 120。**此前没有任何门禁执行它** |
| 任务书 §38（`bloodborne.markdown:1160`） | 原文「禁止…为了消除报错而**大面积注释代码**」 |

三项检查：

1. **`.editorconfig` 合规** —— 客观规则，无判断余地。
2. **注释纪律** —— 注释掉的代码用启发式检出。**零星只告警**（可 review），
   **成片判失败**（单文件 ≥15 行，或连续 ≥5 行），对应任务书说的"大面积"。
3. **源文件必须有文件头注释** —— 说明用途与所有者。否则 §48 的所有权模型会静默腐烂：
   一个没人认领的文件，出问题时不知道该找谁。

> ⚠️ **启发式可能出错**，所以设计成"零星告警、成片才失败"。
> 它拦的是"用注释代码掩盖报错"，不是正常的技术性注释。

阶段 2（请求合并前）：PR 7 个必需章节非空、关联 Issue 真实存在（走 API）、
验证等级恰好 1 个、证据等级（触及 `docs/re/` 或 `mod/core/` 时）、
安全自检（触及敏感路径时全勾）、模块勾选、**安全审计门禁**、跨所有权授权。

---

## 设计原则（这些不是风格偏好，是踩过的坑）

### 1. 复用而非重写

阶段 1 直接使用与 CI **同源**的判据：同一份 GBK 误码标记表、同一份 SCOPE 表
（从 `branch-policy.yml` 解析）、同一份必需文件清单（从 `docs-structure.yml` 解析）。
阶段 2 更是**直接执行 `pr-guard.yml` 的 run body 原文**。

> 若各写一份，两边迟早漂移，本地过而 CI 不过 —— 那就等于没有预检。

### 2. 不说原因的失败不算失败

每个检查都必须能说出「检查了什么、依据是什么」。阶段 2 失败时会打印 CI 原始的
`::error` 行。

### 3. 不能静默通过

无法执行的检查报 **SKIP**，并计入「未验证」清单，**绝不当成通过**。
`-Strict` 会把 SKIP 变成失败。

**具体例子**：本机没有 PyYAML 时，YAML 结构校验报 SKIP 而不是 FAIL ——
否则一台没装 PyYAML 的机器看上去就像仓库坏了。这是刻意的**优雅降级**。

### 4. 读 git blob，不读工作区

CODEOWNERS 检查读的是 `git show HEAD:.github/CODEOWNERS`，不是工作区文件。
**实测原因**：本仓库 `git ls-files --eol` 报 `i/lf w/crlf` —— 仓库里是 LF，
工作区是 CRLF。读工作区会让每个空行带上 `\r`，于是空行不再匹配
`case "$line" in '')` 而被误判为「缩进规则」，产生 CI 永远不会出现的假红。

> 这是「本地过 / CI 不过」的经典来源：**要校验的是将要提交的字节，不是磁盘上的字节。**

### 5. 纯 ASCII 源码 + UTF-8 资源

`preci.ps1` **是纯 ASCII**；所有中文在 `messages.json`，用显式 UTF-8 读取器加载。

**原因**：Windows PowerShell 5.1 用 **ANSI 代码页**（本机 GBK）解码**无 BOM** 的文件，
会把中文字面量全部搞乱并导致解析失败。加 BOM 不是可靠方案 —— 常见编辑工具会静默剥掉它。
纯 ASCII 源码对这个问题彻底免疫。

---

## 与 CI 的关系

```text
preci.ps1（本地，快）        →  目的：把失败提前暴露
      ↓ 同一个判据
CI jobs（远端，权威）        →  目的：不可绕过的强制点
      ↓
gate（唯一必需检查）         →  汇总上游结论，if: always()
```

**预检不替代 CI。** 客户端钩子可以 `--no-verify` 绕过；真正的强制点在 CI 与规则集。
预检的价值是**把发现失败的时间从「推上去之后」提前到「push 之前」**。

---

## 单一事实来源

[`gates.yml`](gates.yml) 登记每个检查、它对应的 CI 上下文（必须与 job 的 `name:`
**逐字节相同**，含全角括号）、以及它属于哪个阶段。

**维护规则：每次在 CI 新增阻断项，必须同步登记到 `gates.yml`**，
否则本地预检会漏掉它 —— 那正是「没预检」的等价物。

`gates.yml` 还列了 `never_require`：**明确禁止设为必需检查的上下文**及原因
（例如 `同步标签` 只在 push 与 dispatch 触发，PR 上永不出现 → 会永久 Pending）。

---

## 已知缺口（不谎称已覆盖）

1. **actionlint / zizmor 未纳入本地**：二进制下载在本机被网络限制挡下，
   只能放进 CI。因此 `${{ }}` 类型检查、`uses:` 可解析性、表达式注入等
   **深层** workflow 校验在本地仍是空白。
2. **YAML 结构校验是软依赖**（需 Python + PyYAML）。装法：
   `pip install --target tools/preci/.pylibs pyyaml`（脚本会自动把它加入 `PYTHONPATH`）。
3. **凭据扫描**只覆盖常见前缀与私钥头；base64 编码后嵌入的凭据扫不到。
4. **无游戏/中继侧测试**，所以「Regression Verified」目前无内容可跑。

---

## 维护这个工具时

- 改完**务必**跑 `test_preci_negative.py（11 个用例）那类负例矩阵 ——
  一个永远全绿的门禁比没有门禁更糟，因为它提供虚假信心。
- 负例矩阵通过 `-Repo <路径>` 调用**同一个** `preci.ps1`，不要另写一份检查逻辑，
  否则测的是副本而不是产品。
- 若新增检查，同时更新 `gates.yml`、`messages.json`（中文文案）与 `README.md` 的覆盖表。
