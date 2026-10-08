# 编译与代码标准（Phase 0 先立闸门）

> **这个文件在写第一行代码之前就存在，是刻意的。**
>
> 零警告现在是免费的：仓库里没有源文件，任何标准都不会立刻产生工作量。
> 等有了几千行代码再补 `/WX`，就要一次性修几百个警告 —— 那时候人就会开始加
> `/wd` 把警告关掉。**技术债就是这样开始的。**
>
> 本文件的每一条都写明**为什么**与**代价**。改之前请先读完。

---

## 1. 工具链现状（实测，不是假设）

### CI（`windows-latest`）

| 工具 | 状态 |
|---|---|
| `cmake` / MSVC `cl.exe` / `msbuild` | 有 |
| `ninja` | 有 |
| `clang-format` / `clang-tidy` | 未配置 |

### 本机（已实测，2026-10）

本机当前用户**不是管理员**，这决定了两条路：

| 工具 | 版本 | 怎么来的 |
|---|---|---|
| `cmake` | 4.4.4 | `pip install --target tools/toolchain/pylibs cmake` |
| `ninja` | 1.13 | 本机 Python313 的 Scripts 目录已有 |
| `zig` | 0.16.0 | `pip install --target … ziglang`（**自带完整 C/C++ 工具链 + libc++**） |
| `clang-format` | 23.1.3 | `pip install --target … clang-format` |
| `clang-tidy` | 22.1.8 | `pip install --target … clang-tidy` |
| **MSVC / VS Build Tools** | **没有** | **需要管理员权限**，装不了 |

一键准备（不需要管理员，全部装进 `tools/toolchain/`，已在 `.gitignore`）：

```powershell
powershell -NoProfile -File tools/toolchain/setup-toolchain.ps1
powershell -NoProfile -File tools/toolchain/setup-toolchain.ps1 -Check   # 只看现状
```

### 本机能编到什么程度（实测结论）

用 `cmake + ninja + zig` 跑通了完整链路：

```text
配置        rc=0    识别为 Clang 21.1.0
compile_commands.json  成功产出（clang-tidy 的前提）
编译        rc=0    .obj 产出，静态库归档成功
clang-format --dry-run --Werror   rc=0（符合 .clang-format）
clang-tidy  rc=1    见下面的已知限制
```

### 为什么本地用 zig 而不是 MSVC：这是有意的取舍

任务书第 84 行说明 bbport 是**原生 x86-64 Windows 进程**，Mod 要注入其中。
因此：

| | MSVC（CI，权威） | zig/clang（本地，辅助） |
|---|---|---|
| ABI | 与游戏/Detours 一致 | **不同**（异常模型与名字修饰不同） |
| 能否链接 MSVC 编译的 `.lib` | 能 | **不能** |
| 用途 | 权威构建门禁（required check） | **冒烟测试**：代码写错了没有 |

**不要把本地 zig 构建通过当成 CI 会通过。** 权威结论只来自 CI 的 MSVC 构建。

### 已知限制（实测，不要当成"应该能用"）

1. **clang-tidy 找不到标准库头文件**。原因：clang-tidy 直接调 clang 并套用
   `compile_commands.json` 里的参数，**不走 zig 驱动**，因此拿不到 zig 内置
   libc++ 的头文件搜索路径。实测报
   `'cstdio' file not found [clang-diagnostic-error]`。
   → 要真正用 clang-tidy，需要一个自带标准库的 clang（例如 LLVM 官方 Windows 包，
   它需要 Windows SDK）**或**等 MSVC 就位。**目前 clang-tidy 在本机不可用。**
2. **`ninja` 的 PyPI 包不含二进制**，只有 Python 包装。真 ninja 要另外装
   （本机是 Python313 的 Scripts 目录里带的）。
3. **两个包装脚本的形式不同，是被 CMake 逼出来的**，不是风格选择：
   - C++ 编译器用 `python + zig-cxx.py`：CMake 在 Ninja 文件里用**单引号**引用编译器
     路径，cmd.exe 不认单引号（实测 `'[zig-cxx.cmd]' is not recognized`）。
   - 归档器用 `zig-ar.cmd`：`CMAKE_AR` **不支持** `exe;arg` 形式，CMake 会把整串
     当一个路径（实测 `can't open file '…\;D:\…\zig-ar.py'`）；而归档步骤走 `cmd /C`，
     `.cmd` 反而正常。
   - `zig-ar.cmd` 还必须处理 ranlib：CMake 会只传归档名调用它，而
     `zig ar libfoo.a` 报 `expected [relpos]`，要补成 `zig ar s libfoo.a`。
