# bbport / Bloodborne PC 运行时侦察（M0-R1）

> **目的**：在投入实现之前，确认目标运行环境与运行时（bbport）的真实能力 —— 这是 M0 可行性冲刺的第一步。
> **方法**：只读 GitHub 一手来源（源码、文档、仓库元数据）；**本机没有安装游戏或运行时，所有运行期行为都未实测**。
> **证据等级**：Confirmed = 实际读到一手源码/文档；Likely = 多份一手材料交叉；Unverified = 未找到或未核实。
> **来源格式**：`<owner>/<repo>@<提交>:<path>`；本文数据截至 2026-10-11。
> **最后更新**：2026-10

---

## 0. 结论摘要

| # | 结论 | 等级 |
|---|---|---|
| 1 | Bloodborne 的 PC 原生运行时生态**存在且活跃**：上游 `deadinside28/bloodborne_pc`（Linux，GPL-2.0，2328 stars，仍在更新）；另有 Windows 适配端口与用于反编译的 fork | Confirmed |
| 2 | 存在**原生库插件点**：`Yharnam-Hunters/bbport` 在 `src/bbgame.h` 定义 `BB_GAME_LIB` 机制 —— loader 完成映射/重定位/补丁后、**游戏代码运行前**加载库并调用 `bbgame_init`，库可用 `install_hook`（guest offset + 14 字节绝对跳转）替换游戏函数 | Confirmed |
| 3 | **但 vanilla 加载器只支持文件 mod（`dvdroot_ps4`）与 XML 代码补丁，明确不支持 DLL 注入与脚本插件**；插件点是 fork 新增；当前 Windows 端口**没有**这套插件点 | Confirmed |
| 4 | 运行时是 **GPL-2.0(-or-later)**；任务书完全没提许可；本仓库**没有 LICENSE** —— 需要 Human Owner 决策 | Confirmed |
| 5 | 本机没有游戏/端口/构建工具链；**R-2（最小 Hook 证明）必须在一台有自有 CUSA03173 v1.09 dump 的 Windows 机器上进行** | Confirmed |

**一句话**：可行性从"未知"变成了"**有清晰路径，但需要我们自己维护一个运行时 fork 并接受 GPL 义务**"。

## 1. 运行时谱系（截至 2026-10-11）

| 仓库 | 是什么 | 许可 / 活跃度 | 与本项目的关系 |
|---|---|---|---|
| `deadinside28/bloodborne_pc` | **上游**：Linux 原生移植，直接跑 PS4 可执行文件（非 CPU 模拟） | GPL-2.0；2328 stars；HEAD `3c57da41`（2026-10-10） | 任务书说的"deadinside28 的 Linux 原生移植版" |
| `Yharnam-Hunters/bbport` | 上游的公开 fork，作为 `Paleblood` 反编译项目的运行时脚手架 | GPL-2.0-or-later；HEAD `8a2ed0ec`（2026-10-07） | **提供了我们要的 Hook/插件机制** |
| `Yharnam-Hunters/Paleblood` | Bloodborne 反编译（C++ 重写，一个函数一个函数验证） | 80 stars；21 个函数已验证；**还不能运行游戏** | 长期可能的资产，现在不可用 |
| `BetterCodeGoblin/bloodborne_pc_windows_port` | 上游的 **Windows 适配**：Win32 运行时 + Vulkan，120 FPS / 21:9 等 | GPL-2.0；HEAD `29e59f58`（2026-10-09）；"Playable on Windows" | 目前最可见的 Windows 路线；**缺插件点** |
| 任务书提到的 "Supermedo 的 Windows fork（`Bloodborne.exe`）" | 任务书 §1 的目标运行时 | **未在 GitHub 找到** | 需要 Human Owner 核实（Unverified） |

## 2. Hook 机制（Confirmed）

### 2.1 插件接口原文（`Yharnam-Hunters/bbport@8a2ed0ec9170:src/bbgame.h`）

```c
// SPDX-License-Identifier: GPL-2.0-or-later
// The loader maps and relocates the image, applies patches, then loads the library and calls
// its bbgame_init before any game code runs. The library replaces game functions by asking
// the loader to install a jump at their entry. Offsets are guest offsets (ELF p_vaddr).
#define BBGAME_API_VERSION 1u

typedef struct BbGameHost {
    uint32_t version;          /* BBGAME_API_VERSION */
    unsigned char *image;      /* host address of guest offset 0 */
    uint64_t image_size;
    /* Replace the function at `offset`, `size` bytes long, by a jump to `target`.
       Returns 0, or -1 if the function is shorter than the 14-byte jump or not inside
       an executable segment. */
    int (*install_hook)(uint64_t offset, uint64_t size, const void *target);
} BbGameHost;

typedef int (*BbGameInit)(const BbGameHost *host);
#define BBGAME_INIT_SYMBOL "bbgame_init"
```

