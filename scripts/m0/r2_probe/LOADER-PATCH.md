# LOADER-PATCH.md — 把 `BB_GAME_LIB` 插件点移植到 Windows 端口

> 状态：**调查 + 设计输入（未应用、未编译、未实测）**。本文件不包含对任何仓库的修改。
> 目标端口（钉住的提交）：`BetterCodeGoblin/bloodborne_pc_windows_port@29e59f5818bd`
> 机制来源（钉住的提交）：`Yharnam-Hunters/bbport@8a2ed0ec9170:src/probe.c` / `src/bbgame.h`
> 证据等级：`[已确认]` = 实际读到该提交的源码；`[待核实]` = 未实测或未能从源码确认。
> 所有行号都指上述两个提交的对应文件。

---

## 0. 一句话结论

Windows 端口当前**完全没有**插件点 `[已确认]`：它的 `src/probe.c` 中没有
`BB_GAME_LIB` / `bbgame_init` / `install_hook`，上游 bbport 甚至在 Windows 分支里显式
`fail("BB_GAME_LIB is not supported on Windows")`
（`Yharnam-Hunters/bbport@8a2ed0ec9170:src/probe.c:312-314`）。
R-2 需要把这一小块（一个头文件 + 约 60 行加载/替换代码）移植进 Windows 端口的
`src/probe.c`，插入点在**重定位与补丁之后、最终页保护与游戏入口之前**。

---

## 1. 目标时序（上游已确认）

上游 bbport 的调用顺序（`src/probe.c`，同一函数 `main`）：

| 顺序 | 上游位置 | 动作 |
|---|---|---|
| 1 | `probe.c:456-489` | 解析 boot 文件的 segments / imports / relocs 并校验 |
| 2 | `probe.c:490-492` | `allocate()` 并整块读入 flat image |
| 3 | `probe.c:520-534` | 应用重定位（`memcpy(image + target, &value, 8)`） |
| 4 | `probe.c:535` | `apply_patches(...)` 应用 BBPATCH2 补丁 |
| 5 | `probe.c:536-537` | 读 `BB_GAME_LIB`，调用 `load_game_library(...)` |
| 6 | `probe.c:538-542` | `protect(thunks...)` / `protect(image, ..., 0)` / 按 segment 恢复页权限 |
| 7 | `probe.c:548-567` | PS4 模块初始化（仅 native libc 路径） |
| 8 | `probe.c:569-575` | 打印 `Entering original x86-64 code at guest offset ...` 并调用 guest entry |

`load_game_library` 内部（`probe.c:311-328`）：
- `dlopen(path, RTLD_NOW | RTLD_LOCAL)`；
- `dlsym(library, BBGAME_INIT_SYMBOL)`；
- 把 `segments/ns/image/size` 填进 `BbGameHost`，`install_hook = install_hook`；
- 调用 `bbgame_init(&host)`；
- `printf("Game library %s: %llu hooks installed, init returned %d\n", ...)`；
- **`if (result) fail("game library init failed");`**（fail closed）。

`install_hook` 本体（`probe.c:297-310`）：
- 只有当 `[offset, offset+size)` 整体落在**同一个 executable segment**（`flags & 1`）内才接受；
- 否则 `size < 14` 或 `target == NULL` → 返回 `-1`；
- 写入 14 字节：`FF 25 00 00 00 00` + 8 字节小端绝对目标地址（`jmp qword ptr [rip+0]`）。

`bbgame.h` 语义（`src/bbgame.h`，本包 `bbgame.h` 是它的重写版）：
`version` = 1；`image` = guest offset 0 的宿主地址；`image_size` = flat image 大小；
`install_hook(offset, size, target)`；`bbgame_init` 返回 0 继续、非 0 停止 loader。
---

## 2. Windows 端口现状（`src/probe.c`，已确认的定位）

