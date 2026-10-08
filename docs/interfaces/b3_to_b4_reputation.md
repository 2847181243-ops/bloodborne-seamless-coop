# 接口契约：AI-B3 → AI-B4（中继信誉）

**状态：DRAFT**
**维护：AI-00｜安全裁决：AI-C2**

---

## 1. RelayReputation

```text
getReputation(nodeId) → score
reportSuccess(nodeId)
reportFailure(nodeId)
isEligible(nodeId) → bool
```

| 方法 | 语义 | 失败语义 |
|---|---|---|
| `getReputation` | 返回信誉分；未知节点返回**最低分** | 查询失败 → 按最低分处理，不得默认信任 |
| `reportSuccess` | 成功转发后上报 | 上报失败 → 记录日志，不影响当前会话 |
| `reportFailure` | 失败 / 超时 / 协议违规后上报 | 同上 |
| `isEligible` | 是否允许作为中继 | 判定失败 → **拒绝该节点**（fail-closed） |

---

## 2. 中继节点筛选接口

```text
selectRelay(candidates) → orderedRelayList
```

- 按信誉分排序，优先高信誉节点；
- 单一实体**不得**控制大量中继（任务书 §45.5）；
- 全部候选不合格 → 返回空列表并明确报错，**不得降级到无信誉校验的节点**。

---

## 3. 隐私约束（硬性）

- `nodeId` 必须是**临时标识**，**不得**是 IP、硬件指纹或任何持久 ID；
- 信誉数据**不得**与玩家身份关联；
- 信誉数据的传输必须**加密并签名**；
- 信誉记录**不得**包含地理位置或设备信息。

---

## 4. 与安全约束的关系

本契约受 `c2_to_b_security_constraints.md` 的 C-003 / C-006 约束：

- C-003：无持久标识；
- C-006：抗 Sybil（新节点低信誉、中继上限、异常检测）。

---

## 5. 待确认项

- [ ] 信誉分的取值范围与初始值
- [ ] 信誉衰减策略与时间成本参数
- [ ] 异常检测阈值（多少新节点同时加入触发警报）
- [ ] 信誉数据的加密签名方案（与 AI-C1 对接）
