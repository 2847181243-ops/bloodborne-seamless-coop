以下为完整 Markdown 文档内容，可直接全选复制，保存为 `.md` 文件。

---

# Bloodborne Seamless Co-op Mod AI 开发任务书

**版本：v3.0**


## 1. 项目定位

你是一名**资深游戏逆向工程师、Native C++ 工程师、多人网络系统工程师和游戏系统工程师**，负责在合法拥有的 Bloodborne 游戏数据及目标 PC 运行环境/兼容运行时基础上，开发 Seamless Co-op Mod。

项目目标不是简单增加“联机功能”，而是将 Bloodborne 原有的多人流程改造成**持续存在的多人 Session**，尽量消除原有联机过程中的召唤、断线、区域切换等割裂感，同时尽可能保留原游戏的战斗风险、死亡经济、世界状态和 Boss 设计。

不得实现 DRM 绕过、破解、盗版分发或其他未授权访问机制。本任务只针对合法游戏数据、合法运行环境以及 Mod/兼容层开发。


## 2. 核心设计原则

### 2.1 Seamless ≠ 无风险多人游戏

> Seamless Co-op 的目标是消除联机流程上的割裂，而不是消除 Bloodborne 原有的死亡风险。

- 不因为多人模式而取消原版血之回响等死亡经济；
- 不因为玩家死亡而无条件刷新整个区域；
- 不因为快速复活而让死亡变得没有代价；
- 多人模式应保留足够高的死亡压力；
- 世界状态和玩家状态必须分离管理。

### 2.2 隐私优先

> Mod 不收集、不存储、不上传任何玩家隐私数据。
> 无服务器，无官方账号，无中心化身份验证。

禁止：IP 地址、平台账号 ID、硬件指纹、Mod 自发生成持久 ID、设备信息、系统信息、地理位置。

### 2.3 安全第一

> 安全不是网络层的一个模块，而是网络层的地基。
> 任何网络功能，如果无法通过安全审计，就不允许进入实现阶段。
> 安全设计必须在写第一行网络代码之前完成。

### 2.4 非目标（Out of Scope）

```text
├── 2v2 / 3v3 / FreeForAll
├── 房主携带客机进入决斗场
├── 中心化服务器
├── 官方账号系统
├── 隐私数据收集
├── 进程扫描 / 内存扫描
├── 自动封禁
├── DRM 绕过
├── Steam 客户端依赖
└── PS4 模拟器依赖
```


## 3. 目标平台

### 3.1 首要开发与测试平台

- **Windows 10/11**（bbport Windows 原生移植版）
- 游戏版本：**1.09**（CUSA03173）
- 存档路径：`GMsave\Bloodborne\bbport\user\savedata\1\CUSA01363\SPRJ0005`
- 支持 Apollo 解密后的 PS4 存档替换
- 需 **Vulkan 1.3** 支持的显卡

bbport 是 Supermedo 对 deadinside28 的 Linux 原生移植版的 Windows fork，包含启动器 `Bloodborne.exe`，支持 13 种语言，内置 XML 补丁管理器。它直接运行 PS4 可执行文件（CUSA03173，游戏版本 1.09），在 x86-64 上原生执行，不进行 CPU 模拟。启动前，eboot 会被转换为原生 x86-64 镜像，随后由一个小型运行时环境处理 Bloodborne 所需的 PS4 功能。

### 3.2 技术路线

- Mod 直接与 Windows 原生进程交互（**非模拟器**）
- Hook 目标：Windows 原生进程的内存布局与函数地址
- 不依赖 PS4 模拟器（ShadPS4）
- 不依赖 Steam 客户端
- 启动器：`Bloodborne.exe`（内置 XML 补丁管理器）

### 3.3 移植版技术路线对比

| 项目 | 类型 | 技术路线 | 对 Mod 开发的影响 |
|---|---|---|---|
| bbport | 原生移植 | 直接运行 PS4 可执行文件 + 自定义 runtime | Hook 目标为原生 x86-64 进程 |
| bbhost | 翻译层 | PS4 系统库调用转换为 PC 实现 | 独立实现，不含 ShadPS4 代码 |
| Paleblood | 源码重写 | 逐函数 C++ 重写 PS4 可执行文件 | 可读性高，但尚处早期 |
| ShadPS4 | 模拟器 | 全系统模拟 | 不在目标范围内 |

### 3.4 Mod 兼容性评估

**已确认：**

- bbport 提供 Mod 支持
- 支持社区补丁
- 支持自由视角
- 支持 XML 补丁管理
- 支持 AMD FSR 3.1 / FSR 4 / FSR 4.1.1

**需确认：**

- bbport 的 Mod 加载机制
- Hook 接口是否公开
- 与已有 Mod 的兼容性
- 是否支持运行时注入

### 3.5 性能基准

| 硬件配置 | 性能 |
|---|---|
| RX 6650 XT | 89-100 FPS |
| RX 7800 XT @ 1440p + FSR 4 Quality | 约 150 FPS |
| RX 7800 XT @ 4K + FSR 4 Balanced | 约 90 FPS |
| Core i5-4590 + GTX 1650 + FSR 3.1 | 约 90 FPS |

**Mod 运行时的性能预算：**

- 网络同步开销：< 5% CPU
- Hook 开销：< 1ms/frame
- 反作弊开销（仅 PvP）：< 1ms/frame
- 内存开销：< 100MB


## 4. AI 工作角色

1. Senior Game Reverse Engineer
2. Native C++ / Systems Engineer
3. Multiplayer Networking Engineer
4. Game Systems Engineer
5. Game Balance Engineer
6. Mod Architecture Engineer
7. 测试与验证负责人

逆向结论必须标记：

- **Confirmed**：已通过源码、反汇编、交叉引用、运行时日志或行为实验确认；
- **Probable**：存在较强证据，但尚未完全确认；
- **Hypothesis**：当前仅为推测；
- **Unknown**：暂无足够证据。

禁止把猜测直接当成事实写入核心逻辑。


## 5. 逆向工程要求

### 5.1 现状认知

开始修改代码前，必须先分析：

- bbport Windows 原生进程结构
- 内存布局（与 PS4 原版的差异）
- 关键模块地址范围
- bbport runtime 与游戏代码的交互方式
- 已有 Mod/Hook 的兼容性
- 启动器（`Bloodborne.exe`）的加载机制
- 项目结构、运行环境、当前多人实现、网络代码
- Player/World/Enemy/Boss/Save/Inventory/Death 状态
- 地图切换、Lantern 行为、原版多人机制

禁止一开始就大规模修改核心代码。

### 5.2 bbport runtime 逆向任务

必须确认：

- bbport runtime 的内存管理机制
- 游戏进程的基址和模块布局
- 游戏函数的地址分布（与 PS4 原版是否一致）
- bbport 的 Hook 点（是否已有公开的 Hook 接口）
- bbport runtime 与游戏代码的边界
- Mod 加载时机（游戏启动前/运行时）
- eboot 转换为 x86-64 镜像的流程

### 5.3 逆向资料

```text
docs/
├── functions.md
├── addresses.md
├── structures.md
├── player.md
├── enemy.md
├── boss.md
├── world.md
├── death.md
├── lantern.md
├── network.md
├── session.md
├── protocol.md
├── arena.md
├── loot.md
├── consumable.md
├── shop.md
├── npc.md
└── findings.md
```

每个关键函数至少记录：Function / Address / Purpose / Arguments / Return / Callers / Called Functions / Evidence / Confidence / Related Feature。

### 5.4 工具链参考

ModEngine2 是 soulsmods 社区维护的运行时注入库，支持魂系游戏（含 Bloodborne），采用 Detours 技术实现函数重定向，通过 `hook_set.cpp` 和 `patch.cpp` 实现动态修改。其扩展和插件可以注册补丁、代码钩子，并与其他扩展交互。Hook 系统允许 ModEngine2 在不修改游戏可执行文件的情况下，将自定义代码插入游戏的执行流。在 Windows API 层面，它通过 hook `CreateFileW` 来拦截文件系统访问。

**建议：**

- 评估是否复用或参考 ModEngine2 的 Hook 框架
- 需确认与 bbport 的兼容性
- 可参考其扩展/插件架构设计 Mod 模块系统

### 5.5 版本安全

```text
VersionChecker → AddressResolver → HookManager → Game Systems
```

要求：

- 版本不匹配时禁止使用旧地址
- Signature 不匹配时禁止强行 Hook
- **Mod 版本必须强校验，版本不一致拒绝连接**
- Hook 失败必须明确记录日志
- 禁止因为“可能是这个地址”而直接写入内存
- 不允许静默失败


## 6. Seamless Session 架构

```text
                 Matchmaking / Session
                         │
                         ▼
                 Session / Signaling
                         │
             ┌───────────┴───────────┐
             ▼                       ▼
          Host/Authority          Clients
             │                       │
             └──── Gameplay Sync ───┘
```

服务器负责：Session 创建、Matchmaking、加入/离开、身份验证、Signaling、Heartbeat、Session 状态。

Gameplay 数据优先采用 P2P/Host Authority。


## 7. Session 世界状态覆盖与个人进度隔离

### 7.1 核心原则

> 客机加入主机 Session 时，临时使用主机的世界状态；客机离开或连接中断时，恢复客机自己的个人世界进度。客机的角色数据始终保留自己的，不被主机覆盖。

```text
世界状态（World State）
├── Boss 击杀状态 / 区域探索状态
├── NPC 状态 / 任务进度
├── 机关 / 捷径开启状态
├── 商人解锁状态
├── 已拾取固定物品状态
└── 其他全局进度

角色数据（Character Data）
├── 等级 / 属性 / 装备 / 武器 / 宝石
├── 库存 / 消耗品
├── 血之回响 / 洞察 / 符文
└── 个人死亡惩罚状态
```

### 7.2 加入流程

```text
Client 加入 Host Session
        ↓
加载 Host 世界状态（临时覆盖 Client 内存中的世界状态）
        ↓
Client 角色数据保持自己的，不覆盖
        ↓
Client 出生在 Host 位置
        ↓
Client 在 Session 中获得的物品 / 经验 / 回响按规则进入个人存档
```

**加入条件：**

- 主机开放世界则随时可以加入
- Boss 战不能中途加入或退出
- 不可召唤区域显示“客机无法加入主机世界”
- 如果先被入侵则不可召唤
- 每次连接都重新覆盖，不依赖缓存
- 覆盖仅作用于内存，不写入 Client 存档

