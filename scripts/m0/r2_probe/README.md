# bbcoop R-2 准备包 — 最小 Hook / 内存读写证明（Windows）

> 面向 M0-R2：在 Human Owner 提供游戏环境后，直接跑通
> `BB_GAME_LIB` 最小探针（加载 DLL → `bbgame_init` → 读 image → 可选 Hook）。
> **本包是盲写产物**：探针 DLL 已在本机做过无游戏自检；loader 补丁、真实游戏运行、
> 版本 pin 的实际校验都还没有实测。所有假设在文件里都标了 `[已确认]` / `[待核实]`。
> 只读来源：本仓库 `docs/references/prior-art-bbport-runtime.md`（REF-007）+
> GitHub API 读到的公开源码（提交号见各文件）。

## 文件索引（先读哪个）

| 顺序 | 文件 | 作用 |
|---|---|---|
| 1 | `README.md` | 本页：索引、状态、待核实、需要 Human Owner 提供什么 |
| 2 | `LOADER-PATCH.md` | 要移植什么：目标时序、Windows 端口候选函数/行号、`install_hook` 要点、版本绑定与 fail-closed |
| 3 | `TEST-PROTOCOL.md` | 环境变量、逐步运行命令、可判定的验收判据、kill criteria、证据格式 |
| 4 | `bbgame.h` | 我们自己重写的 ABI 兼容声明（GPL-2.0-or-later，来源已注明） |
| 5 | `bbgame_probe.c` | 探针 DLL 源码：`bbgame_init`、结构化日志、内存读/写回、skip/replay14 Hook |
| 6 | `smoke_host.c` | 无游戏自检：假 `BbGameHost` + 假 `install_hook`，验证导出/ABI/skip/replay/版本拒绝 |
| 7 | `build.ps1` | 优先仓库内 zig（`tools\toolchain\pylibs\ziglang\zig.exe`）→ PATH zig → gcc；`-SelfTest` 编译并运行 smoke |

本机已生成的验证产物（**不随仓库提交**：仓库禁止未审计二进制，日志含本机路径；
可用 `build.ps1 -SelfTest` 重建）：`bbgame_probe.dll`、`smoke_host.exe`、`build.log`、`smoke_probe.log`。

## 当前状态（截至 2026-10-11，盲写）

- `[已确认]` 本机 zig 0.16.0（仓库自带）可编译探针 DLL；`-SelfTest` 通过：
  导出 `bbgame_init`、ABI `sizeof(BbGameHost)=32`、skip 与 replay14 都工作、
  version=2 被拒绝、日志事件齐全（见 `build.log` / `smoke_probe.log`）。**未加载游戏。**
- `[已确认]` Windows 端口 `@29e59f5818bd` 没有插件点，需按 `LOADER-PATCH.md` 打补丁。
- `[已确认]` 上游 bbport 的 `install_hook` 契约、加载时序、`BB_SKIP_GAME_CHECK` 拒绝行为。
- `[待核实]` 端口实际在 1562-1564 行之间是否保持 image 可写；`LoadLibraryA` 在端口里的行为；
  zig 构建的 DLL 是否被端口 MinGW 环境接受（错误 126/127 的回退已写进协议）。
- `[待核实]` 版本 pin 的 loader 内校验（SHA-256 定义已确认，实现未写）；
  Supermedo fork / `Bloodborne.exe` 仍无一手证据。
- `[待核实]` 真实游戏上的 10 分钟存活、Hook 触发与 replay 行为；以及 guest FS（`wrfsbase`）
  切换后探针 DLL 使用 Windows CRT 写日志的兼容性。

## 需要 Human Owner 提供的具体环境

1. 一台 Windows 10/11 x64、AVX2、Vulkan 1.3 的机器，装有**自备**的
   Bloodborne CUSA03173 v1.09 dump（1.09 update 已合并，`eboot.bin` 可读）。
2. 目标运行时选择：`BetterCodeGoblin/bloodborne_pc_windows_port@29e59f5818bd`
   （推荐，协议按它写）或任务书提到的 Supermedo fork（需要你先确认其来源/版本/是否支持加载原生库）。
3. MSYS2/MinGW64 工具链：运行端口 `setup-windows.ps1`（会安装并 pin 依赖）。
4. 对 GPL-2.0-or-later 义务的决策：插件接口来自 bbport（GPL-2.0-or-later）；
   本包 `bbgame.h` 也按该许可标注。仓库本身没有 LICENSE，需 Human Owner 决定分发路线。
5. 反汇编工具（MSYS2 `objdump` 或 `setup-windows.ps1` 安装的 Zydis），用于 Run B 前
   确认 entry 前 14/15 字节的指令边界。

## 最可能失败的 3 个点

1. **loader 补丁的插入点/时序**：如果插件加载发生在最终 `protect(image, ..., 0)` 之后，
   `install_hook` 写只读页会崩溃；探针会把 `mem.write_back` 标为 `skipped`，必须改用
   `VirtualProtect` 版本或把调用移到 1564 行之前。
2. **DLL/CRT 兼容与导出名**：zig mingw CRT 与端口 MSYS2 MinGW 可能不同，`LoadLibraryA`
   可能返回 126/127；回退是用 `build.ps1 -Compiler gcc -GccPath <端口同一 gcc>` 重编。
3. **replay14 的指令边界/位置相关性**：entry 前 14 字节若跨指令或含 RIP-relative 操作数，
   replay 会崩溃；必须先离线反汇编确认（`REPLAY_LEN` 只能是 14/15），否则退回 `skip`
   （只证明触发，稳定性由 Run A 的 600 秒承担）。

## 证据与合规

- 只采集日志与工具输出：`build.log`、`smoke_probe.log`、探针运行日志、端口 stdout、
  `out\summary.log`、`game_check` 输出。**禁止**截图/录像/游戏文件/存档进入证据
  （REF-007 第 5 节、端口 FORK.md 规则）。
- 探针日志写入 `BB_PROBE_LOG`（默认 `%TEMP%\bbcoop-r2-probe.log`），UTF-8 无 BOM、LF、
  结构化 `event=... key=value` 行，便于 grep 验收。