| 行号 | 内容 | 与插件点的关系 |
|---|---|---|
| 33-34 | `Segment { address, size, flags }` / `Reloc` | `install_hook` 可执行段判定所需的类型已存在 |
| 37 | `static unsigned char *image;` | 宿主 flat image 基址 |
| 44 | `static uint64_t image_size;` | 与本地 `size` 相同，1499 行赋值 |
| 107-115 | `fail()`：Windows 下 `TerminateProcess(...,1)` | 插件加载失败应该走这里 |
| 144-156 | `protect()`：`VirtualProtect` + `FlushInstructionCache` | 最终页权限；顺序很重要 |
| 1214-1219 | `mapped(segments, ns, address, bytes)` | 可作为插件点的边界校验参考 |
| 1223-1255 | `apply_patches(...)`（与上游同构） | 插件点应**紧跟其后** |
| 1384-1394 | 打开 `argv[1]`、校验 `BBPROBE1..5`、读 `size/entry/ns/nr` | boot 文件格式 |
| 1482-1483 | `image = allocate(round_page(size))`、整块读入 | 此时起 image 可读写 |
| 1485-1498 | GPU/Vulkan 初始化（`bbgpu_init`） | 插件点在**其后**，窗口已创建（与上游顺序一致） |
| 1499 | `image_size = size;` | 插件点可直接用局部 `size` 或全局 `image_size` |
| 1519-1529 | 生成 import thunks | — |
| 1530-1560 | 重定位循环，末尾 `memcpy(image + relocs[i].target, &value, 8)` | 插件点应在其后 |
| **1562** | `if (patch_file) apply_patches(patch_file, segments, ns, relocs, nr);` | **推荐插入点就在这一行之后** |
| 1563 | `protect(thunks, ..., 5)` | — |
| **1564** | `protect(image, round_page(size), 0);` | 插入点必须在其**之前**（否则 image 只读） |
| 1565-1571 | 按 segment 恢复页权限 + `executable_entry` 检查 | 之后装 hook 就需要 `VirtualProtect` |
| 1573-1592 | native libc 模块初始化（会执行 guest 代码） | 插件必须在 1586 行之前完成 |
| 1594-1603 | 打印 Entering、`enter_on_stack(image+entry, ...)` | 游戏代码正式开始执行 |
| 全文件 | `BB_GAME_LIB` / `bbgame_init` / `install_hook` / `LoadLibrary` 命中数 = 0 | 当前**没有插件点** |

构建侧（`gpu/CMakeLists.txt`）：
- `add_executable(bbport ../src/probe.c ../src/win32_compat.c ../src/win32_memory.c ...)`（约 304-313 行）；
- Windows 链接 `ws2_32 dbghelp ntdll winmm`；没有 `bcrypt`（若要加 SHA-256 校验，需要新增）。
- 因此只新增 `src/bbgame.h` + 修改 `src/probe.c` 即可，**不需要改 CMake 源文件列表**；
  `LoadLibraryA`/`GetProcAddress` 来自 kernel32（已隐式链接）。

`build.bat` 产物：`cmake --build out/gpu --target bbgpu bbport bb-gpu-capabilities -- -j 1`，
然后把 `out\bbport.exe` 复制为 `out\bb-probe.exe`，再执行 `python scripts\stage_dlls.py`。
---

## 3. 推荐插入点（设计输入，尚未实施）

在 `src/probe.c` 第 1562 行 `apply_patches(...)` 之后、第 1564 行
`protect(image, round_page(size), 0);` 之前，加入：

```c
    if (patch_file) apply_patches(patch_file, segments, ns, relocs, nr);   /* line 1562, unchanged */

    /* --- NEW: BB_GAME_LIB plug-in point (ported from bbport@8a2ed0ec9170 probe.c:536-537) --- */
    {
        const char *game_lib = getenv("BB_GAME_LIB");
        if (game_lib && *game_lib) bbport_load_game_library(game_lib, segments, ns, size);
    }
    /* --- end NEW --- */

    protect(thunks, round_page((import_count + 1) * 32), 5);               /* line 1563, unchanged */
    protect(image, round_page(size), 0);                                   /* line 1564, unchanged */
```

要点 / 理由：
- **在补丁之后**：`apply_patches` 可能替换函数指针槽；先打补丁再装 hook 不会互相覆盖。
- **在最终 `protect` 之前**：此时 `image` 仍是可写页，`install_hook` 可以直接写 14 字节，
  不需要 `VirtualProtect`（与上游完全一致，上游 `bbport` 的 hook 也是写在最终页保护之前：
  `probe.c:535-542`）。