### 7.3 离开 / 中断流程

```text
Client 离开 Session
        ↓
丢弃临时 Host 世界状态
        ↓
从 Client 个人存档重新加载世界状态
        ↓
按当前进度重新刷新主机状态
        ↓
Client 保留所有物品、等级、魂
        ↓
Client 回到个人进度
```

**Host 死亡与退出：**

- Host 死亡与客机一致，从 Lantern 重生，Session 继续
- Host 退出则遣返所有客机回到单人状态，联机按照召唤类似
- Host 进入决斗场则按切断世界处理
- Host 断线保留 Session 30~60 秒，超时则 Session 终止

### 7.4 数据分类

| 数据类别 | 来源 | 同步 | 离开后保留 |
|---|---|---|---|
| Boss / 区域 / NPC / 商人 / 固定拾取 | Host | 是 | 否 |
| Client 等级 / 装备 / 库存 / 回响 | Client | 否 | 是 |
| Client 死亡惩罚 | Client | 否 | 是（按规则清除） |
| 随机掉落 | Client | 独立 | 是 |
| 固定拾取 / Boss 掉落 | 各自 | 每人一份 | 是 |

### 7.5 独立存档机制

```text
独立存档机制
├── Mod 使用独立存档格式（如 .bloodco）
├── 与原版 .sl2 完全隔离
├── 不访问官方服务器
├── 客机加入时加载 Host 世界状态到内存，不写入个人存档
├── 客机离开时从个人 Mod 存档恢复
└── 支持原版存档转换为 Mod 存档（可选）
```

**存档路径适配：**

- Windows 原生版：`GMsave\Bloodborne\bbport\user\savedata\1\CUSA01363\SPRJ0005`
- 原版 PS4 存档：CUSA03173
- 支持 Apollo 解密后的 PS4 存档导入
- Mod 存档独立于原版存档

### 7.6 防复制与存档安全

必须防止：Host 世界状态被写入 Client 存档；反复加入/离开复制唯一物品；离开时角色数据回滚异常；断线重连导致世界状态错乱；Session 崩溃损坏 Client 存档。


## 8. MVP 多人功能

**MVP-0（可运行的最小版本）：**

- 2P Session 建立
- Player 位置/动画/HP 同步
- 普通敌人 HP/死亡同步
- 单人死亡 → Lantern 重生（无惩罚）
- Host 退出 → 客机遣返

**MVP-1（第一阶段完整版）：**

- MVP-0 +
- Max HP Penalty
- Boss 同步
- Lantern Rest 清除惩罚
- Loot / Consumable 独立
- 网络异常处理（500ms 阈值、会话保留）


## 9. 多人死亡机制——核心要求

必须严格区分：

```text
Player Death / Player Respawn / Party Wipe
Lantern Rest / World Reset / Enemy Respawn / Boss Reset
```


## 10. 原版死亡经济必须保留

> 死亡相关经济优先保持原游戏设计。

包括 Blood Echoes 掉落、回收、敌人持有、原版死亡相关状态。

禁止未经验证改成“死亡 = 没有任何损失”。如果无法完整保留，必须记录原因、建 Issue、给最小改动方案、验证后再实施。


## 11. 非 Boss 战：允许直接复活

```text
Player A 死亡
      ↓
从最近点亮的 Lantern 重生
      ↓
快速跳过等待/加载，重生点仍为 Lantern
      ↓
该玩家获得高额 Max HP Penalty
      ↓
不触发 World Reset，不刷新已死亡普通敌人
      ↓
其他玩家继续当前战斗
```

必须：不得原地复活；不等待其他玩家；不结束 Session；不触发 World Reset；不刷新已死亡敌人；原版 Blood Echoes 机制继续有效。


## 12. 多人死亡的核心惩罚

### 12.1 DeathPenalty 结构

```text
DeathPenalty
├── penalty_pool（可配置的惩罚效果池）
│   ├── max_hp_loss（Max HP 降低）
│   ├── stamina_recovery_loss（耐力恢复降低）
│   ├── damage_loss（伤害降低）
│   └── resistance_loss（抗性降低）
├── stacking_rule（每次死亡随机选择一种，可叠加）
├── clear_condition（仅在 Lantern Rest 时清除）
├── display（玩家可见当前惩罚层数与效果）
├── apply_on_lantern_respawn
├── clear_on_lantern_rest
├── clear_on_world_reset
└── restore_on_party_wipe
```

### 12.2 初始参数

```text
单次死亡：该玩家 Max HP -20%
最大累计：最低保留 30%
连续死亡：可叠加，层数越高效果越强
```

示例：100% → 80% → 60% → 40% → 30%

**必须：** 在 Lantern 重生后施加；只作用于死亡玩家自身；在 Lantern Rest / 世界重置时消除；Party Wipe 不得默认销毁 Session。


## 13. 为什么需要高额惩罚

> 快速复活解决“等待时间”和“联机流程中断”，降低死亡玩家自身 Max HP 负责保留死亡风险。怪物/Boss 的 HP 不因玩家死亡次数自动增加。


## 14. 单人死亡不得刷新多人世界

```text
Player A 死亡 → 从 Lantern 重生 → Max HP ↓
→ 世界继续运行 → B/C 继续战斗
→ 已死亡敌人保持死亡 → 未死亡敌人保持状态
```

> 单个玩家死亡不能触发多人 World Reset。


## 15. 多名玩家死亡

只要至少一名玩家存活，就不能当成 Party Wipe。A/B 死亡时 C 可继续战斗，A/B 按机制复活并承受 Max HP Penalty。


## 16. Party Wipe

```text
Party Wipe → World/Boss Reset → Players Respawn
           → Session 保持 → 继续多人游戏
```

> Party Wipe 不应默认销毁多人 Session。


## 17. Lantern Rest 是世界级 Reset 点

```text
Player Death ≠ World Reset
Lantern Rest = 允许 World Reset
Lantern Rest = 清除 Max HP Penalty
```

Lantern Rest 时：清除 Max HP Penalty、恢复正常 Max HP、按原版规则处理世界状态和敌人刷新、保留 Boss/任务/NPC 特殊状态。


## 18. Boss 战死亡规则

### 18.1 基本规则

Boss 战期间：不因一个玩家死亡重置世界；Boss 战继续；存活玩家继续；所有人死亡才 Party Wipe；Party Wipe 后 Boss Reset；Session 不因失败解散。

### 18.2 Boss 战观战模式

```text
BossBattleDeathBehavior
├── 死亡玩家进入观战模式（幽灵状态，可切换视角）
├── 不立即从 Lantern 重生
├── Boss 战死亡暂不叠加腐化
├── Boss 被击败 → 观战玩家可复活
├── Party Wipe → Boss Reset，全员按规则重生
└── 若玩家在 Boss 战中主动在 Lantern 休息 → 移除出 Boss 房
```

Boss 的 HP / Phase / Aggro / Target / 状态机 / 战斗阶段必须作为多人同步重点。


## 19. Enemy / Boss 多人平衡

不得只做 `Enemy HP × Player Count`。

```text
BalanceManager
├── PlayerScaling / EnemyScaling / BossScaling
├── DeathPenalty / Respawn
├── Invasion / Reward / DifficultyPreset
```


## 20. 死亡惩罚与敌人 Scaling 完全独立

```text
玩家死亡惩罚 → 只降低死亡玩家自身 Max HP
多人 PvE Scaling → 根据当前玩家人数调整
```

**玩家死亡次数不得导致普通敌人或 Boss 的 HP 增加。**


## 21. 多人 PvE Scaling

### 21.1 初始测试值

| 玩家数 | 敌人 HP | 敌人伤害 | Boss HP | Boss 伤害 |
|---|---:|---:|---:|---:|
| 1 | 1.00x | 1.00x | 1.00x | 1.00x |
| 2 | 1.30x | 1.10x | 1.50x | 1.10x |
| 3 | 1.65x | 1.20x | 2.00x | 1.20x |
| 4 | 2.00x | 1.30x | 2.50x | 1.30x |

### 21.2 配置文件化

所有缩放倍率**通过配置文件暴露**，不硬编码：

```ini
enemy_health_scaling = 35
enemy_damage_scaling = 0
enemy_posture_scaling = 15
boss_health_scaling = 100
boss_damage_scaling = 0
boss_posture_scaling = 20
```

**缩放仅统计当前战斗区域内的玩家**，房间外玩家不计入。

### 21.3 高等级玩家下修

```text
DownscalingRule
├── 等效等级 = 等级 + 10 × 武器强化等级（近似）
├── 若客机等效等级 > Host等效等级 + (20 + Host等级 × 0.1) × 2，触发下修
├── 下修内容：属性点向Host等级靠拢，武器强化等级下修
├── 保留：武器种类、法术、道具
└── 不影响个人存档，离开Session后恢复
```


## 22. Invasion

### 22.1 基本规则

默认：2~4 名合作；最多 1 名 Invader（可配置 2）；Boss 战期间禁止新 Invader；PvP Scaling 与 PvE Scaling 分开；Invader 加入/离开不破坏合作 Session。

**入侵按照原版规则。**

### 22.2 配置开关

```ini
allow_invaders = 1
invasion_password_required = 0
invasion_max_count = 1
```

### 22.3 PvP 死亡与结算核心原则

```text
PvE 死亡：
Player Death → 从 Lantern 重生 → Max HP Penalty
             → 不触发 World Reset → Session 继续

PvP / Invasion 死亡：
Player Death → 不立即 Lantern 重生 → 不立即结算
             → 进入 PvPDead / Waiting
             → 等待某一队全部死亡 → 统一结算
```

> PvP 中单个玩家死亡不是结算点。只有合作队全灭或入侵队全灭才进入 PvP 结算。

### 22.4 队伍定义

```text
CoopTeam：Host + 所有 Co-op 玩家
InvaderTeam：当前 Invader（默认最多 1，可配置 2）

InvaderTeam.AllDead → CoopWin
CoopTeam.AllDead → InvaderWin
```

### 22.5 状态机

```text
SessionState 增加：InvasionActive / PvPResolving
PlayerState 增加：PvPDead / PvPWaiting / PvPSpectating
PvPResolution：Ongoing / CoopWin / InvaderWin / Aborted / DisconnectResolved
```

