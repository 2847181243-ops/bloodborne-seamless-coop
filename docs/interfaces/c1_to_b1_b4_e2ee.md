# 接口契约：AI-C1 → AI-B1 / AI-B4（E2EE 与密钥交换）

**状态：DRAFT（安全约束见 `c2_to_b_security_constraints.md`）**
**维护：AI-00｜安全裁决：AI-C3**

---

## 1. E2EEChannel

```text
encrypt(plaintext) → ciphertext
decrypt(ciphertext) → plaintext
verifyIntegrity(ciphertext, hmac) → bool
```

| 项 | 要求 |
|---|---|
| 算法 | ChaCha20-Poly1305（AEAD） |
| 每条消息 | 必须附带 HMAC 与**单调递增序列号** |
| 防重放 | 序列号 + 时间戳，双机制 |
| 失败语义 | 校验失败 → **丢弃并记录 `[SECURITY]` 日志**，不得静默通过 |
| 隐私 | **不得**在密文外封装任何玩家标识、IP 或持久 ID |

---

## 2. KeyExchange

```text
generateKeyPair() → (publicKey, privateKey)
computeSharedSecret(theirPublicKey) → sharedSecret
deriveSessionKey(sharedSecret) → sessionKey
```

| 步骤 | 要求 |
|---|---|
| 1 | 双方生成**临时**密钥对（X25519 / ECDH） |
| 2 | 通过信令交换公钥 |
| 3 | 计算共享密钥 |
| 4 | 通过 HKDF 派生会话密钥 |
| 5 | 公钥指纹验证（可选，防 MITM） |
| 6 | 建立 E2EE 通道 |
| 7 | 定期轮换会话密钥（前向保密） |

**约束**

- 每次会话使用**独立密钥**（前向保密）；
- **禁止**降级路径：不得回退到明文或弱算法；
- 私钥**只存内存**，不得落盘、不得写日志；
- 密钥材料在日志中必须完全不可见。

---

## 3. KeyRotation

```text
rotateSessionKey() → newSessionKey
```

- 轮换期间必须保证不丢包、不乱序；
- 旧密钥在使用后立即清除；
- 轮换失败 → 明确报错，**不允许静默降级**。

---

## 4. 中继侧约束（AI-B4）

中继**只能**看到：加密后的数据包、**临时**源/目标节点标识。

中继**不能**：解密、篡改（HMAC 失败）、重放（序列号失败）、追踪玩家。

中继**必须**：仅转发加密数据、不存储任何数据、不记录任何日志、可随时退出。

---

## 5. 审计要求

AI-C1 的密码学代码：
- **不允许**被其他 Agent 修改（任务书 §48）；
- 合并前必须通过 **AI-C3 密码学审计**；
- 上线前必须通过 **AI-C4 渗透测试**（MITM / 重放 / 降级）。

---

## 6. 待确认项

- [ ] 公钥指纹验证是否作为默认开启项
- [ ] 会话密钥轮换周期
- [ ] 时间戳防重放的允许窗口（需与 500ms 延迟阈值兼容）