- **在模块初始化（1586）与 entry（1602）之前**：满足「游戏代码运行前」的语义。
- 备选：插到 1571 行之后（页权限已经恢复），也可以，但 `install_hook` **必须**自己
  `VirtualProtect(PAGE_EXECUTE_READWRITE)` → 写跳转 → 恢复原保护 → `FlushInstructionCache`；
  否则会因只读页而崩溃。`[待核实]`：端口没有现成的「记录原保护」的封装，备选方案需要额外代码。

---

## 4. 需要新增/修改的代码（草案，未编译）

### 4.1 新增 `src/bbgame.h`

直接使用本准备包里的 `bbgame.h`（ABI 兼容重写，GPL-2.0-or-later，来源已注明），
或按同一布局自行重写。关键点：`uint32_t version; unsigned char *image; uint64_t image_size;
int (*install_hook)(uint64_t, uint64_t, const void *);`，x86-64 上 `sizeof == 32`，
`offsetof(image/image_size/install_hook) == 8/16/24`。R-2 探针 DLL 按这个布局编译。

### 4.2 `src/probe.c` 顶部

```c
#include "bbgame.h"      /* 与 runtime.h 一起，放在约第 10 行 */
```
### 4.3 hook 状态与 `install_hook`

在 `apply_patches` 附近（约 1219 行后）新增，逻辑照搬上游 `probe.c:291-310`：

```c
static const Segment *hook_segments = NULL;
static uint64_t hook_segment_count = 0;
static uint64_t hooks_installed = 0;

static int bbport_install_hook(uint64_t offset, uint64_t size, const void *target) {
    int executable = 0;
    for (uint64_t i = 0; i < hook_segment_count; ++i) {
        if ((hook_segments[i].flags & 1) &&
            offset >= hook_segments[i].address &&
            size <= hook_segments[i].size &&
            offset - hook_segments[i].address <= hook_segments[i].size - size)
            executable = 1;
    }
    if (!executable || size < 14 || target == NULL) return -1;

    unsigned char *p = image + offset;
    uint64_t address = (uint64_t)(uintptr_t)target;
    p[0] = 0xff;                 /* jmp qword ptr [rip+0] */
    p[1] = 0x25;
    memset(p + 2, 0, 4);
    memcpy(p + 6, &address, 8);  /* 8-byte absolute target, little-endian */
    FlushInstructionCache(GetCurrentProcess(), p, 16);

    ++hooks_installed;
    return 0;
}
```

- `[已确认]` 判定/编码/失败返回值来自上游 `probe.c:297-310`。
- `[已确认]` 端口 `Segment.flags` 的 bit0 = executable：1577 行附近用 `flags & 1` 判定；
  `protect(..., flags)`（144-156 行）中 bit0=X、bit1=W、bit2=R。
- `[设计输入]` `FlushInstructionCache` 是上游没有的额外保险；x86 上写代码后执行通常已序列化，
  加它无害。
- `[待核实]` 如果 hook 在最终 `protect` 之后安装：需在写之前
  `VirtualProtect(p, 16, PAGE_EXECUTE_READWRITE, &old)`，写完后恢复 `old` 并
  `FlushInstructionCache`；失败返回 -1。当前没有实现。
- `[待核实]` 同一 offset 重复 hook、或 hook 目标位于本进程其他模块（如探针 DLL）时的
  地址空间/ASLR 行为未实测；14 字节跳转本身支持任意 64 位目标。

### 4.4 `bbport_load_game_library`（Win32 版）

在 `install_hook` 后新增，逻辑对应上游 `probe.c:311-328`，`dlopen/dlsym` 换成
`LoadLibraryA/GetProcAddress`：