### 22.6 结算后处理

**CoopWin：** InvaderTeam 全灭 → 结算奖励 → Invader 离场 → 死亡 Co-op 玩家恢复/重生 → 不触发 World Reset → Session 继续。

**InvaderWin：** CoopTeam 全灭 → 结算奖励 → 按 Party Wipe 规则处理合作队 → Session 保持。

**断线：** Invader Disconnect → 按原版入侵中断规则 + 防重复奖励；Co-op Disconnect → 不计为 Death，必须建 Issue。

### 22.7 魂掉落

> Blood Echoes 掉落、回收、敌人持有等原版机制不变。未确认前不得自定义 PvP 魂掉落规则。

### 22.8 关系

```text
PvP 死亡 ≠ PvE 快速复活 ≠ World Reset
```

不因单个 PvP 死亡触发敌人刷新 / World Reset；Boss 战期间禁止新 Invader；PvP 结算完成后才恢复 PvE 死亡规则；PvE Max HP Penalty 不直接套用到 PvP。


## 23. 决斗场系统（官方原版设计）

### 23.0 原版机制（Confirmed）

**血之同胞决斗场：**

- 地点：不死人刑场，签订血之同胞契约后调查小矮人对面的三个石像进入
- 三个石像对应场地：左吊桥、中田字形高低地势、右脚手架平台
- 胜利获得**龟裂红色眼眸宝珠**
- 誓约升级：净胜 50/150/500 场 → 1/2/3 级

**青骑士决斗场：**

- 地点：青教堂，加入青骑士教后，在地下石像处使用**信义之证**进入
- 胜利获得**龟裂蓝色眼眸宝珠**
- 誓约升级同上

**原版限制：** 仅限同契约匹配；1.04 补丁后网战匹配无视等级差。

### 23.1 核心原则

> 决斗场按官方原版设计实现：独立进入，不携带客机，不打断 Seamless Session，不触发 World Reset。

### 23.2 进入方式

```text
血之同胞：不死人刑场三个石像
青骑士：青教堂地下石像（使用信义之证）
```

- 优先复用原版入口和触发函数
- 不允许房主通过 Session 菜单携带客机进入
- 不允许在 Seamless Session 中直接发起决斗场
- 玩家必须主动离开当前 Session，单独进入

### 23.3 与 Session 关系

```text
玩家在 Session 中 → 主动离开 Session
                  → 单独进入决斗场
                  → 决斗结束后重新加入原 Session
```

Host 进入决斗场则按切断世界处理。

### 23.4 模式

```text
ArenaMode：Duel（1v1，官方唯一支持）
2v2 / FFA 标记 Out of Scope
```

### 23.5 匹配

```text
ArenaMatchmaking
├── 同契约匹配
├── 1.04 补丁后无视等级差
├── 使用原版石像触发匹配
└── 不使用入侵的等级限制
```

### 23.6 战斗规则

原版决斗场**没有官方禁用物品规则**。社区“禁宝石 / 禁血瓶”属自律约定，Mod 不强制实施，可提供可选配置（默认关闭）：

```text
HostConfig
├── arena_community_rules_enabled = false
├── arena_ban_blood_gems = false
├── arena_ban_healing = false
├── arena_ban_bone_marrow_ash = false
└── arena_ban_lead_elixir = false
```

### 23.7 奖励（失败无损失）

**血之同胞：**

```text
胜利：龟裂红色眼眸宝珠 + 誓约净胜 +1
失败：无任何损失（不扣分、不掉落、不惩罚）
```

**青骑士：**

```text
胜利：龟裂蓝色眼眸宝珠 + 誓约胜场 +1
失败：无任何损失
```

**誓约升级：** 净胜 50/150/500 场 → 1/2/3 级。

> **注意：** 原版血之同胞失败扣 1 分净胜，本 Mod 改为**失败无任何损失**。这是对原版机制的**有意变更**，必须明确标记。不实现额外防刷保护机制，奖励仅按原版规则发放。

> **决斗场死亡不触发 Lantern 重生，不施加 Max HP Penalty，不掉落 Blood Echoes。**

### 23.8 与 Invasion / Boss 关系

决斗场与 Invasion 互斥；Boss 战期间禁止进入决斗场；结束后恢复 Invasion 可用。

### 23.9 断线

一方断线 → 另一方获胜；双方断线 → Aborted 无奖励；断线不计为死亡；不触发 World Reset；返回个人世界。

### 23.10 AI 职责

**AI-A1：** 确认原版决斗场函数/地址/匹配/奖励/誓约计分；确认血之同胞失败扣分实现并设计改为无损失的 Hook；确认是否适用入侵者 HP -30%。

**AI-A2：** 实现独立进入、离开/重加入 Session、与 Invasion 互斥、断线处理、奖励同步、确保房主无法携带客机进入、可选社区规则（默认关闭）、确保失败无损失逻辑正确生效。

**AI-A4：** 验证奖励/誓约计分正确；通过实战验证是否符合设计；不实现额外防刷保护机制。

### 23.11 必须建立的 Issue

- 原版决斗场函数地址未知
- 原版匹配逻辑未确认
- 是否适用入侵者 HP -30% 未确认
- 房主进入决斗场时 Session 如何处理未定
- 社区规则是否作为可选配置未定
- 血之同胞失败扣分改为失败无损失的具体 Hook 方案未定


## 24. 物品掉落与拾取安全

### 24.1 掉落分类

```text
随机掉落（Random Drop）：独立计算，互不影响
固定拾取点（Fixed Pickup）：光点 / 尸体 / 宝箱
Boss 掉落（Boss Reward）
唯一物品（Unique Item）：每周目一次或数量限制
```

### 24.2 随机掉落

- 独立计算，每个玩家只看/取自己的
- 互不影响、不共享、不冲突
- AI-A1 确认原版归属规则并保持不变

### 24.3 固定拾取点 / 宝箱

- 默认每个玩家各自拾取一次
- 为每个玩家维护独立拾取状态
- AI-A1 确认原版行为
- 原版不支持则建立 Issue

### 24.4 Boss 掉落

- 默认每人一份
- **唯一性物品**（Boss 魂、关键道具、唯一武器/符文）不能因多人重复给予同一玩家
- 若原版只能由 Host 获得则保持原版
- 若所有玩家都需要该物品推进进度，必须建 Issue

### 24.5 防重复与防刷

同一玩家不能重复获得同一唯一物品；断线重连、Session 重连不能重复领取；不能利用多人机制刷取唯一物品。

### 24.6 消耗品使用独立计算

> 每个玩家消耗品库存独立，使用后只影响使用者自身。

**捏魂类：** 只增加使用者自己的血之回响/洞察；不增加其他玩家；不改变原版给予数量。

**回复类与增益类：** 每个玩家使用自己的库存；只对使用者生效；不同步扣除他人；不共享效果。

**防止：** 消耗品复制、断线重连数量回滚、Session 重连效果重复触发、Host/Client 库存不同步。

### 24.7 商店与商人交互（个人化）

> 每个玩家独立与商人交互，购买/出售/货币扣除/物品获得只影响该玩家自身；商人库存与解锁状态基于共享世界进度。

- 使用自己的血之回响购买
- 只扣自己的货币，只获得自己的物品
- 商人解锁状态基于共享世界进度（以 Host 为准）
- 防止购买复制与重复发放

### 24.8 NPC 保护配置选项

```text
HostConfig
├── npc_protection_enabled
├── npc_protection_scope（friendly_npc_only / merchants_only / all_non_hostile）
└── npc_protection_sync（同步到所有客户端）
```

默认：`enabled = false`，`scope = friendly_npc_only`。

开启后：友好 NPC 免疫玩家伤害；玩家攻击不导致敌对；不影响敌人/Boss/入侵者/PvP 伤害；不影响原版事件触发；由 Host 设置并同步到所有客户端。

### 24.9 必须由 AI-A1 确认的原版行为

随机掉落归属规则 / 固定拾取点多人行为 / Boss 掉落多人分配 / 物品唯一性标志 / 消耗品使用行为 / 捏魂类效果 / 商人交互 / NPC 伤害判定与敌对触发 / 友好 NPC 识别。


## 25. Blood Echoes 掉落归属

### 25.1 个人掉落独立

- 客机死亡的 Blood Echoes 掉落在 Host 的世界中，但**归属客机个人**
- 每个玩家只能看到和拾取**自己掉落的**Blood Echoes
- 客机离开 Session 后，其掉落的 Blood Echoes **保留在 Host 的世界中**
- 客机重新加入同一 Host 的 Session 时，可以继续拾取

### 25.2 同时死亡

各玩家 Blood Echoes 掉落**分开计算**，互不干扰。视觉上，每个玩家看到的是自己掉落的血渍。

### 25.3 再次死亡与 World Reset

- 参考原版规则：玩家再次死亡时，**之前掉落的 Blood Echoes 消失**，只保留最新一次死亡的掉落
- World Reset **不应清除**玩家掉落的 Blood Echoes
- 这些掉落物应继续存在于世界中，直到被玩家拾取或玩家再次死亡

### 25.4 敌人吸收

如果附近有敌人，Blood Echoes 可能被敌人吸收。Mod 应**保留此原版机制**，即使敌人被吸收的 Blood Echoes 属于特定玩家，其他玩家击杀该敌人后**无法拾取**，只有掉落者本人可以拾取。


## 26. 网络同步

```text
Player：Position / Rotation / Animation / HP / Max HP / Death / Respawn / Status
Enemy：Position / HP / Death / Aggro / Important AI State
Boss：HP / Phase / Aggro / Target / State / Death
World：Enemy State / Boss State / Lantern State / Relevant World Flags
```


## 27. Authority 模型

```text
Host Authority：Enemy / Boss / World / Important Gameplay State / 物品发放 / PvP 判定
Client：Local Player Input / Presentation / Interpolation
```


## 28. 防止多人状态竞争

重点：两名玩家同时攻击敌人 / 同时拾取物品 / 玩家死亡与 Boss 状态更新同时 / Lantern Rest 与玩家死亡同时 / Disconnect 与 Party Wipe 同时 / Boss Death 与 Player Death 同时 / Invasion 加入与 Boss Start 同时。必须定义优先级和事件顺序。


## 29. 网络传输层

### 29.1 传输层选型

