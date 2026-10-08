# 接口契约：AI-A2 → AI-A3 / AI-A4（状态与同步）

**状态：DRAFT**
**维护：AI-00**

---

## 1. 接口清单

```text
SessionState
PlayerState
CurrentPlayerCount
PartyState
BossState
世界状态同步接口
```

---

## 2. SessionState

| 字段 | 类型 | 说明 |
|---|---|---|
| `session_id` | — | 会话标识（**非持久、不可用于追踪玩家**） |
| `state` | 枚举 | `Idle` / `Active` / `InvasionActive` / `PvPResolving` / `Suspended` / `Terminated` |
| `host_present` | bool | Host 是否在线 |
| `player_count` | int | 当前会话玩家数（1–4） |
| `reconnect_token` | — | 一次性重连令牌 |

**状态机约束**

- Host 断线：保留 Session **30–60 秒**，超时则终止（§29.4）；
- Host 退出：遣返所有客机（§7.3）；
- Host 死亡：**与客机一致**，从 Lantern 重生，Session 继续（§7.3）；
- Party Wipe：**不销毁 Session**（§16）；
- Boss Death：**不自动销毁 Session**（§40）。

---

## 3. PlayerState

| 字段 | 类型 | 说明 |
|---|---|---|
| `player_ref` | — | 会话内引用（**非平台 ID、非硬件指纹**） |
| `state` | 枚举 | `Alive` / `Dead` / `Spectating` / `PvPDead` / `PvPWaiting` / `PvPSpectating` / `Disconnected` |
| `hp` / `max_hp` | — | 同步字段 |
| `max_hp_penalty_layers` | int | 死亡惩罚层数（0–5，最低保留 30%） |
| `world_state_override` | — | 客机临时覆盖的主机世界状态（**仅内存**） |

**约束**：`max_hp_penalty_layers` **只影响该玩家自身**，**不得**影响敌人 / Boss 的 HP。

---

## 4. PartyState / BossState

| 结构 | 字段 | 说明 |
|---|---|---|
| `PartyState` | `team`（`CoopTeam` / `InvaderTeam`）、`alive_count`、`all_dead` | PvP 结算依据 |
| `BossState` | `boss_id`、`hp`、`phase`、`aggro_target`、`active`、`dead` | 同步重点 |

**Authority（任务书 §27）**

```text
Host Authority：Enemy / Boss / World / 重要 Gameplay State / 物品发放 / PvP 判定
Client         ：Local Player Input / Presentation / Interpolation
```

---

## 5. 世界状态覆盖接口（§7）

```text
apply_host_world_state(session)   // 加入时：临时覆盖，仅内存，不写存档
restore_personal_progress()       // 离开/中断时：丢弃覆盖，从个人存档恢复
clear_cached_host_state()         // 玩家可手动清除本地缓存的 Host 世界状态
```

**不变量**

- 客机角色数据（等级/属性/装备/库存/回响/符文）**永不被 Host 覆盖**；
- 覆盖**只作用于内存**，**不写入 Client 存档**；
- 每次连接重新覆盖，**不依赖缓存**；
- 异常中断 → 超时后自动清理临时世界状态，安全返回个人进度。

---

## 6. 待确认项

- [ ] 世界状态的内存表示与可覆盖字段（AI-A1）
- [ ] 独立存档格式（`.bloodco`）结构（AI-A2）
- [ ] 重连令牌的生成与校验方式