4. **`.cmd` 文件必须纯 ASCII**。注释里的制表符/框线字符会被 cmd.exe 当成命令执行
   （实测报 `'──…' is not recognized as an internal or external command`）。

### 如果要装 MSVC（需要管理员）

MSVC 无法用 pip 装，必须用官方安装器，且**需要管理员权限**：

1. 下载 **Visual Studio Build Tools**（不需要完整 IDE）：
   <https://visualstudio.microsoft.com/downloads/> → 「生成工具」
2. 安装时勾选工作负载 **「使用 C++ 的桌面开发」**
   （含 MSVC v143 编译器 + Windows SDK + CMake 集成）
3. 勾选**单个组件**里的 **「适用于 Windows 的 C++ CMake 工具」**（可选，含自己的 CMake）
4. 装完在 **「x64 Native Tools Command Prompt for VS」** 里执行：
   ```bat
   cmake -S . -B build -A x64 -DCMAKE_BUILD_TYPE=Release
   cmake --build build --config Release
   ```
   这个命令行与 CI 的 `build.yml` **完全一致**。

装好后 `tools/preci` 会自动发现 `cmake`，构建检查从「跳过」变成真正编译。

> **注意**：即使装了 MSVC，本仓库 CI 的构建门禁**仍然是**权威判据。
> 本地装它只是为了提前发现问题，不是为了替代 CI。

---

## 2. 编译标准（已在 `CMakeLists.txt` 中落地）

全部集中在一个接口目标 `bbcoop_warnings` 上。**模块不自己决定要不要警告**，
统一 `target_link_libraries(<target> PRIVATE bbcoop_warnings)`。

| 开关 | 作用 | 如果去掉会怎样 |
|---|---|---|
| `/W4` | 打开绝大多数有用警告（`/Wall` 噪音过大，不可用） | 只报最基础的警告，大量问题到运行期才暴露 |
| **`/WX`** | **任何警告都当错误** | `/W4` 退化成建议；警告会累积到没人再看 |
| `/permissive-` | 关闭 MSVC 的宽松解析，向标准靠拢 | 写出在 Clang/GCC 下编不过的代码，且自己不知道 |
| `/utf-8` | 源码与执行字符集都按 UTF-8 | MSVC 按系统代码页（本机 GBK）解释源文件 —— **本仓库已经因此吃过一次亏**（GBK 误码入库） |
| `/external:W0`<br>`/external:anglebrackets` | 把 `<...>` 形式的第三方头文件当外部代码，不报它们的警告 | 第三方头文件会把 `/WX` 顶爆，人只能去关掉整个警告等级 |
| `/guard:cf` `/DYNAMICBASE`<br>`/NXCOMPAT` `/HIGHENTROPYVA` | 链接期加固：控制流保护、ASLR、DEP、64 位高位随机化 | 可能被某次配置改动悄悄关掉而没人注意 |

### 关于 `/external:W0` 的两个注意点

1. **它不管 `/analyze`**。MSVC 的代码分析默认「像分析普通文件一样分析外部头文件」，
   `/external:W0` 对它无效；需要 `/analyze:external-` 或 `CAExcludePath` 环境变量。
2. 用它**不是**为了掩盖自己的问题：不用它，第三方头文件会直接顶爆 `/WX`，
   结果是人去关掉 `/W4` —— 那才是真的掩盖。

### 明确禁止

- **不要大面积 `/wd`** 来让 `/WX` 通过。个别警告确有正当理由时，用
  `#pragma warning(push/pop)` 把它限制在最小范围内，并在注释里写明原因。
- **不要全局 `#define _CRT_SECURE_NO_WARNINGS`**。它会一次性关掉编译器对所有
  不安全 CRT 函数的警告。应该按调用点处理，或改用 `_s` 版本。

---

## 3. 生成器：为什么默认不是 Ninja

`compile_commands.json` 是 clang-tidy 的前提，而**只有 Makefile 与 Ninja 生成器会产出它**，
Visual Studio 生成器不会。

但 CI 的 `.github/workflows/build.yml` 用的是：

```text
cmake -S . -B build -A x64 -DCMAKE_BUILD_TYPE=Release
```

**`-A x64` 只有 Visual Studio 生成器支持，Ninja 不支持。** 所以：

- **默认沿用 Visual Studio 生成器**，CI 不动。
- 需要 clang-tidy 时**显式**用 Ninja，命令写在 `CMakeLists.txt` 与 `.clang-tidy` 里。