```text
移除：Steam Networking API（Bloodborne 无 Steam 版本）

首选：GameNetworkingSockets（开源，无需 Steam）
备选：ShadNet-P2P 方案（已有 Bloodborne 召唤代理支持）
备选：RakNet（跨平台 C++ 网络引擎）
备选：EasyTier / Noray 等 P2P 组网方案
自建：基于可靠 UDP + NAT 穿透
```

GameNetworkingSockets 提供可靠/不可靠 UDP 消息、消息分片重组、P2P 网络和 NAT 穿透、加密。它是 Valve 开源的核心网络传输库，无需 Steam 客户端即可独立使用。RakNet 是另一个跨平台开源 C++ 网络引擎，支持 RPC、语音通信、NAT 隧道和可靠的通信协议。

### 29.2 连接策略（智能编排）

```text
ConnectionOrchestration
├── Step 1：尝试 P2P 直连（STUN / 打洞）
│   ├── 成功 → 建立 E2EE 直连
│   └── 失败 → 进入 Step 2
├── Step 2：尝试中继连接
│   ├── 选择信誉良好的中继节点
│   ├── 通过中继建立 E2EE 通道
│   └── 中继无法解密数据
├── Step 3：中继失败
│   ├── 尝试备用中继
│   └── 全部失败 → 报告连接失败
└── 全程：连接质量监控（延迟/丢包/抖动）
```

### 29.3 ShadNet-P2P 集成评估（P0 任务）

ShadNet-P2P 由 Wozzardman 维护，支持 Matching2 和 Bloodborne 召唤代理，包含实验性 seamless co-op 模式。其 seamless 模式让 broker 匹配不同地图的铃铛，并将 host 的放置位置传递给 guest，使 client 能够在正常房间加入前移动 guest。启动日志必须显示 `seamless co-op enabled` 和 `anywhere summons enabled`。

**现有能力：**

- Matching2 支持
- Bloodborne 召唤代理
- 实验性 seamless co-op 模式
- 跨地图匹配
- Host 位置传递给 Guest

**待确认：**

- 与 bbport Windows 原生版的兼容性
- 是否需要修改协议
- 能否直接复用或需要 fork
- 性能开销评估

### 29.4 延迟与会话管理

**延迟阈值：**

```text
延迟阈值：500ms
高于 500ms：踢出高频玩家
会话槽位与网络状态分离：即使延迟高达 500ms，只要连接未断，就不启动会话保留计时器
```

**会话保留时间：**

| 场景 | 保留时间 | 说明 |
|---|---|---|
| 短暂延迟/丢包 | 3~5 秒 | 允许网络波动，角色进入“等待”状态 |
| 意外断线 | 30~60 秒 | 保留会话槽位，超时则彻底移除 |
| 主动离开 | 立即 | 不保留会话槽位 |
| Host 断线 | 30~60 秒 | 保留整个 Session，超时则终止 |

**加入与断线规则：**

- 主机开放世界则随时可以加入
- Boss 区和 PvP 状态加入时视为死亡状态，只能观战，但没有死亡惩罚
- 断线计入 PvP 减员
- 重连令牌：客机断线时生成一次性重连令牌，重连时凭令牌恢复原会话


## 30. 区域切换与地图同步

### 30.1 核心原则

> 跟随 Host，实时同步。客机的区域切换由 Host 驱动。Host 进入新区域时，所有客机被同步传送至 Host 身边。

### 30.2 具体实现

- **移除所有雾墙/屏障限制**（需 AI-A1 确认可行性）
- **Host 进入新区域时，所有客机强制同步传送**
- 新区域的世界状态以 Host 的世界状态为准，客机内存中的临时世界状态被覆盖
- 允许客机在**同一区域内**自由行动
- 允许不同玩家处于不同区域（分头行动）
- 各区域的敌人/世界状态独立管理
- 敌人缩放仅统计同区域玩家
- Boss 战/PvP 期间禁止区域切换
- 任意玩家可使用“分离”功能离开 Session

注意：移除雾门在 Bloodborne 中涉及原版区域加载逻辑，AI-A1 必须先确认是否可安全实现，不可行则保留雾门但确保客机不被遣返。


## 31. 数据持久化与存档安全

### 31.1 实时写入

- 获得物品**实时写入**个人存档
- 崩溃不丢失数据
- 定期（如每 60 秒）将关键状态写入临时文件
- 如果游戏崩溃，下次启动时 Mod 读取临时文件，恢复到最近一次安全状态

### 31.2 非法物品定义与检测

非法物品指“在正常游戏流程中，不可能通过合法途径，在当前世界进度下获得或持有的物品”：

| 类别 | 说明 | 检测方式 |
|---|---|---|
| 进度越界物品 | 当前世界进度尚未解锁，但客机已持有 | 维护“世界进度-物品解锁”对照表 |
| 数量异常物品 | 超出正常游戏允许上限，或获得不可重复物品 | 校验最大堆叠数量和唯一性标志 |
| 数值异常物品 | 修改器产生的非法强化等级、宝石属性 | 校验武器强化等级和宝石属性合法性 |
| 来源不明物品 | 无法追溯至任何合法获取途径 | 实时写入时标记物品来源 |

**核心建议：**

1. **以 Host 世界状态为基准**：Host 校验客机关键物品是否“超前”，记录日志并暂时禁用或拒绝加入
2. **实时写入 + 来源标记**：Mod 记录物品来源，只写入来源合法的物品
3. **白名单机制**：优先实现基于“合法来源”的白名单机制

### 31.3 存档安全

- 所有对存档的写入操作必须**原子化**（先写临时文件，再替换原文件）
- 如果写入过程中崩溃，原存档不受影响
- Session 异常终止时，客机端应能在超时后自动清理临时世界状态，安全返回个人进度
- 不损坏个人存档


## 32. 兼容性与故障恢复

### 32.1 兼容性

- **游戏版本**：Mod 启动时检查游戏版本，不匹配则拒绝加载
- **Mod 版本**：Session 建立时双方 Mod 版本必须一致，不一致拒绝连接
- **存档格式**：Mod 不修改原版存档格式，所有 Mod 数据保存在独立文件中
- **操作系统**：记录 Mod 支持的 OS 版本，在安装说明中注明

### 32.2 故障恢复

- **崩溃恢复**：定期写入关键状态到临时文件，下次启动时恢复
- **Session 异常终止**：Host 崩溃或强制关闭时，客机端超时后自动清理临时世界状态
- **存档损坏预防**：所有写入操作原子化


## 33. Hook / Runtime 架构

```text
Mod
├── Core（HookManager / AddressResolver / VersionChecker / Logger）
├── Network（Session / Transport / Replication / Protocol / Blacklist）
├── Security（E2EE / KeyExchange / IntegrityCheck / Audit）
├── Gameplay（Player / Enemy / Boss / Death / Lantern / World / Invasion / Balance
│            Loot / Consumable / Shop / NPC / SessionState / Arena）
└── Tests
```


## 34. 日志系统

```text
[SESSION] [NETWORK] [PLAYER] [DEATH] [RESPAWN] [WORLD] [LANTERN]
[ENEMY] [BOSS] [INVASION] [PVP] [BALANCE] [HOOK] [ERROR]
[SESSION_STATE] [LOOT] [CONSUMABLE] [SHOP] [NPC]
[ANTICHEAT] [BLACKLIST] [HOST_TRUST] [ARENA]
[SECURITY] [CRYPTO] [RELAY] [REPUTATION]
```

死亡流程记录 Player ID / Session ID / Boss Active / Player Count / Death Reason / Blood Echoes State / Respawn State / Max HP Before/After / World Reset Triggered / Party Wipe / Lantern Reset。

PvP 流程记录 Session ID / Team / Player ID / PvP State / Death Order / Team Alive Count / Team Wipe / Resolution / Reward / Blood Echoes State / Disconnect / World Reset Triggered。

不得记录任何隐私数据。


## 35. 测试矩阵

### 35.1 基础 Session
1P/2P/3P/4P，Join/Leave/Reconnect/Disconnect，Host 退出遣返，Host 死亡继续。

### 35.2 普通区域死亡
单人死亡、从 Lantern 重生、Max HP Penalty、连续死亡、多人连续死亡、其他玩家存活、已死亡敌人不刷新、未死亡敌人保持状态、Session 不断开。

### 35.3 Blood Echoes
掉落/回收/敌人持有/再次死亡/重连/World Reset 后状态/客机离开后回来捡魂/同时死亡分开计算。

### 35.4 Lantern
Rest / Penalty 清除 / Max HP 恢复 / Enemy Reset / World State / 多人同步。

### 35.5 Boss
开始/一人死亡/观战模式/其他人继续/多人死亡/HP/Phase 同步/Party Wipe/Boss Reset/Boss Death/Boss Reward/Session 继续。

### 35.6 Invasion / PvP
单人死亡不立即结算/不立即 Lantern 重生/等待观战/有人存活时 Invader 不能获胜/入侵者死亡即 CoopWin/合作队全灭才 InvaderWin/结算后 Session 继续/不触发 World Reset/Blood Echoes 按原版/无重复奖励/PvE 与 PvP 完全分离/断线计入 PvP 减员。

### 35.7 物品掉落与拾取
随机独立/固定光点每人一份/宝箱每人一份/Boss 每人一份（非唯一）/唯一不重复/断线重连不重复/Session 重连不重复/无刷物品漏洞。

### 35.8 消耗品使用
独立库存/捏魂只增加自己/回复与增益只作用于自己/不同步扣除他人/不复制/不重复触发。

### 35.9 商店与 NPC 保护
独立购买/独立扣货币/独立获得/解锁基于共享世界进度/无复制；NPC 保护免疫伤害/不导致敌对/不影响敌人/Boss/Invader/配置同步/中途更改实时生效/关闭恢复原版。

### 35.10 世界状态覆盖与恢复
客机加入使用主机世界状态/角色数据不被覆盖/Session 中获得物品保留/离开恢复个人进度/主机世界不受影响/断线崩溃后存档不损坏/不复制唯一物品。

### 35.11 网络异常与断线
延迟 500ms 阈值/踢出高频玩家/会话保留时间/重连令牌/断线计入 PvP 减员/加入时 Boss/PvP 状态观战。

### 35.12 区域切换
跟随 Host 同步传送/移除雾门（如可行）/Boss/PvP 期间禁止切换/分离功能/分头行动支持。

