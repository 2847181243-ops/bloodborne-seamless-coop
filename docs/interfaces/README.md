# docs/interfaces/ —— 接口契约

**所有者：AI-00（Integration Lead）**
**裁决权：接口冲突由 AI-00 裁决（开发优先）**

接口契约是**唯一权威版本**。任何一方要改接口，必须先改本目录，再由 AI-00 建 Issue 通知所有受影响方。

---

## 1. 契约矩阵

| 契约文件 | 来源 → 目标 | 内容 |
|---|---|---|
| [`a1_to_a2_events.md`](a1_to_a2_events.md) | AI-A1 → AI-A2 | 游戏事件（死亡 / 重生 / Lantern / 世界重置 / 敌人 / Boss / Party Wipe） |
| [`a2_to_a3_a4_state.md`](a2_to_a3_a4_state.md) | AI-A2 → AI-A3 / AI-A4 | Session 与玩家状态、玩家数、Party / Boss 状态、世界状态同步接口 |
| [`c1_to_b1_b4_e2ee.md`](c1_to_b1_b4_e2ee.md) | AI-C1 → AI-B1 / AI-B4 | E2EE 通道、密钥交换、密钥轮换 |
| [`c2_to_b_security_constraints.md`](c2_to_b_security_constraints.md) | AI-C2 → AI-B1/B2/B3/B4 | 安全约束清单、威胁模型、隐私要求、中继安全协议、审计要求 |
| [`b3_to_b4_reputation.md`](b3_to_b4_reputation.md) | AI-B3 → AI-B4 | 中继信誉查询与上报、中继节点筛选 |
| [`c3_to_all_audit.md`](c3_to_all_audit.md) | AI-C3 → 所有开发 Agent | 审计请求 / 审计结果（PASS / PASS_WITH_WARNING / FAIL） |

---

## 2. 每个接口必须定义

```text
事件名 / 方法名
触发条件          —— 什么情况下触发，是否可重入
数据结构          —— 字段、类型、单位、默认值、取值范围
Hook 地址         —— 若来自逆向
Signature         —— 版本绑定
版本限制          —— 支持的游戏版本 / Mod 版本
证据等级          —— Confirmed / Probable / Hypothesis / Unknown
失败语义          —— Hook 失败、版本不匹配、超时时的行为
线程模型          —— 在哪个线程回调，是否允许阻塞
```

**不允许静默失败。** 每个接口必须定义失败时的可观测行为（日志前缀 + 错误码）。

---

## 3. 契约变更流程

```text
1. 提出方在本目录提交契约修改（PR 到自己的分支无效，接口文件由 AI-00 维护）
2. 建 Issue：影响范围 / 涉及 Agent / 迁移方案 / 验收标准
3. AI-00 裁决 → 通知所有受影响 Agent
4. 受影响 Agent 在自己的分支同步适配
5. 集成测试通过后合并
```

---

## 4. 已定义的接口骨架

### 4.1 AI-A1 → AI-A2

```text
PlayerDeathEvent / PlayerRespawnEvent
LanternRestEvent / WorldResetEvent
EnemyDeathEvent / BossStartEvent / BossDeathEvent
PartyWipeEvent
```

### 4.2 AI-A2 → AI-A3 / AI-A4

```text
SessionState / PlayerState
CurrentPlayerCount
PartyState / BossState
世界状态同步接口
```

### 4.3 AI-C1 → AI-B1 / AI-B4

```text
E2EEChannel
├── encrypt(plaintext) → ciphertext
├── decrypt(ciphertext) → plaintext
└── verifyIntegrity(ciphertext, hmac) → bool

KeyExchange
├── generateKeyPair() → (publicKey, privateKey)
├── computeSharedSecret(theirPublicKey) → sharedSecret
└── deriveSessionKey(sharedSecret) → sessionKey

KeyRotation
└── rotateSessionKey() → newSessionKey
```

### 4.4 AI-B3 → AI-B4

```text
RelayReputation
├── getReputation(nodeId) → score
├── reportSuccess(nodeId) / reportFailure(nodeId)
└── isEligible(nodeId) → bool
+ 中继节点筛选接口
```

### 4.5 AI-C3 → 所有开发 Agent

```text
审计请求 / 审计结果
├── PASS
├── PASS_WITH_WARNING
└── FAIL（一票否决）
+ 审计报告
```

---

## 5. 状态机契约（跨 A2 / A3 / A4）

```text
SessionState：+ InvasionActive / PvPResolving
PlayerState ：+ PvPDead / PvPWaiting / PvPSpectating
PvPResolution：Ongoing / CoopWin / InvaderWin / Aborted / DisconnectResolved
```

**事件优先级（任务书 §28，必须显式定义）**

```text
两名玩家同时攻击敌人 / 同时拾取物品
玩家死亡与 Boss 状态更新同时
Lantern Rest 与玩家死亡同时
Disconnect 与 Party Wipe 同时
Boss Death 与 Player Death 同时
Invasion 加入与 Boss Start 同时
```

上表每一条都必须在契约文件中给出**确定的先后顺序与冲突消解规则**，不允许“视情况而定”。
