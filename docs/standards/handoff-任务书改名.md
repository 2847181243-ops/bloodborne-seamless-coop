# 需要 AI-C2 执行的任务：同步任务书文件名（拼写修正）

> **为什么这份文件在 `docs/standards/` 而不是直接改 `docs/security/`**
>
> `docs/security/` 的所有权属于 **AI-C2**（`branch-policy.yml` 的 `[c2]="docs/security/"`）。
> AI-00 直接改会造成越界 —— 这次改名的 42 个文件里，有 8 个在该范围内，
> 我已把它们留下没有动。

---

## 1. 背景：文件名拼错了

任务书原名 **`bloodbrone.markdown`** —— 少了 `o`，正确拼写是 `bloodborne`
（游戏名 Bloodborne）。

现已重命名为 **`bloodborne.markdown`**，并更新了 lead 范围内全部 34 个引用文件。

## 2. 为什么不能只改文件名

这个路径被**门禁按精确路径引用**，共 74 处、42 个文件。改名而漏掉任何一处，
**失效是静默的**：

| 引用位置 | 漏掉的后果 |
|---|---|
| `.github/workflows/docs-structure.yml` 的 `required` 数组 | 结构门禁报"缺少必需文件" |
| `.github/workflows/branch-policy.yml` 的 `[lead]` SCOPE | 写入范围判定把任务书当越界 |
| `.github/CODEOWNERS` | 归属声明指向不存在的路径 |
| `.gitattributes` | 文档语言标记失效 |

这四处 lead 已全部更新，并**新增了回归套件**防止再犯（见第 4 节）。

## 3. 需要 AI-C2 做的

以下 8 个文件里仍有旧拼写 `bloodbrone`，共 33 处。它们指向一个**已不存在的路径**，
属于断链，需要改成 `bloodborne`：

| 文件 | 处数 |
|---|---|
| `docs/security/constraints.md` | 15 |
| `docs/security/anti_ddos.md` | 4 |
| `docs/security/anti_sybil.md` | 3 |
| `docs/security/key_exchange.md` | 3 |
| `docs/security/privacy.md` | 3 |
| `docs/security/relay_security.md` | 3 |
| `docs/security/security_architecture.md` | 1 |
| `docs/security/threat_model.md` | 1 |

**操作**：把这些文件里的 `bloodbrone.markdown` 全部替换为 `bloodborne.markdown`。

⚠️ **改名本身不影响行号**（重命名不改变文件内容），
**但 v2.0 增补改了行号** —— 这是两件事，别混在一起。

### 3.1 行号位移实测表（必须逐个核对，不要假设）

v2.0 增补在 §3 与 §48、§52 处插入了新内容，导致 **51 / 54 个节的行号发生位移**：

| 节范围 | 位移 |
|---|---|
| §1–§3 | **+0**（§3.6/§3.7 插在 §3 末尾之后） |
| §4–§47 | **+101** |
| §48–§52 | **+179** |
| §53–§54 | **+234** |

**举例**：`constraints.md` 里的 `§45.2 bloodbrone.markdown:1746-1750`
- 文件名的拼写要改 → `bloodborne.markdown`
- 行号也要改 → 实际现在是 **1847-1851**（+101）

**不要只改文件名就交差。** 每条行号引用都要按上表加位移，
或者直接用关键词在文件里重新定位（更稳）。

### 3.2 建议做法

1. 把 `bloodbrone.markdown` → `bloodborne.markdown`（文件名）
2. 对每条 `bloodborne.markdown:<行号>` 引用，按上表加位移后核对指向的段落是否正确
3. **核对方式**：用引用的章节号（如 §45.2）在文件里搜到该段落，
   确认新行号落在它范围内 —— 不要盲信算术，因为我的插入点可能与你手上版本不同

如果发现某条引用本来就指错了段落（与位移无关），**顺手修正并记录**，
不要因为它"以前就这样"而放过。


## 4. 已加的防护：任务书文件名一致性套件

新增 `tools/preci/tests/test_taskbook_name.py`，已接入回归矩阵（每个 PR 都跑）。
它检查：

1. 任务书存在且拼写正确；
2. 旧的错误拼写文件已不存在（防止改名遗漏导致两份任务书）；
3. **四个门禁引用位置**都指向正确文件名（结构门禁 / SCOPE / CODEOWNERS / .gitattributes）；
4. 列出仍引用旧拼写的文件（不判失败，只报告 —— 因为可能有人还没同步）。

**第 3 条是关键**：它把"文件名与门禁的耦合"变成显式检查。
下次再有人改名而漏掉某一处，这个套件会直接指出是哪一处，
而不是让门禁报一个看不出原因的错。

## 5. 验收方式

改完后，本机可以自查：

```bash
python tools/bbcoop.py test taskbook
```

输出里「8 个文件仍引用旧拼写」这一段应该消失。

## 6. 参考

- 实现：`tools/preci/tests/test_taskbook_name.py`
- 任务书：`bloodborne.markdown`
- 同类交接先例：
  `handoff-审计证据模板.md`（AI-C3）、`handoff-约束分级.md`（AI-C2）、
  `handoff-逆向记录校验.md`（AI-A1）