这样做的取舍：**不拿已有的构建门禁去换一个尚未用到的静态分析能力。**
等 clang-tidy 真正要接入时，再决定是否把 CI 改成 Ninja（那需要多一步配 MSVC 环境）。

---

## 4. 格式化：只用 diff

有 `.clang-format`，配置与 `.editorconfig` 一致（4 空格缩进、行宽 120）。

**纪律：PR 里只用 `git clang-format --diff <base>` 格式化本次改动的行。**

不要对整棵树跑 `clang-format`。本仓库已有大量手写文档，整树格式化会产生几千行 diff，
把真正的改动淹没，review 就没法做了 —— 这会让人为了"看得清"而跳过格式化，
结果连新代码也不格式化。

---

## 5. 复杂度与规模门槛（写代码前定，而不是事后补）

| 指标 | 门槛 | 依据 |
|---|---|---|
| 函数认知复杂度 | 25 | `.clang-tidy` 的 `readability-function-cognitive-complexity` |
| 单函数行数 | 200 | `.clang-tidy` 的 `readability-function-size` |
| 单文件行数 | 建议 800，硬上限 1200 | 尚未自动化，先作为 review 判据 |
| 嵌套深度 | 建议 ≤ 4 | 尚未自动化 |

**为什么现在定**：门槛在代码存在之后定，就变成了"给现状打折扣"；在之前定，才是标准。
上面两个阈值已经写进 `.clang-tidy`，工具一装就能用。

**建议的独立检查**（需要额外工具，本机未装）：

```text
lizard -C 25 -L 200 <源码目录>      # 复杂度与长度
```

---

## 6. 这些标准如何被强制

| 层 | 机制 | 现在是否生效 |
|---|---|---|
| 配置阶段 | `CMakeLists.txt` 自检 `bbcoop_warnings` 里有 `/W4` 与 `/WX`，缺了就 `FATAL_ERROR` | ✅ 已生效（有源文件后就会跑到） |
| 本地预检 | `tools/preci/check_style.py` 检查 `CMakeLists.txt` 的关键开关是否还在 | ✅ 已生效 |
| CI 构建 | `build.yml` 用 MSVC x64 真编译；有 `/WX` 就等于"警告即失败" | ✅ 已生效（有源文件后就会跑到） |
| 静态分析 | clang-tidy（需 Ninja + `compile_commands.json`） | ⬜ 未接入，工具未装 |
| 格式化 | clang-format | ⬜ 未接入，工具未装 |
| 复杂度 | 阈值已写在 `.clang-tidy`，工具未装 | ⬜ 未接入 |

**为什么"本地没装 cmake"时报跳过而不是通过**：门禁只有在**真的跑了**的时候才算通过。
报"跳过"并计入"未验证"，是因为"本机没有工具"和"代码符合标准"是两回事。
把前者当后者，就是给虚假信心。

---

## 7. C++ 语言层面（随第一个源文件一起落地）

| 项 | 决定 | 理由 |
|---|---|---|
| 标准 | **C++20**，关闭编译器扩展（`CMAKE_CXX_EXTENSIONS=OFF`） | bbport runtime 与 Detours 生态要求 |
| 异常 | 暂定**启用**，但不得跨过 mod 边界抛给游戏 | 未核实：需要先确认 bbport/Hook 边界上的行为，属 Phase 1 待确认项 |
| RTTI | 暂定**启用** | 同上，未核实 |
| 命名 | 类型大驼峰、函数小驼峰、成员 `m_`、常量 `k` | 已写进 `.clang-tidy` |

> ⚠️ 异常与 RTTI 这两项我**没有核实**在 bbport 环境下的实际约束。
> 它们标为待确认，不要当成已定标准使用。

---

## 8. 待确认项（不要当成已定）

1. **异常与 RTTI 在 bbport/Hook 边界的行为** —— 未核实，Phase 1 需要实测。
2. **`/analyze` 是否启用** —— 它比 `/W4` 慢很多，且需要 `/analyze:external-` 才不会
   分析外部头文件。是否值得，要等代码量上来后按实际耗时决定。
3. **clang-format / clang-tidy 的接入方式** —— 是先本地（需各人装工具）还是只放 CI
   （需要 Linux job 或 Windows 上装 LLVM）。未定。
4. **`/W4` 下 MSVC 自身的噪音** —— 已知 `<windows.h>` 相关警告较多，靠
   `/external:W0` 缓解。真实项目上是否还漏，要等编译一次才知道。
   **我没有在本机验证过任何一次编译**（本机无 cmake/MSVC）。