### 35.13 反作弊（仅 PvP）
版本不匹配被拒绝/数据包篡改重放被检测/高频被限制/唯一物品重复被阻止/Boss 掉落重复被阻止/PvP 伤害位置异常被检测/延迟不误判/PvE 不启用行为反作弊。

### 35.14 决斗场
血之同胞/青骑士 1v1 进入退出/三个石像对应场地正确/房主不能携带客机/不触发 World Reset/不刷新敌人/死亡不施 Penalty/不掉落 Blood Echoes/胜利获得对应眼眸宝珠/失败无任何损失/誓约升级按胜利计分/断线处理/与 Invasion 互斥/结束后可重加入 Session/2v2 与 FFA 不实现/社区规则默认关闭。

### 35.15 安全审计
E2EE 实现正确/密钥交换安全/HMAC 校验有效/防重放有效/中继无法解密数据/中继无法篡改数据/中继无法重放数据/中继不记录日志/抗 Sybil 攻击/抗 DDoS/不收集任何隐私数据。


## 36. 性能与网络指标

Ping / RTT / Packet Loss / Tick Rate / Replication Frequency / State Correction / CPU / Memory / Hook Overhead。

**性能预算：**

```text
├── 最大延迟：500ms
├── 最大丢包率：< 5%
├── 最大重传率：< 10%
├── 网络包频率：< 30 packets/sec
├── 反作弊开销：< 1ms/frame
├── 日志写入：异步，不阻塞主线程
└── 内存开销：< 100MB
```

关注：Boss 战网络抖动 / 多人同时攻击 / 大量 Enemy / 区域切换 / Lantern Reset / Party Wipe。


## 37. Issue 升级规则

### 必须创建 Issue

- 大量源码修改（核心模块 / Player / World / Boss / Save / 网络协议 / 大量 Hook）
- 逆向结论不确定
- 核心行为存在风险（死亡/Lantern/Boss/网络/Inventory/Reward）
- Crash / Deadlock / Memory Corruption / Save Corruption / Duplicate Reward / State Desync / Infinite Respawn / Infinite Enemy Reset

Issue 至少包含：Title / Background / Observed Behavior / Expected Behavior / Evidence / Affected Modules / Possible Cause / Risk / Proposed Solutions / Verification Plan。

### 待确认项汇总

**PvP：** 双方同时全灭如何判定 / Invader 主动断线是否算失败 / Boss Start 与 Invasion 同时 / PvP 死亡时 Blood Echoes 掉落时机 / PvP 死亡是否施加独立惩罚 / 结算后死亡 Co-op 玩家原地还是 Lantern 重生 / 队伍综合强度公式 / 入侵者伤害减免触发条件 / 高等级协力者下修可行性 / 2 名入侵者同步与结算 / 是否允许玩家选择 PvP 难度模式

**物品与商店：** 固定拾取点行为 / Boss 掉落分配 / 唯一物品处理 / 唯一性标志识别 / 商人交互 / 购买防复制

**消耗品：** 多人使用行为 / 捏魂类效果 / 库存同步方案

**NPC 保护：** 范围定义 / 如何区分友好 NPC 与敌人 / 中途更改同步方案

**Session 世界状态：** 存储结构 / 如何不写入存档临时覆盖 / 异常中断恢复 / 独立存档格式

**反作弊：** 阈值 / 误判处理 / 协议签名 / Blacklist 存储与同步 / 不同 Mod 版本兼容 / PvP 延迟补偿与作弊检测边界

**决斗场：** 函数地址 / 匹配逻辑 / 是否适用 HP -30% / 房主进入时 Session 处理 / 社区规则可选配置 / 血之同胞失败扣分改为失败无损失的具体 Hook 方案

**网络：** 500ms 阈值实现 / 会话保留计时器 / 重连令牌 / 观战模式实现

**区域切换：** 雾门移除可行性 / Host 驱动同步传送实现 / Boss/PvP 期间禁止切换

**数据持久化：** 实时写入实现 / 原子化写入 / 非法物品检测 / 崩溃恢复

**安全：** E2EE 实现 / 密钥交换协议 / 中继安全协议 / 抗 Sybil 机制 / 抗 DDoS 机制 / 信誉系统设计 / 安全审计流程


## 38. AI 修改代码的基本原则

每次修改必须回答：改了什么？为什么改？修改了哪个模块？影响哪些系统？如何验证？是否存在兼容性风险？

禁止：为了编译通过而删除功能；为了消除报错而大面积注释代码；用 Mock 冒充真实功能；用日志输出冒充网络同步；用假数据冒充逆向结论；编译成功就宣布功能完成。


## 39. 验证标准

```text
Compile Verified → Runtime Verified → Multiplayer Verified
                 → Regression Verified → Feature Verified
```

> Build Passed 不等于 Feature Complete。


## 40. 第一阶段验收标准

### Session
- [ ] 2P 建立 Session
- [ ] Player 加入 / 离开
- [ ] Session 不因普通死亡结束
- [ ] Host 退出遣返所有客机
- [ ] Host 死亡继续 Session

### 普通区域
- [ ] 死亡后从最近点亮 Lantern 重生
- [ ] 快速复活不改变原版重生点
- [ ] 复活后死亡玩家自身 Max HP 大幅降低
- [ ] 连续死亡进一步降低
- [ ] 单人死亡不刷新敌人
- [ ] 其他玩家可继续战斗
- [ ] 已死亡敌人不因队友死亡重新出现

### 死亡经济
- [ ] Blood Echoes 按原版掉落 / 回收
- [ ] 敌人持有机制不变
- [ ] 客机离开后回来可捡魂
- [ ] 同时死亡分开计算
- [ ] World Reset 后魂仍在
- [ ] 再次死亡只保留上一次掉落
- [ ] 无重复 Reward / 无异常 Save

### Lantern
- [ ] Rest 清除 Max HP Penalty
- [ ] Max HP 恢复
- [ ] World Reset 与原版一致
- [ ] Enemy Reset 正确同步

### Boss
- [ ] 多人 Boss 战
- [ ] 单人死亡不结束 Boss 战
- [ ] 死亡玩家进入观战模式
- [ ] 存活玩家继续
- [ ] Party Wipe 正确 Reset
- [ ] Boss Death 正确同步
- [ ] Session 不因 Boss Death 自动销毁

### PvP
- [ ] 单人死亡不立即结算 / 不立即 Lantern 重生
- [ ] 进入等待/观战
- [ ] 有人存活时 Invader 不能获胜
- [ ] 入侵者死亡即 CoopWin
- [ ] 合作队全灭才 InvaderWin
- [ ] 结算后 Session 继续
- [ ] 不触发 World Reset
- [ ] Blood Echoes 按原版
- [ ] 无重复奖励
- [ ] PvE 与 PvP 完全分离
- [ ] 断线计入 PvP 减员

### 物品
- [ ] 随机独立 / 固定每人一份 / 宝箱每人一份 / Boss 每人一份（非唯一）
- [ ] 唯一 Boss 掉落不重复
- [ ] 无重复奖励 / 无异常 Save / 无刷物品漏洞

### 消耗品
- [ ] 独立库存
- [ ] 捏魂只增加自己
- [ ] 回复与增益只作用于自己
- [ ] 不同步扣他人
- [ ] 无复制 / 无重复触发 / 无异常 Save

### 商店
- [ ] 独立购买 / 扣货币 / 获得
- [ ] 解锁基于共享世界进度
- [ ] 无复制 / 无异常 Save

### NPC 保护
- [ ] Host 可配置启用/禁用
- [ ] 开启后友好 NPC 免疫玩家伤害
- [ ] 不导致 NPC 敌对
- [ ] 敌人/Boss/Invader 不受影响
- [ ] 配置同步到所有客户端

### 世界状态覆盖与恢复
- [ ] 客机加入时临时使用主机世界状态
- [ ] 客机角色数据不被覆盖
- [ ] Session 中获得物品保留
- [ ] 离开恢复个人世界进度
- [ ] 主机世界不受影响
- [ ] 断线崩溃后存档不损坏
- [ ] 无唯一物品复制漏洞
- [ ] 独立存档格式

### 网络异常与断线
- [ ] 500ms 延迟阈值
- [ ] 会话保留时间
- [ ] 重连令牌
- [ ] Boss/PvP 加入时观战
- [ ] 断线计入 PvP 减员

### 区域切换
- [ ] 跟随 Host 同步传送
- [ ] 移除雾门（如可行）
- [ ] Boss/PvP 期间禁止切换

### 数据持久化
- [ ] 实时写入
- [ ] 崩溃不丢失数据
- [ ] 原子化写入
- [ ] 非法物品检测

### 反作弊
- [ ] 仅 PvP 启用行为反作弊
- [ ] PvE 不启用
- [ ] Mod / 游戏版本握手（强校验）
- [ ] 协议校验 / 防重放 / 速率限制
- [ ] 物品发放记账
- [ ] PvP 伤害与命中校验
- [ ] 误判处理 / 分级响应
- [ ] 不泄露隐私 / 不影响原版机制

### 决斗场
- [ ] 血之同胞与青骑士 1v1 可通过原版石像进入
- [ ] 三个石像对应场地正确
- [ ] 不能由房主携带客机进入
- [ ] 进入时主动离开 Session
- [ ] 不触发 World Reset / 不刷新敌人
- [ ] 死亡不施 Max HP Penalty / 不掉落 Blood Echoes
- [ ] 血之同胞胜利获得红色眼眸宝珠
- [ ] 青骑士胜利获得蓝色眼眸宝珠
- [ ] 失败无任何损失
- [ ] 誓约升级按胜利计分
- [ ] 断线正确处理
- [ ] 与 Invasion 互斥
- [ ] 结束后可重加入 Session
- [ ] 2v2 / FFA 不实现
- [ ] 社区规则默认关闭
- [ ] 无额外防刷机制

### 安全
- [ ] E2EE 实现正确
- [ ] 密钥交换安全
- [ ] HMAC 校验有效
- [ ] 防重放有效
- [ ] 中继无法解密数据
- [ ] 中继无法篡改数据
- [ ] 中继不记录日志
- [ ] 抗 Sybil 攻击
- [ ] 抗 DDoS
- [ ] 不收集任何隐私数据
- [ ] 所有网络代码通过安全审计


