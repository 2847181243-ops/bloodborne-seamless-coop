# 接口契约：AI-A1 → AI-A2（游戏事件）

**状态：DRAFT（等 AI-A1 逆向结论填充）**
**维护：AI-00**

> 每个事件必须填满下表全部字段，**不允许留空、不允许静默失败**。
> 未填完的事件视为 `Unknown`，AI-A2 不得据此实现。

---

## 1. 事件清单

```text
PlayerDeathEvent
PlayerRespawnEvent
LanternRestEvent
WorldResetEvent
EnemyDeathEvent
BossStartEvent
BossDeathEvent
PartyWipeEvent
```

---

## 2. 事件定义模板

### `<EventName>`

| 字段 | 值 |
|---|---|
| 触发条件 | 什么情况下触发；是否可重入；是否每帧 |
| 数据结构 | 字段 / 类型 / 单位 / 默认值 / 取值范围 |
| Hook 地址 | 基址 + 偏移（注明模块） |
| Signature | 版本绑定的字节序列 |
| 版本限制 | 游戏 1.09（CUSA03173）/ bbport Windows 原生版 / Mod 版本 |
| 线程模型 | 在哪个线程回调；是否允许阻塞；是否允许分配内存 |
| 失败语义 | Hook 失败 / Signature 不匹配 / 版本不匹配时的可观测行为与错误码 |
| 日志前缀 | 对应 `CONTRIBUTING.md` §12 的前缀 |
| 证据等级 | Confirmed / Probable / Hypothesis / Unknown |
| 证据 | 反汇编 / 交叉引用 / 运行时日志 / 行为实验 |

---

## 3. 语义约束（先定死，再实现）

- **PlayerDeathEvent ≠ WorldResetEvent**：单人死亡**不得**触发世界重置。
- **PlayerRespawnEvent** 的重生点必须是最近点亮的 Lantern，快速复活不得改变重生点。
- **PartyWipeEvent** 仅在**全员死亡**时触发；至少一人存活时不得触发。
- **BossStartEvent** 期间：禁止新 Invader（任务书 §22.1）、禁止区域切换（§30.2）。
- **WorldResetEvent** **不得清除**玩家掉落的 Blood Echoes（§25.3）。
- **LanternRestEvent** 是清除 Max HP Penalty 的唯一常规入口（§17）。

---

## 4. 事件优先级（任务书 §28）

必须在此给出确定顺序，禁止“视情况而定”：

| 并发场景 | 优先级 / 顺序 | 理由 |
|---|---|---|
| 玩家死亡 与 Boss 状态更新 同时 | _待定_ | |
| Lantern Rest 与 玩家死亡 同时 | _待定_ | |
| Disconnect 与 Party Wipe 同时 | _待定_ | |
| Boss Death 与 Player Death 同时 | _待定_ | |
| Invasion 加入 与 Boss Start 同时 | _待定_ | |

---

## 5. 待确认项

- [ ] bbport runtime 是否公开 Hook 接口（AI-A0 / AI-A1）
- [ ] 游戏函数地址是否与 PS4 原版一致
- [ ] eboot 转换为 x86-64 镜像后地址是否稳定