```c
static void bbport_load_game_library(const char *path, Segment *segments, uint64_t ns, uint64_t size) {
    /* fail closed: hooks/addresses are only valid for the executable game_check.py pins */
    const char *skip = getenv("BB_SKIP_GAME_CHECK");
    if (skip && strcmp(skip, "1") == 0)
        fail("BB_GAME_LIB needs the supported executable; unset BB_SKIP_GAME_CHECK");

    HMODULE library = LoadLibraryA(path);
    if (!library) {
        fprintf(stderr, "LoadLibraryA(%s): error %lu\n", path, (unsigned long)GetLastError());
        fail("cannot load the game library");
    }
    BbGameInit init = (BbGameInit)(void *)GetProcAddress(library, BBGAME_INIT_SYMBOL);
    if (!init) fail("game library has no bbgame_init");

    hook_segments = segments;
    hook_segment_count = ns;
    BbGameHost host = { BBGAME_API_VERSION, image, size, bbport_install_hook };
    int result = init(&host);
    printf("Game library %s: %llu hooks installed, init returned %d\n",
           path, (unsigned long long)hooks_installed, result);
    if (result) fail("game library init failed");
}
```

- `[已确认]`：`BB_SKIP_GAME_CHECK=1` 拒绝加载、缺符号拒绝、init 非 0 拒绝，均照上游
  `probe.c:316-326`；上层 `fail()`（端口 107-115 行）在 Windows 上终止进程并返回 1。
- `[待核实]`：MinGW/zig 生成的 C DLL 导出名。x86-64 Windows 无名字修饰，`__declspec(dllexport)`
  导出 `bbgame_init`；但若用 .def/装饰名或 32 位构建，需要调整 `GetProcAddress`。
  本包的 `build.ps1 -SelfTest` 已在本机用 `GetProcAddress("bbgame_init")` 验证过探针 DLL
  （`[已确认，见 build.log]`），但不能替代对端口自己链接方式的确认。
- `[待核实]`：`LoadLibraryA` 的相对路径按进程当前目录解析；协议要求 `BB_GAME_LIB` 用绝对路径。
- `[待核实]`：探测 DLL 与端口的 CRT 组合（zig mingw CRT vs MSYS2 MinGW）不同可能导致错误
  126/127。协议步骤里给了用 `build.ps1 -Compiler gcc` 重编探针的回退。
### 4.5 版本绑定 / fail-closed 设计

`[已确认]` 钉住的 executable（`scripts/game_check.py`，两个 fork 内容相同）：
- `TITLE_ID = CUSA03173`；`APP_VER = 01.09`；
- `SUPPORTED_IMAGE = 071df19c8880086d97182dbc057bc8cb37badaca57d9112683836b24a0444c0a`；
- 该 SHA-256 的定义：对 `max(p_vaddr + p_memsz)` 大小的 flat image，把各 loadable
  segment 的 `filesz` 字节拷入 `p_vaddr` 位置后计算（`game_check.py:image_sha256`，
  `scripts/prepare.py:126-136` 用同样规则构造写入 boot 文件的 image）。
- `BB_SKIP_GAME_CHECK=1` 会让 `problem()` 直接返回 `None`（跳过检查）；
  `launcher.py` 约 774-793 行在启动前调用 `game_check.problem()`；
  bbport 的 loader 因此在 `probe.c:316-317` 拒绝 `BB_GAME_LIB`。

对 Windows 端口的 fail-closed 建议（按强度递增）：
1. **最低要求（照搬上游）**：`BB_SKIP_GAME_CHECK=1` 时拒绝加载 `BB_GAME_LIB`。已证明上游行为。
2. **推荐（R-2 可先不做）**：在重定位循环（1530 行）**之前**，对
   `image[0..size)` 计算 SHA-256 并与 `SUPPORTED_IMAGE` 比较；不一致就拒绝插件。
   - `[已确认]`：该位置的 image 尚未被重定位/补丁改写，正好对应 `game_check.py` 的哈希定义
     （`prepare.py:209-221` 写入的就是这串字节）。
   - `[待核实]`：用 Windows CNG `BCryptHashData` 实现需要在 `gpu/CMakeLists.txt` 的
     bbport 链接里加 `bcrypt`（当前只有 `ws2_32 dbghelp ntdll winmm`），且未在本机编译验证。