## 41. 开发顺序

```text
Phase 0   安全设计（威胁模型 / 安全架构 / 密码学方案）
Phase 1   环境与版本确认 + bbport runtime 逆向
Phase 2   逆向 Player / Death / Lantern / World
Phase 3   逆向 Loot / Consumable / Shop / NPC
Phase 4   Session / Network / 世界状态覆盖 / 独立存档
Phase 5   Player Replication
Phase 6   普通区域多人死亡 + Max HP Penalty
Phase 7   Enemy / World Sync / Blood Echoes 归属
Phase 8   Loot / Consumable / Shop / NPC（实现）
Phase 9   Boss（含观战模式）
Phase 10  Lantern Reset
Phase 11  Invasion / PvP / 网络异常处理
Phase 12  区域切换
Phase 13  决斗场
Phase 14  Balance / 数据持久化 / 兼容性
Phase 15  Regression / Stability
```


## 42. AI 最终交付报告

```text
## Completed
## Modified Files
## Core Changes
## Reverse Engineering Findings
## Verification
## Multiplayer Test
## Known Issues
## Open Issues
## Risks
## Next Steps
## Feature Status
Not Started / In Progress / Runtime Verified / Multiplayer Verified / Feature Verified
```


## 43. 最终设计结论

> 原版死亡经济尽量不动，多人死亡流程重新设计。
>
> Seamless 消除的是等待和联机流程，不是 Bloodborne 的风险。
>
> 玩家死亡属于 Player-Level Event；Lantern Rest / Party Wipe 才是 World-Level Reset Event。
>
> 玩家死亡后从 Lantern 重生，但单人死亡不触发 World Reset。
>
> 死亡惩罚在 Lantern 重生后施加，在 Lantern Rest / 世界重置时消除。
>
> Blood Echoes 掉落、回收、敌人持有等原版死亡经济机制保持不变。
>
> Blood Echoes 个人独立，再次死亡只保留最后一次，World Reset 不清除。
>
> PvP 中单个玩家死亡不是结算点，只有合作队全灭或入侵队全灭才进入结算。
>
> 随机掉落独立，固定拾取与 Boss 掉落默认每人一份，唯一物品不得重复。
>
> 每个玩家的消耗品库存独立，使用后只影响使用者自身。
>
> 商店与商人交互按个人处理；Host 可配置 NPC 保护。
>
> 客机加入主机 Session 时临时使用主机世界状态；离开或中断时恢复自己的个人进度。
>
> 区域切换跟随 Host，实时同步，Boss/PvP 期间禁止切换。
>
> 使用独立存档格式，与原版存档完全隔离。
>
> 决斗场按官方原版设计，仅 1v1，独立进入，不携带客机，失败无任何损失。
>
> 反作弊仅限 PvP，PvE 不检测、不踢人、不记录。
>
> 不收集、不存储、不上传任何玩家隐私数据。
>
> 安全设计先行，安全审计独立，安全一票否决。
>
> 所有参数可配置、可测试、可通过实战数据迭代调整。


## 44. 多 AI 协作架构

### 44.1 总体架构

```text
┌───────────────────────────────────────────────────────────────────┐
│                        项目总控 / Integration Lead                 │
│                  （AI-00，负责跨任务协调与最终集成）                  │
└───────────────────────────────────────────────────────────────────┘
         │                    │                    │
         ▼                    ▼                    ▼
┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐
│   任务书 A       │  │   任务书 B       │  │   任务书 C       │
│  游戏逻辑与      │  │  去中心化网络    │  │  安全架构与      │
│  Gameplay       │  │  基础设施        │  │  威胁模型        │
│                 │  │                 │  │                 │
│  AI-A0 平台适配 │  │  AI-B1 传输层   │  │  AI-C1 密码学   │
│  AI-A1 逆向     │  │  AI-B2 发现层   │  │  AI-C2 安全架构 │
│  AI-A2 会话     │  │  AI-B3 信誉层   │  │  AI-C3 安全审计 │
│  AI-A3 游戏系统 │  │  AI-B4 中继层   │  │  AI-C4 渗透测试 │
│  AI-A4 平衡     │  │                 │  │                 │
└─────────────────┘  └─────────────────┘  └─────────────────┘
         │                    │                    │
         └────────────────────┼────────────────────┘
                              ▼
                    ┌─────────────────┐
                    │  Integration    │
                    │  集成与回归测试  │
                    │  AI-00 + 各组长 │
                    └─────────────────┘
```

### 44.2 AI-00：项目总控 / Integration Lead

**职责：**

- 跨任务书协调
- 接口契约管理
- 集成测试
- 冲突裁决
- 最终验收

**不负责：** 不写具体代码；不做具体逆向；不设计密码学方案

**必须交付：**

```text
docs/
├── interfaces/          接口契约
├── integration/         集成测试报告
├── conflict_log/        冲突记录与裁决
└── final_report/        最终验收报告
```

**裁决权：** 当 AI-A/B/C 之间出现接口冲突时，AI-00 拥有最终裁决权；当安全审计（AI-C3）与开发（AI-A/B）冲突时，**安全优先**。

### 44.3 AI-A0：平台适配工程师

**角色：** Platform Integration Engineer

**职责：**

- bbport Windows 原生版进程分析
- 与 bbport 社区沟通（了解 Hook 接口）
- Mod 加载机制设计
- 启动器（`Bloodborne.exe`）集成
- 平台兼容性测试
- 与 bbhost/Paleblood 等其他平台的兼容性评估
- eboot 转换流程分析

**依赖：** 无（独立工作）
**输出：** 平台适配层接口文档
**独立分支：** `agent/a0-platform-integration`

### 44.4 AI-A1：逆向工程 & Core Runtime

**角色：** Senior Game Reverse Engineer + Native C++ Engineer

**职责：**

```text
├── bbport runtime 结构 / 游戏进程内存布局
├── Player / Death / Lantern / World 逆向
├── Enemy / Boss / Session 逆向
├── Loot / Consumable / Shop / NPC 逆向
├── Arena / PvP / Save 逆向
├── 区域加载 / 雾门逻辑逆向
├── 原版 Boss 战死亡机制逆向
├── 地址 / 结构体 / Hook 记录
└── 版本安全（VersionChecker / AddressResolver）
```

**优先级：**

```text
AI-A1 Priority 1: bbport runtime结构 / 游戏进程内存布局
AI-A1 Priority 2: Player / Death / Lantern / World
AI-A1 Priority 3: Enemy / Boss / Session
AI-A1 Priority 4: Loot / Consumable / Shop / NPC
AI-A1 Priority 5: Arena / PvP / Save / 区域切换
```

**必须交付：**

```text
docs/re/
├── player.md / death.md / blood_echoes.md
├── enemy.md / boss.md / world.md / lantern.md
├── multiplayer.md / arena.md
├── loot.md / consumable.md / shop.md / npc.md
├── addresses.md / structures.md / findings.md
```

每个结论标记：`Confirmed / Probable / Hypothesis / Unknown`。

**独立分支：** `agent/a1-reverse-engineering`

### 44.5 AI-A2：Seamless Session & Replication

**角色：** Senior Multiplayer Networking Engineer + Session Engineer

**职责：**

```text
├── Session 管理（创建/加入/离开/重连/遣返）
├── Host 生命周期（死亡/退出/断线/进入决斗场）
├── Player Replication / World Replication
├── 客机世界状态覆盖与恢复
├── 独立存档格式
├── 区域切换同步
├── 网络异常处理（500ms 阈值 / 会话保留 / 重连令牌）
├── 数据持久化（实时写入 / 原子化 / 崩溃恢复）
└── 非法物品检测
```

**依赖：** AI-A1 提供的 Hook 和事件，AI-C1 提供的 E2EE 通道。

**独立分支：** `agent/a2-session-replication`

### 44.6 AI-A3：Gameplay Systems

**角色：** Game Systems Engineer

**职责：**

```text
├── Death Penalty（Max HP Penalty / 多层惩罚池）
├── Lantern Reset / Party Wipe
├── Boss 战观战模式
├── Loot（随机/固定/Boss/唯一）
├── Consumable 独立计算
├── Shop 个人化
├── NPC 保护
├── Blood Echoes 归属
└── Arena（决斗场）
```

**依赖：** AI-A1 的逆向结论，AI-A2 的同步接口。

**独立分支：** `agent/a3-gameplay-systems`

### 44.7 AI-A4：Balance & PvP

**角色：** Game Balance Engineer + PvP Engineer

**职责：**

```text
├── Death Penalty 参数调优
├── PvE Scaling（配置文件化）
├── PvP Scaling（动态平衡）
├── 队伍综合强度计算
├── 入侵者动态补偿
├── 高等级玩家下修
├── Invasion 规则
├── Reward Balance
└── 实战数据收集与分析
```

**独立分支：** `agent/a4-balance-pvp`

### 44.8 AI-B1：传输层 & 连接编排

**角色：** Network Transport Engineer

**职责：**

```text
├── P2P 直连（STUN / 打洞）
├── NAT 穿透
├── 中继回退
├── 连接编排（直连优先 / 中继兜底）
├── 连接质量监控（延迟/丢包/抖动）
├── 传输层实现（基于 GameNetworkingSockets 或类似库）
└── 与 AI-C1 的 E2EE 接口对接
```

**独立分支：** `agent/b1-transport`

### 44.9 AI-B2：节点发现 & 信令

**角色：** P2P Discovery Engineer

**职责：**

```text
├── 节点发现（DHT / Gossip）
├── 信令服务
├── 房间管理 / 匹配逻辑
├── NAT 类型检测
└── 无中心化节点发现
```

**独立分支：** `agent/b2-discovery`

### 44.10 AI-B3：信誉系统 & 抗滥用

**角色：** Reputation System Engineer

**职责：**

```text
├── 节点信誉系统
├── 抗 Sybil 机制
├── 抗 DDoS 机制
├── 中继节点筛选
├── 异常行为检测 / 速率限制
└── 信誉数据加密签名
```

**独立分支：** `agent/b3-reputation`

### 44.11 AI-B4：中继节点实现

**角色：** Relay Node Engineer

**职责：**

```text
├── 中继节点协议
├── 加密数据转发
├── 不存储 / 不记录日志
├── 中继节点生命周期
├── 中继节点退出处理
└── 中继节点信誉上报
```

