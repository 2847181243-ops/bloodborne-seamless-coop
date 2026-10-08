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
