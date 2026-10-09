# docs/ —— 文档目录规约

> 本目录是任务书 §44.2 / §50 / §51 的工程化落地。
> **文档缺失 = 工作未完成**：CI 会校验目录结构，PR 会校验结论是否标注证据等级。

---

## 1. 目录所有者

| 目录 | 所有者 | 内容 |
|---|---|---|
| `interfaces/` | **AI-00** | 接口契约（唯一权威版本） |
| `re/` | **AI-A1** | 逆向文档、地址、结构体、findings |
| `security/` | **AI-C2** | 威胁模型、安全架构、隐私方案、中继安全协议 |
| `audit/` | **AI-C3** | 审计报告（PASS / PASS_WITH_WARNING / FAIL） |
| `pentest/` | **AI-C4** | 渗透测试报告与修复验证 |
| `integration/` | **AI-00** | 集成测试报告 |
| `conflict_log/` | **AI-00** | 冲突记录与裁决 |

跨目录写入 = 跨所有权修改 → 先建 Issue。

---

## 2. 全局强制要求

### 2.1 证据等级

每条逆向结论 / 原版行为断言必须标注：

```text
Confirmed   —— 源码 / 反汇编 / 交叉引用 / 运行时日志 / 行为实验确认
Probable    —— 较强证据，未完全确认
Hypothesis  —— 仅推测
Unknown     —— 暂无足够证据
```

**禁止把猜测直接当成事实写入核心逻辑。** 未标注等级的结论在 review 中一律按 `Unknown` 处理。

### 2.2 版本绑定

任何地址 / Signature / Hook 记录必须绑定：

```text
游戏版本（1.09 / CUSA03173）
平台（bbport Windows 原生版）
Mod 版本
Signature
```

版本不匹配时**禁止**复用旧地址。

### 2.3 隐私

文档中**不得**出现任何玩家隐私数据（IP / 账号 ID / 硬件指纹 / 地理位置）。
日志样例必须脱敏。

---

## 3. 文件命名

- 全小写 + 下划线：`player_death.md`、`blood_echoes.md`
- 接口契约：`<来源>_to_<目标>.md`，例如 `a1_to_a2_events.md`、`c1_to_b1_e2ee.md`
- 审计报告：`audit_<模块>_<YYYYMMDD>.md`
- 渗透测试：`pentest_<攻击类型>_<YYYYMMDD>.md`
- 冲突记录：`conflict_<YYYYMMDD>_<序号>.md`

---

## 4. 各目录必填内容

- [`interfaces/README.md`](interfaces/README.md)
- [`re/README.md`](re/README.md)
- [`security/README.md`](security/README.md)
- [`audit/README.md`](audit/README.md)
- [`pentest/README.md`](pentest/README.md)
- [`integration/README.md`](integration/README.md)
- [`conflict_log/README.md`](conflict_log/README.md)

---

## 5. 知识该写哪里（分层）

> **这一节解决的问题**：本项目由多个 Agent 长时间连续作业。
> 如果经验只留在会话里，它就会随会话消散 —— 下一个 Agent（或下一个会话的
> 同一个 Agent）会从零开始，重复踩同一个坑。
>
> 本项目实测：同一个 PowerShell 编码坑被反复踩过多次，每次都重新发现一遍。
>
> 借自 `build_in_harmonyos` 的 L1/L2/L3 分层，见
> [`standards/参考-build_in_harmonyos-可复用经验.md`](standards/参考-build_in_harmonyos-可复用经验.md)。

### 判断标准

遇到问题时只问一句：

> **"下一个完全不同的模块也会遇到吗？"**

```text
遇到问题
  ├─ 会  → 平台级 / 类别级知识 → 本文件「第 5 节」或 docs/standards/
  └─ 不会 → 本模块本版本的知识 → 该模块的归档目录或 docs/<对应目录>/
```

### 三级对照

| 级别 | 内容特征 | 写到哪 | 例 |
|---|---|---|---|
| **L1 平台真理** | 任何模块都可能遇到的系统性知识 | `docs/standards/` | MSVC 按 ANSI 代码页解码无 BOM 文件；`git archive` 会应用 export 过滤器 |
| **L2 类别模式** | 同类问题复现的模式 | `docs/standards/` 或 `docs/interfaces/` | 门禁检查必须排除注释行，否则会「自指」 |
| **L3 本模块笔记** | 只对本模块本版本有效 | `docs/re/`、`docs/security/` 等对应目录 | 某个具体地址、某个具体补丁 |

### 三条硬规则

1. **边做边记，禁止批量补记。**
   探索阶段每解决一个问题就落一次记录。事后回忆会漏掉当时的真实原因。

2. **索引必须同步。**
   新增 `docs/` 下的目录或文件后，回到本文件补上链接。
   索引落后等于没有 —— 没人会去翻一个不知道存在的文件。

3. **不确定的必须标注不确定。**
   证据等级 `Confirmed / Probable / Hypothesis / Unknown`（见第 2.1 节）
   是**强制**的。标成 `Unknown` 不丢人；把猜测写成事实才是。

---

## 6. 知识写在哪一层：反例

以下三种写法都是错的，都会让下一个人重新踩坑：

| 错误写法 | 为什么错 | 正确做法 |
|---|---|---|
| 只写在 PR 描述里 | PR 会被合并、被遗忘，检索不到 | 提炼进 `docs/standards/`，PR 里只放链接 |
| 只写在会话回复里 | 会话结束即消失 | 落到文件 |
| 写在 `docs/security/` 里但内容是通用的 | 放错层，别的 Agent 不会去看 | 通用的进 `docs/standards/` |

**判断"通用还是专用"**：把这条知识讲给一个正在做完全不同模块的 Agent 听，
他会觉得"这跟我有关"（通用）还是"这是你那边的事"（专用）。