### 2.2 语义要点（`FORK.md` + `bbgame.h`）

- 加载顺序：**映射 + 重定位 → 应用补丁 → 加载 `BB_GAME_LIB` → 调用 `bbgame_init` → 游戏代码开始执行**；
- `image` 直接给出 guest offset 0 的宿主地址（即游戏镜像基址）；
- `install_hook(offset, size, target)` 是一个**受约束的替换**：目标函数必须 ≥14 字节、必须在可执行段内，否则拒绝；
- 地址只在**被 `scripts/game_check.py` 钉住的**那份可执行文件上有效；`BB_SKIP_GAME_CHECK=1` 时拒绝安装 Hook（版本绑定机制）。

### 2.3 对我们的意义

这正是 `mod.game_hook` 需要的底座：**基址 + 受控 Hook + 在游戏代码运行前获得控制权**。
但它目前在 **Linux fork** 上；要用于任务书指定的 Windows 目标，需要把这一小块机制移植到 Windows 端口（见 §4）。

## 3. 加载器自带的 Mod 能力与限制（Confirmed）

来源：`docs/MODS.md`（两个 fork 内容一致）。

- **文件 mod**：`mods/<名称>/dvdroot_ps4/...`，替换游戏数据文件（纹理/模型/param/界面/音频/字体等），不覆盖原文件，启动时合并成临时树；
- **XML 代码补丁**：`patches/` + `BB_PATCHES`（`patches/Bloodborne.xml` 约 249 KB）；
- **明确不支持**（原文为俄文，关键句）：

  > «Моды с заменой eboot.bin, sce_module, sce_sys, **инжекторы DLL и скриптовые плагины** этим загрузчиком не поддерживаются.»
  >
  > 翻译：替换 `eboot.bin` / `sce_module` / `sce_sys` 的 mod、**DLL 注入器与脚本插件**，这个加载器都不支持；可执行代码补丁走既有的 `patches/` / `BB_PATCHES` 系统。

- 因此：**"往游戏进程里注入我们的 DLL"这条路在 vanilla 加载器下走不通**；要么走插件点（fork 机制），要么只用文件/XML 补丁（对状态同步不够）。

## 4. Windows 端口现状（Confirmed + Unverified）

`BetterCodeGoblin/bloodborne_pc_windows_port@29e59f5818bd`：

- 上游 Linux 移植的 Windows 适配：Win32 内存/同步/线程替换，Vulkan 渲染，FSR 3.1/4；
- README 自述 **"Playable on Windows"**（能启动、读档、音频、手柄、状态持久化）；
- 构建：`setup-windows.ps1`（安装 MSYS2/MinGW64 + 固定依赖）+ `build.bat`；运行 `launch_gui.bat`；
- 需要用户**自备 CUSA03173 v1.09 dump**（`eboot.bin`）；
- **未发现** `src/bbgame.h` / `BB_GAME_LIB`：对 `src/probe.c` 的检索没有命中 `bbgame_init` / `install_hook` / `LoadLibrary`（Unverified：只做了关键字检索，未逐行通读）；
- 结论：**要在 Windows 上拿到插件点，需要我们移植 `BB_GAME_LIB` 那一小块（约一个头文件 + loader 里几十行），或向端口/上游提交**。

任务书陈述里的两处**待核实**：

1. "Supermedo 的 Windows fork（启动器 `Bloodborne.exe`）" —— 未在 GitHub 找到；需要 Human Owner 确认这份 fork 的来源、版本与是否支持加载原生库；
2. 存档路径写作 `...CUSA01363...SPRJ0005`，而游戏版本是 **CUSA03173** —— 编号不一致，需核对（可能是不同区版或笔误）。

## 5. 许可与合规（需要 Human Owner 决策）