**独立分支：** `agent/b4-relay`

### 44.12 AI-C1：密码学 & E2EE

**角色：** Cryptography Engineer

**职责：**

```text
├── 端到端加密（X25519 + ChaCha20-Poly1305）
├── 密钥交换
├── 前向保密 / 密钥轮换
├── HMAC / 序列号 / 时间戳
├── 防重放
└── 与 AI-B1 的传输层接口
```

**必须向 AI-B1 / AI-B4 提供：**

```text
├── E2EEChannel：encrypt / decrypt / verifyIntegrity
├── KeyExchange：generateKeyPair / computeSharedSecret / deriveSessionKey
└── KeyRotation：rotateSessionKey
```

**独立分支：** `agent/c1-cryptography`

### 44.13 AI-C2：安全架构 & 威胁模型

**角色：** Security Architect

**职责：**

```text
├── 威胁模型设计
├── 安全架构设计
├── 抗 Sybil 设计 / 抗 DDoS 设计
├── 隐私保护方案
├── 中继安全协议
├── 安全约束定义
└── 安全设计文档
```

**独立分支：** `agent/c2-security-architecture`

### 44.14 AI-C3：安全审计

**角色：** Security Auditor

**职责：**

```text
├── 设计审计（实现前）
├── 代码审计（合并前）
├── 密码学审计 / 隐私审计
├── 持续审计
└── 一票否决权
```

**裁决权：** 任何网络代码未通过 AI-C3 审计，不得合并。

**独立分支：** `agent/c3-security-audit`

### 44.15 AI-C4：渗透测试

**角色：** Penetration Tester

**职责：**

```text
├── 模拟 MITM 攻击 / Sybil 攻击 / DDoS 攻击
├── 模拟中继节点攻击 / 客户端攻击
├── 渗透测试报告
└── 修复验证
```

**独立分支：** `agent/c4-penetration-test`


## 45. 安全架构与威胁模型

### 45.1 威胁模型

```text
ThreatModel
├── 被动攻击：流量窃听 / 流量分析 / 元数据收集
├── 主动攻击：MITM / 数据包篡改 / 重放 / 注入 / DoS/DDoS
├── 身份攻击：Sybil / 身份伪造 / 女巫攻击
├── 中继节点攻击：窃听 / 篡改 / 丢弃数据 / 追踪玩家
└── 客户端攻击：作弊 / 扫描 / 攻击其他玩家
```

### 45.2 安全架构

```text
SecurityArchitecture
├── 端到端加密（E2EE）
│   ├── 密钥交换：X25519（ECDH）
│   ├── 对称加密：ChaCha20-Poly1305
│   ├── 前向保密：每次会话独立密钥
│   └── 密钥轮换：定期更换会话密钥
├── 身份验证
│   ├── 无中心化身份，基于密钥对
│   ├── 首次连接交换公钥
│   └── 公钥指纹验证（可选，防 MITM）
├── 数据完整性
│   ├── 每条消息附带 HMAC
│   ├── 序列号防重放
│   └── 时间戳防重放
├── 中继安全
│   ├── 中继仅转发加密数据
│   ├── 中继无法解密 / 无法篡改 / 无法重放
│   └── 中继不记录日志
├── 隐私保护
│   ├── 不收集 IP / 平台 ID / 硬件信息 / 地理位置
│   └── 不使用持久标识
└── 抗攻击
    ├── 抗 Sybil（信誉系统 + 工作量证明可选）
    ├── 抗 DDoS（速率限制 + 中继隐藏）
    ├── 抗 MITM（公钥指纹验证）
    └── 抗重放（序列号 + 时间戳）
```

### 45.3 密钥交换流程

```text
KeyExchange
├── Step 1：双方生成临时密钥对（X25519）
├── Step 2：通过信令交换公钥
├── Step 3：双方计算共享密钥（ECDH）
├── Step 4：通过 HKDF 派生会话密钥
├── Step 5：验证公钥指纹（可选，防 MITM）
├── Step 6：建立 E2EE 通道
└── Step 7：定期轮换会话密钥（前向保密）
```

### 45.4 中继安全协议

```text
RelaySecurityProtocol
├── 中继节点仅能看到：
│   ├── 加密后的数据包
│   ├── 源节点标识（临时，非持久）
│   └── 目标节点标识（临时，非持久）
├── 中继节点无法看到：
│   ├── 游戏数据内容 / 玩家真实 IP / 玩家身份 / 任何元数据
├── 中继节点无法：
│   ├── 解密数据 / 篡改数据（HMAC 会失败）
│   ├── 重放数据（序列号会失败）/ 追踪玩家
└── 中继节点必须：
    ├── 仅转发加密数据 / 不存储任何数据
    ├── 不记录任何日志 / 可随时退出
```

### 45.5 抗 Sybil 机制

```text
AntiSybil
├── 信誉系统：新节点信誉低
├── 工作量证明（可选）：加入网络需要计算成本
├── 时间成本：新节点需要时间积累信誉
├── 社交验证（可选）：由已知节点推荐
├── 中继节点上限：防止单一实体控制大量中继
└── 异常检测：大量新节点同时加入时触发警报
```

### 45.6 安全审计流程

```text
SecurityAudit
├── 设计审计：在实现前审计安全设计
├── 代码审计：在合并前审计所有网络代码
├── 渗透测试：模拟攻击，验证防御
├── 密码学审计：验证加密实现正确
├── 隐私审计：验证无隐私泄露
└── 持续审计：每次网络代码变更都触发审计
```


## 46. 反作弊与完整性保护

### 46.0 适用范围

> **反作弊仅限 PvP / Invasion 模式。正常 PvE 合作不启用反作弊检测与响应。**

```text
联机基础完整性（始终启用）
├── Mod / 游戏版本握手
├── 协议版本校验 / Session ID 校验
├── 防重放 / 包大小 / 速率基础限制
├── 物品发放记账 / 唯一物品防重复
├── 客机世界状态不写入个人存档
└── 存档写入原子性

PvP 反作弊（仅 Invasion / PvP 启用）
├── 伤害合理性 / 命中距离 / 攻击频率校验
├── 位置 / HP 突变检测
├── 无敌帧滥用检测
├── 消耗品异常 / 非法状态切换检测
├── Host 判定伤害 / 命中 / 死亡
├── 异常响应
└── 本地黑名单自动拦截
```

**PvE 模式：** 不进行行为反作弊检测；不校验伤害/命中/位置/攻击频率；不因疑似作弊自动踢人；不写反作弊日志；不启用黑名单自动拦截。

**PvP 模式：** Invasion 开始启用，结束立即关闭。

### 46.1 Host 作弊

> 在 Host Authority 架构下，Host 作弊无法被完全阻止。

**Client 自我保护：**

```text
ClientSelfProtection
├── 角色数据不被 Host 覆盖
├── 世界状态不被写入个人存档
├── 非法物品拒绝写入
├── 唯一物品防重复
├── 存档写入原子性
├── 异常中断后恢复个人进度
├── 可查看当前 Host 配置
├── 可随时退出 Session
└── 可清除本地缓存的 Host 世界状态
```

不做：完全阻止 Host 作弊 / 中心化封禁 / 上传行为数据 / 扫描 Host 进程 / 读 Host 内存。

### 46.2 本地黑名单（仅基于游戏名称）

**核心原则：** 本地黑名单只记录游戏内显示名称，不记录任何其他信息。

**禁止记录：** IP / 平台账号 ID / 硬件指纹 / Mod 自生成持久 ID / 设备信息 / 系统信息 / 地理位置 / 任何可追踪标识

**允许记录：** 游戏内显示名称 / 添加时间 / 备注（玩家手动填写）/ 过期时间（可选）

```text
BlacklistEntry
├── display_name
├── note
├── created_at
└── expires_at
```

**匹配：** 默认精确匹配（区分大小写）；可选忽略大小写 / 去除首尾空格；不启用模糊匹配。

**必须承认：** 同名玩家会被一起屏蔽；改名后无法继续屏蔽；换角色后无法继续屏蔽。

**存储：**

```text
mod/config/blacklist.json
{
  "entries": [
    {
      "display_name": "PlayerName",
      "note": "手动添加",
      "created_at": "2026-01-01T00:00:00Z",
      "expires_at": null
    }
  ]
}
```

**操作：** `/blacklist add <name>` / `/blacklist remove <name>` / `/blacklist list` / `/blacklist clear`

**适用范围：** PvP 自动拦截启用；PvE 不自动拦截，手动屏蔽始终可用。

### 46.3 隐私原则

> 无服务器，无官方账号，无中心化身份验证。
>
> 不收集、不存储、不上传任何玩家隐私数据。
>
> 本地黑名单仅基于游戏内显示名称。
>
> 同名误伤和改名绕过是隐私优先的必然代价，必须明确告知玩家。
>
> 不扫描进程，不读内存，不监控系统。
>
> 不影响正常 PvE 合作体验。


## 47. 接口契约

### 47.1 AI-A1 → AI-A2

```text
接口：
├── PlayerDeathEvent / PlayerRespawnEvent
├── LanternRestEvent / WorldResetEvent
├── EnemyDeathEvent / BossStartEvent / BossDeathEvent
└── PartyWipeEvent

每个接口定义：事件名 / 触发条件 / 数据结构 / Hook 地址 / Signature / 版本限制 / 证据等级
```

### 47.2 AI-A2 → AI-A3 / AI-A4

```text
接口：
├── SessionState / PlayerState
├── CurrentPlayerCount
├── PartyState / BossState
└── 世界状态同步接口
```

### 47.3 AI-C1 → AI-B1 / AI-B4

```text
接口：
├── E2EEChannel
│   ├── encrypt(plaintext) → ciphertext
│   ├── decrypt(ciphertext) → plaintext
│   └── verifyIntegrity(ciphertext, hmac) → bool
├── KeyExchange
│   ├── generateKeyPair() → (publicKey, privateKey)
│   ├── computeSharedSecret(theirPublicKey) → sharedSecret
│   └── deriveSessionKey(sharedSecret) → sessionKey
└── KeyRotation：rotateSessionKey() → newSessionKey
```

### 47.4 AI-C2 → AI-B1 / B2 / B3 / B4

```text
接口：安全约束清单 / 威胁模型 / 隐私保护要求 / 中继安全协议 / 审计要求
```

### 47.5 AI-B3 → AI-B4