3. **地址不对就拒绝**：任何 hook 的 `offset/size` 越界、非可执行段、`size < 14` →
   `install_hook` 返回 -1；插件必须把 -1 当作致命错误（本包探针会返回非 0 让 loader `fail`）。
   loader 对 `bbgame_init` 任何非 0 返回值都必须 `fail`，不得继续进入游戏代码。

---

## 5. 修改清单（Human Owner / 实施者用）

1. `[ ]` 把本包 `bbgame.h` 放进端口 `src\bbgame.h`（或等价重写）。
2. `[ ]` `src\probe.c` 加 `#include "bbgame.h"`。
3. `[ ]` 加入 4.3 的 `hook_segments/hook_segment_count/hooks_installed/bbport_install_hook`。
4. `[ ]` 加入 4.4 的 `bbport_load_game_library`（Win32 版）。
5. `[ ]` 在 1562 与 1564 行之间插入 4.2 的调用（推荐；备选是插到 1571 行之后 + `VirtualProtect`）。
6. `[ ]` 重新 `build.bat`，确认 `out\bbport.exe` 生成（不需要改 CMake 源列表）。
7. `[ ]` 用 `BB_GAME_LIB=<本包>\bbgame_probe.dll` 跑 `TEST-PROTOCOL.md` 的 Run A/B。
8. `[ ]` 回滚方法：删除新文件/新代码块并重新 `build.bat`；不会影响 boot 文件与存档。

---

## 6. 待核实清单（不要当成已实现）

- `[待核实]` Windows 端口在实际运行中是否确实在 1562-1564 之间保持 `image` 可写（源码顺序支持，
  未实测）。
- `[待核实]` `LoadLibraryA` 从 `probe.c` 调用是否与端口的 VEH、低地址假设、
  以及 `-Wl,--disable-dynamicbase`/`--disable-high-entropy-va` 链接选项冲突。
- `[待核实]` 端口实际使用的 CRT/toolchain 与探针 DLL 的兼容性（错误 126/127 的回退见协议）。
- `[待核实]` 备选插入点（最终页保护之后）的 `VirtualProtect` 恢复原保护实现。
- `[待核实]` loader 内 SHA-256 pin 校验（CNG/bcrypt 链接与调用）。
- `[待核实]` 14 字节覆盖对具体 guest 函数的指令边界影响（R-2 时用反汇编确认；
  探针的 replay14 模式要求操作者先离线确认）。
- `[待核实]` 探针在 guest FS 已被 `wrfsbase` 切到 guest TCB 的线程上写日志的兼容性：
  Windows x64 CRT 走 GS/TEB（端口自身也在同一环境里调用 Win32），理论上安全，但未实测；
  Run B 若在 `hook.triggered` 后立即崩溃，除指令边界外也应排查这一点。
- `[待核实]` Supermedo Windows fork / `Bloodborne.exe` 是否存在及其插件能力（REF-007 第 9 节仍未解决）。

---

## 7. 一手来源

- `Yharnam-Hunters/bbport@8a2ed0ec9170:src/bbgame.h`（ABI；GPL-2.0-or-later）
- `Yharnam-Hunters/bbport@8a2ed0ec9170:src/probe.c`（291-328 插件点；456-575 顺序）
- `Yharnam-Hunters/bbport@8a2ed0ec9170:FORK.md`（本地变更说明：映射/重定位/补丁后、游戏代码前）
- `BetterCodeGoblin/bloodborne_pc_windows_port@29e59f5818bd:src/probe.c`（目标端口，全文）
- `BetterCodeGoblin/bloodborne_pc_windows_port@29e59f5818bd:gpu/CMakeLists.txt`（bbport 目标构成）
- `BetterCodeGoblin/bloodborne_pc_windows_port@29e59f5818bd:scripts/game_check.py`（版本钉住）
- `BetterCodeGoblin/bloodborne_pc_windows_port@29e59f5818bd:scripts/prepare.py`（boot 文件格式）
- `BetterCodeGoblin/bloodborne_pc_windows_port@29e59f5818bd:run.bat` / `build.bat` / `setup-windows.ps1`
- 本仓库 `docs/references/prior-art-bbport-runtime.md`（REF-007）