- 上游与两个 fork 都是 **GPL-2.0 / GPL-2.0-or-later**；
- **本仓库没有任何 LICENSE**；任务书里也没有许可条款（全文检索无 "GPL / 许可证 / License"）；
- 如果我们的 Mod 需要 fork/修改运行时（很可能会），或者我们的库通过 `BB_GAME_LIB` 加载进 GPL 运行时，**分发很可能必须采用 GPL 兼容许可**；
- 上游规则（`FORK.md`）：不提供、不指向游戏文件；不得在仓库里放游戏画面/录像；必须自有 dump —— 与本仓库既有约束一致，但**"不得放游戏截图/录像"会限制验收证据形式**（只能用日志与工具输出，参考其 `docs/assets/ALLOWLIST` 做法）；
- 这不是法律意见；需要 Human Owner 决定许可证策略（GPL-2.0-or-later、或明确分离与运行时的链接方式）。

## 6. 任务书陈述 vs 本次核实

| 任务书陈述 | 核实结果 |
|---|---|
| bbport 是 deadinside28 Linux 移植的 Windows fork | **部分成立**：Linux 上游确认；GitHub 上可见的 Windows 适配是另一个 fork；任务书点名的 Supermedo fork 未找到（Unverified） |
| 直接运行 PS4 可执行文件、非 CPU 模拟、eboot 转 x86-64 | **成立**（上游与 Windows 端口 README 均如此描述） |
| bbport 提供 Mod 支持 | **成立但有限**：文件 mod + XML 补丁；**不支持 DLL / 脚本插件**（关键差异） |
| 游戏版本 1.09 / CUSA03173 | **成立**（Windows 端口明确要求 CUSA03173 v1.09 dump） |
| 存档路径 `GMsave\Bloodborne\bbport\user\savedata\1\CUSA01363\...` | **待核对**（CUSA01363 与 CUSA03173 不一致） |
| 启动器 `Bloodborne.exe` | **未核实**（GitHub 端口用 `launch_gui.bat` / Python launcher） |

## 7. 对架构的影响

1. **`mod.game_hook` 有了具体落点**：`bbgame_init` + `BbGameHost.image` + `install_hook`；不再是"凭想象注入 DLL"。
2. **需要"运行时 fork 策略"**：选哪个 fork、如何跟踪上游、如何构建可复现、如何分发 —— 这是新的框架级工作，任务书没写。
3. **06 的进程边界不变**：未来的"宿主进程"就是运行时进程；我们的 Kernel/模块要在宿主的插件里跑，辅助进程由我们 spawn（细节留待 R-2/R-3 验证）。
4. **"注入 DLL"路线在 vanilla 下不可用**：必须走插件点（需要对运行时做小改动）或文件/XML 补丁。
5. **GPL 义务**会影响仓库许可证与分发方式。

## 8. R-2 的进入条件（需要 Human Owner 提供）

1. **选定目标运行时**：
   - 方案 A（推荐）：Windows 端口（`BetterCodeGoblin/...`）+ 移植 `BB_GAME_LIB` 补丁；
   - 方案 B：任务书点名的 Supermedo fork（需先定位与验证）；
   - 方案 C：`Yharnam-Hunters/bbport`（Linux）—— 与任务书的 Windows 目标不符。
2. **一台装有自有 CUSA03173 v1.09 dump 的 Windows 机器**（本机没有游戏）。
3. **MSYS2 / MinGW64 工具链**（`setup-windows.ps1` 会安装；本机目前没有）。
4. **R-2 产出**：最小测试库（读 `BbGameHost`、装一个探针 Hook、输出日志）+ 可复现命令 + 日志（不含游戏画面）。
5. **Kill criteria**：两周内给不出"加载成功 + 稳定存活 + 读到一处游戏状态"的可复现证据 → 按 M0 的 kill criteria 重估。

## 9. 未核实

- Supermedo fork 的存在、来源与能力；
- Windows 端口是否有其他扩展点（只做了关键字检索）与是否跟踪上游；
- bbport 是否有反篡改/完整性检查（`game_check.py` 的 executable pin 属于版本绑定，不是反作弊）；
- 存档路径 `CUSA01363` 的来源；
- GPL 的具体法律适用（非法律意见）。

## 10. 来源清单

- `Yharnam-Hunters/bbport@8a2ed0ec9170`：`src/bbgame.h`、`FORK.md`、`docs/MODS.md`、`docs/ROADMAP.md`
- `BetterCodeGoblin/bloodborne_pc_windows_port@29e59f5818bd`：`README.md`、`src/probe.c`（关键字检索）、`docs/MODS.md`、`setup-windows.ps1`
- `deadinside28/bloodborne_pc@3c57da4198ad`：仓库元数据（stars / license / 活跃度）
- `Yharnam-Hunters/Paleblood`：仓库元数据（反编译状态）