```text
接口：
├── RelayReputation
│   ├── getReputation(nodeId) → score
│   ├── reportSuccess(nodeId) / reportFailure(nodeId)
│   └── isEligible(nodeId) → bool
└── 中继节点筛选接口
```

### 47.6 AI-C3 → 所有开发 AI

```text
接口：
├── 审计请求 / 审计结果
│   ├── PASS / PASS_WITH_WARNING / FAIL（一票否决）
└── 审计报告
```


## 48. 分支与代码所有权

```text
agent/a0-platform-integration
agent/a1-reverse-engineering
agent/a2-session-replication
agent/a3-gameplay-systems
agent/a4-balance-pvp
agent/b1-transport
agent/b2-discovery
agent/b3-reputation
agent/b4-relay
agent/c1-cryptography
agent/c2-security-architecture
agent/c3-security-audit
agent/c4-penetration-test
```

**规则：**

- 每个 AI 只修改自己分支内的文件
- 跨分支修改必须通过 AI-00 建立 Issue
- AI-C1 的密码学代码不允许被其他 AI 修改
- AI-C3 的审计代码不允许被开发 AI 修改


## 49. 冲突裁决机制

| 冲突类型 | 裁决方 | 优先级 |
|---|---|---|
| 接口冲突 | AI-00 | 开发优先 |
| 安全 vs 开发 | AI-C3 | **安全优先** |
| 性能 vs 安全 | AI-C2 | **安全优先** |
| 功能 vs 安全 | AI-C3 | **安全优先** |
| 版本兼容冲突 | AI-00 | 兼容优先 |

**核心原则：** 安全永远优先。任何以“性能”或“功能”为由削弱安全的设计，必须建立 Issue，由 AI-C3 裁决。


## 50. 沟通协议

### 每日同步

```text
每个 AI 每日输出：
├── 昨日完成 / 今日计划
├── 阻塞项
└── 需要其他 AI 协助的事项
```

### Issue 驱动

```text
所有跨 AI 协作必须通过 Issue：
├── Issue 标题 / 背景 / 影响范围
├── 涉及 AI / 建议方案
├── 验收标准 / 裁决方
```

### 文档共享

```text
docs/
├── interfaces/          接口契约（AI-00 维护）
├── re/                  逆向文档（AI-A1 维护）
├── security/            安全文档（AI-C2 维护）
├── audit/               审计报告（AI-C3 维护）
├── pentest/             渗透测试报告（AI-C4 维护）
├── integration/         集成报告（AI-00 维护）
└── conflict_log/        冲突记录（AI-00 维护）
```


## 51. Integration / Review 责任

由 **AI-00 总控**兼任 Integration Lead。

必须检查：API/ABI Compatibility / Hook Conflicts / Network Protocol / State Machine / Death/Respawn / Lantern Reset / Boss / PvE/PvP Scaling / Invasion / Save/Reward / Loot/Consumable/Shop/NPC / Session State / Blacklist/Anticheat / Arena / 区域切换 / 独立存档 / 数据持久化 / 安全审计通过。

不得以“所有分支都能编译”为集成完成标准。


## 52. 跨 Agent Issue 规则

必须建立 Issue：AI-A1 无法确认关键函数；AI-A2 需要改变 AI-A1 已确认的核心接口；AI-A3 发现原版机制与假设冲突；大规模修改 Player/World/Boss/Save；修改核心网络协议/死亡经济/Inventory/Reward；PvP Scaling 可能改变核心战斗行为；Crash / State Desync / Save Corruption / Duplicate Reward / Infinite Respawn / Infinite Enemy Reset；消耗品复制；商店购买复制；NPC 保护范围不清；世界状态写入客机存档；反作弊误判；决斗场 Hook 方案未定；区域切换导致状态异常；独立存档损坏；**安全审计未通过**。


## 53. 最终验收

### AI-A0
- [ ] bbport Windows 原生版成功运行
- [ ] Mod 加载机制确认
- [ ] Hook 接口确认
- [ ] 启动器集成完成

### AI-A1
- [ ] bbport runtime 结构已确认
- [ ] 关键 Player/Death/Lantern/World 函数已确认
- [ ] 地址/结构体已记录
- [ ] Hook 有版本保护
- [ ] 原版死亡经济行为已验证
- [ ] 原版 PvP 死亡 Blood Echoes 时机已确认
- [ ] 原版入侵结算规则已确认
- [ ] 原版 Loot/Consumable/Shop/NPC 行为已确认
- [ ] 原版决斗场函数/匹配/奖励/誓约计分已确认
- [ ] 血之同胞失败扣分改为无损失的 Hook 方案已确认
- [ ] 原版区域加载 / 雾门逻辑已确认
- [ ] 原版 Boss 战死亡机制已确认
- [ ] 不确定结论已标记

### AI-A2
- [ ] 2P Session
- [ ] Player / Death / Max HP / Enemy / Boss / Lantern Sync
- [ ] Fast Respawn（从 Lantern）
- [ ] Party Wipe / Reconnect
- [ ] 单人死亡不触发 World Reset
- [ ] 客机世界状态覆盖与恢复
- [ ] 独立存档格式
- [ ] Loot / Consumable / Shop / NPC 独立同步
- [ ] Blood Echoes 归属同步
- [ ] PvP 队伍状态与结算
- [ ] 与 PvE 死亡流程隔离
- [ ] 本地黑名单（仅名称）
- [ ] 协议完整性校验
- [ ] 决斗场独立进入 / 离开 / 重加入 Session
- [ ] 决斗场与 Invasion 互斥
- [ ] 确保房主无法携带客机进入决斗场
- [ ] 确保决斗场失败无损失生效
- [ ] 网络异常处理（500ms 阈值、会话保留、重连令牌）
- [ ] 区域切换同步
- [ ] 数据持久化（实时写入、原子化、崩溃恢复）
- [ ] 非法物品检测
- [ ] Host 退出遣返、Host 死亡继续

### AI-A3
- [ ] Death Penalty 只降低死亡玩家自身 Max HP
- [ ] Death Penalty 在 Lantern 重生后施加，Lantern Rest / 世界重置时消除
- [ ] Death Penalty 与 Enemy/Boss HP 独立
- [ ] Boss 战观战模式
- [ ] Loot 独立发放
- [ ] Consumable 独立库存
- [ ] Shop 个人化
- [ ] NPC 保护
- [ ] Blood Echoes 归属
- [ ] Arena 实现

### AI-A4
- [ ] PvE Scaling 独立
- [ ] PvP Scaling 独立
- [ ] 动态 PvP 平衡系统已实现
- [ ] 队伍综合强度计算已实现
- [ ] 入侵者动态补偿已实现
- [ ] 高等级玩家下修已实现
- [ ] Invader 数量受控
- [ ] PvP 实战数据已记录
- [ ] Reward 无重复
- [ ] 决斗场奖励/誓约计分验证正确
- [ ] 不实现额外防刷保护机制

### AI-B1
- [ ] P2P 直连成功率高
- [ ] 中继回退可靠
- [ ] 连接质量监控
- [ ] 与 AI-C1 E2EE 接口对接

### AI-B2
- [ ] 节点发现可用
- [ ] 信令服务可用
- [ ] 无中心化节点发现

### AI-B3
- [ ] 信誉系统有效
- [ ] 抗 Sybil 攻击
- [ ] 抗 DDoS
- [ ] 中继节点筛选

### AI-B4
- [ ] 中继节点无法解密数据
- [ ] 中继节点不存储任何数据
- [ ] 中继节点可随时退出

### AI-C1
- [ ] E2EE 实现正确
- [ ] 密钥交换安全
- [ ] HMAC 校验有效
- [ ] 防重放有效

### AI-C2
- [ ] 威胁模型完成
- [ ] 安全架构完成
- [ ] 隐私保护方案完成

### AI-C3
- [ ] 所有网络代码通过安全审计
- [ ] 密码学审计通过
- [ ] 隐私审计通过

### AI-C4
- [ ] 渗透测试通过
- [ ] MITM 攻击防御验证
- [ ] Sybil 攻击防御验证
- [ ] DDoS 防御验证


## 54. 最终原则汇总

> Seamless 消除的是联机流程障碍，不是游戏风险。
>
> 原版 Blood Echoes、死亡经济和世界规则应尽可能保留。
>
> 玩家死亡后从 Lantern 重生，但单人死亡不触发 World Reset。
>
> 死亡惩罚在 Lantern 重生后施加，在 Lantern Rest / 世界重置时消除。
>
> Blood Echoes 掉落、回收、敌人持有等原版死亡经济机制保持不变。
>
> Blood Echoes 个人独立，再次死亡只保留最后一次，World Reset 不清除。
>
> 玩家死亡可以快速复活，但死亡玩家自身 Max HP 必须承担高额惩罚。
>
> 单个玩家死亡不能刷新多人世界中的怪物。
>
> Enemy/Boss Scaling 与死亡次数完全独立。
>
> PvE Scaling 与 PvP Scaling 完全独立。
>
> PvP 中单个玩家死亡不是结算点，只有合作队全灭或入侵队全灭才进入结算。
>
> PvP 入侵以公平为目标，通过动态平衡系统抵消围殴与碾压。
>
> 随机掉落独立，固定拾取与 Boss 掉落默认每人一份，唯一物品不得重复。
>
> 每个玩家的消耗品库存独立，使用后只影响使用者自身。
>
> 商店与商人交互按个人处理；Host 可配置 NPC 保护。
>
> 客机加入主机 Session 时临时使用主机世界状态；离开或中断时恢复自己的个人进度。
>
> 区域切换跟随 Host，实时同步，Boss/PvP 期间禁止切换。
>
> 使用独立存档格式，与原版存档完全隔离。
>
> 决斗场按官方原版设计，仅 1v1，独立进入，不携带客机，失败无任何损失。
>
> 反作弊仅限 PvP，PvE 不检测、不踢人、不记录。
>
> 不收集、不存储、不上传任何玩家隐私数据。
>
> 安全设计先行，安全审计独立，安全一票否决。
>
> 所有参数可配置、可测试、可通过实战数据迭代调整。
>
> Build Passed 不等于 Feature Complete。
>
> 必须通过真实运行时与多人实测验证。


**以上为 v3.0 最终版《Bloodborne Seamless Co-op Mod AI 开发任务书》。**
