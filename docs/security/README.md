# docs/security/ —— 安全架构与威胁模型

**所有者：AI-C2（Security Architect）**
**裁决权：性能 vs 安全冲突 → AI-C2，安全优先**

> **安全设计必须在写第一行网络代码之前完成。**
> 本目录的产出是 Phase 0 的交付物，也是所有网络实现的强制约束来源。

---

## 1. 必填文件清单

```text
docs/security/
├── threat_model.md          威胁模型（被动 / 主动 / 身份 / 中继 / 客户端攻击）
├── security_architecture.md 安全架构（E2EE / 身份 / 完整性 / 中继 / 隐私 / 抗攻击）
├── key_exchange.md          密钥交换流程（X25519 → HKDF → 会话密钥 → 轮换）
├── relay_security.md        中继安全协议（能看什么 / 不能看什么 / 必须做什么）
├── anti_sybil.md            抗 Sybil 机制（信誉 / 可选 PoW / 时间成本 / 中继上限）
├── anti_ddos.md             抗 DDoS 机制（速率限制 / 中继隐藏）
├── privacy.md               隐私保护方案与红线
└── constraints.md           安全约束清单（发给 AI-B1/B2/B3/B4 的强制要求）
```

---

## 2. 约束清单的写法

`constraints.md` 是**面向实现的强制条款**，每条必须可验证：

```markdown
### C-001 消息完整性

- 要求：每条消息必须附带 HMAC 与单调递增序列号。
- 理由：防止篡改与重放。
- 适用范围：AI-B1（传输层）、AI-C1（密码学实现）。
- 验证方式：AI-C4 重放测试 + AI-C3 代码审计。
- 违反后果：FAIL（一票否决）。
```

**禁止**写成“应当尽量”“建议考虑”这类不可验证的表述。

---

## 3. 隐私红线（不可协商）

```text
禁止采集：IP / 平台账号 ID / 硬件指纹 / Mod 自发生成持久 ID
         设备信息 / 系统信息 / 地理位置 / 任何可追踪标识
禁止行为：扫描进程 / 读内存 / 监控系统 / 上传行为数据 / 中心化封禁
```

中继节点约束：

```text
能看到：加密后的数据包 / 临时源节点标识 / 临时目标节点标识
看不到：游戏数据内容 / 玩家真实 IP / 玩家身份 / 任何元数据
不能  ：解密 / 篡改（HMAC 失败）/ 重放（序列号失败）/ 追踪玩家
必须  ：仅转发加密数据 / 不存储任何数据 / 不记录任何日志 / 可随时退出
```

---

## 4. 与其他 Agent 的接口

```text
AI-C2 → AI-B1 / B2 / B3 / B4
├── 安全约束清单（constraints.md）
├── 威胁模型（threat_model.md）
├── 隐私保护要求（privacy.md）
├── 中继安全协议（relay_security.md）
└── 审计要求
```

详细契约见 [`../interfaces/c2_to_b_security_constraints.md`](../interfaces/c2_to_b_security_constraints.md)。

---

## 5. 交付判定

- [ ] 威胁模型完成，且覆盖任务书 §45.1 全部五类攻击
- [ ] 安全架构完成，且每条约束可验证
- [ ] 隐私保护方案完成，且与 SECURITY.md 一致
- [ ] 中继安全协议完成，AI-B4 可据此实现
- [ ] 抗 Sybil / 抗 DDoS 方案完成
- [ ] 约束清单已交付 AI-B1/B2/B3/B4 并获得确认
