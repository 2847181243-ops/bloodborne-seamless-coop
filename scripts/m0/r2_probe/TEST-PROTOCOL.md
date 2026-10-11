# TEST-PROTOCOL.md — M0-R2（最小 Hook / 内存读写证明）运行协议

> 适用对象：Human Owner 在有自备 CUSA03173 v1.09 dump 与可运行 Windows 端口的机器上。
> 配套文件：`bbgame_probe.c` / `bbgame.h` / `build.ps1` / `smoke_host.c` / `LOADER-PATCH.md`。
> 状态：**探针 DLL 已在本机通过无游戏自检（见 build.log / smoke_probe.log）；
> loader 补丁与真实游戏运行均为待执行/待核实。**
> 证据规则：只允许日志与工具输出；**禁止提交/保存游戏文件、存档、截图、录像**（REF-007 第 5 节）。
> 所有命令都在 PowerShell 中执行；`$repo` 指 Windows 端口 checkout，`$pack` 指本准备包目录。

---

## 1. 前置条件（缺一不可）

- Windows 10/11 x64，支持 AVX2 的 CPU，Vulkan 1.3 驱动（端口 README 的 Requirements）。
- 自备的 Bloodborne **CUSA03173 v1.09** 解密 dump，且 1.09 update 已合并进游戏目录
  （`eboot.bin` + `sce_sys/param.sfo`）。
- 目标端口已按 `setup-windows.ps1` 安装 MSYS2/MinGW64 + 依赖；
  `build.bat` 能产出 `out\bbport.exe`。
- **已应用 `LOADER-PATCH.md` 的插件点并重新构建**（本准备包不包含对端口的修改）。
- Python 3.10+（端口脚本需要）；本仓库自带 zig 工具链（`tools\toolchain\pylibs\ziglang\zig.exe`）
  用于构建探针。
- 明确：**不要**设置 `BB_SKIP_GAME_CHECK=1`。该变量会绕过版本校验，按上游设计会导致
  loader 拒绝 `BB_GAME_LIB`。

---

## 2. 环境变量（探针读取）

| 变量 | 必填 | 含义 / 默认 |
|---|---|---|
| `BB_GAME_LIB` | 是（Run A/B） | 探针 DLL 的**绝对路径**，如 `$pack\bbgame_probe.dll` |
| `BB_PROBE_LOG` | 建议 | 结构化日志路径；默认 `%TEMP%\bbcoop-r2-probe.log` |
| `BB_PROBE_STDERR` | 否 | `1` = 日志同时镜像到 stderr |
| `BB_PROBE_WRITE_TEST` | 否 | `0` = 关闭 8 字节同值写回测试；默认开启 |
| `BB_PROBE_WRITE_OFFSET` | 否 | 写回测试 offset，默认 `0` |
| `BB_PROBE_HOOK_OFFSET` | Hook 时 | guest offset（ELF p_vaddr），不是宿主指针；不设置 = 不装 Hook |
| `BB_PROBE_HOOK_SIZE` | 否 | 传给 `install_hook` 的函数长度，默认 `14`（必须 >= 14） |
| `BB_PROBE_HOOK_MODE` | 否 | `skip`（默认）或 `replay14` |
| `BB_PROBE_HOOK_REPLAY_LEN` | replay14 时 | `14` 或 `15`，默认 `14`；见第 6 步 |
| `BB_PROBE_HOOK_ACK` | replay14 时 | 必须正好是 `replay14`，否则探针拒绝安装 |
| `BB_TIMEOUT` | 运行控制 | `run.bat` 把它变成 `--timeout <秒>`；`600` = 10 分钟后干净退出并打印 summary |
| `BB_SKIP_GAME_CHECK` | 否 | **必须未设置/不为 1** |

探针日志格式：每行 `ts=<UTC ISO8601> seq=<n> pid=<n> tid=<n> event=<name> key=value ...`，
LF 换行、UTF-8 无 BOM；关键事件见第 7 步验收判据。
---

## 3. 步骤 0 — 无游戏自检（任何一台 Windows 机器）

先确认探针 DLL 能编译、能导出 `bbgame_init`、ABI 大小正确、skip/replay 两种 hook 模式
与版本拒绝路径都能工作：

```powershell
$pack = "$env:TEMP\bbcoop-r2-pack"
New-Item -ItemType Directory -Force "$pack\evidence" | Out-Null
powershell -NoProfile -ExecutionPolicy Bypass -File "$pack\build.ps1" -SelfTest 2>&1 |
    Tee-Object "$pack\evidence\step0-build-selftest.log"
```

期望：控制台/日志出现 `BUILD OK`、`SMOKE PASS`；产物为 `$pack\bbgame_probe.dll`
与 `$pack\smoke_host.exe`。若失败，先解决工具链问题，**不要**进入游戏步骤。

---

## 4. 步骤 1 — 版本绑定检查（必须通过）

`BB_SKIP_GAME_CHECK` 必须未设置：

```powershell
$repo = 'C:\bbport'                              # <-- 改成端口 checkout 路径
$game = 'C:\Games\Bloodborne\CUSA03173'          # <-- 改成自己的 dump 路径
$pack = "$env:TEMP\bbcoop-r2-pack"
Remove-Item Env:BB_SKIP_GAME_CHECK -ErrorAction SilentlyContinue
Push-Location "$repo\scripts"
python -c "import game_check, sys; p = game_check.problem(r'$game'); print('problem=', p); sys.exit(0 if p is None else 1)" 2>&1 |
    Tee-Object "$pack\evidence\step1-game_check.log"
$checkExit = $LASTEXITCODE
Pop-Location
if ($checkExit -ne 0) { throw 'game_check failed: dump is not CUSA03173 v1.09 (or unreadable)' }
```

期望：`problem= None`。这一步钉住的 executable 哈希是
`071df19c8880086d97182dbc057bc8cb37badaca57d9112683836b24a0444c0a`
（来源：`scripts/game_check.py`，两个 fork 相同）。任何其他结果都**不要继续**。

---

## 5. 步骤 2 — 构建端口（未打补丁的基线）

```powershell
Push-Location $repo
powershell -ExecutionPolicy Bypass -File .\setup-windows.ps1   # 仅第一次
cmd /c build.bat
Pop-Location
```

期望产物：`$repo\out\bbport.exe`（`build.bat` 还会复制成 `out\bb-probe.exe`）。
把 `build.bat` 的输出保存到 `evidence\step2-build-port.log`（例如用
`cmd /c build.bat 2>&1 | Tee-Object ...`）。端口构建需要 MinGW64/CMake/Ninja/SDL3/Vulkan；
缺依赖时 `setup-windows.ps1` 会给出明确错误。

---

## 6. 步骤 3 — 生成 boot 产物

`run.bat` 每次启动前都会重新生成产物；用 `BB_TIMEOUT=5` 做一次短启动即可（此时
`BB_GAME_LIB` 必须未设置）：

```powershell
Remove-Item Env:BB_GAME_LIB,Env:BB_PROBE_LOG,Env:BB_PROBE_HOOK_OFFSET -ErrorAction SilentlyContinue
$env:BB_TIMEOUT = '5'
& "$repo\run.bat" $game 2>&1 | Tee-Object "$pack\evidence\step3-prepare-5s.stdout.log"
```

期望产物（`$repo\out\`）：`boot-linked.bin`、`content.bin`、`patches.bin`、`analysis.json`。
`analysis.json` 里 `entry` 字段就是 guest entry offset（`0x...`），Run B 会用到。
（也可以直接依次运行 `run.bat` 中列出的 `scripts\mods.py`、`prepare.py`、`link_libc.py`、
`link_modules.py`、`content_profile.py`、`patches.py`，不启动游戏；命令见 `run.bat` 原文。）

---

## 7. 步骤 4 — 应用 loader 补丁并重建（待定位/待实施）

按 `LOADER-PATCH.md` 第 3-5 节：新增 `src\bbgame.h`，在 `src\probe.c` 约 1562 行之后、
1564 行之前加入 `BB_GAME_LIB` 加载调用，`install_hook` 与 Win32 `LoadLibraryA` 版本
照第 4.3/4.4 节。然后重新构建：

```powershell
Push-Location $repo
cmd /c build.bat 2>&1 | Tee-Object "$pack\evidence\step4-build-patched-port.log"
Pop-Location
Get-Item "$repo\out\bbport.exe" | Select-Object FullName,Length,LastWriteTime
```

`[待核实]`：本准备包不含补丁；如果实施者选择在最终页保护之后插入，`install_hook` 必须
加 `VirtualProtect`，并注意探针的 `mem.write_back` 会记录
`skipped=1 reason=page_not_writable`（此时写证明由 `hook.install result=0` 提供）。
---

## 8. 步骤 5 — Run A（不装 Hook）：初始化 + 内存读写 + 10 分钟存活

```powershell
$pack = "$env:TEMP\bbcoop-r2-pack"
$repo = 'C:\bbport'
$game = 'C:\Games\Bloodborne\CUSA03173'
Remove-Item Env:BB_PROBE_HOOK_OFFSET,Env:BB_PROBE_HOOK_SIZE,Env:BB_PROBE_HOOK_MODE,
    Env:BB_PROBE_HOOK_ACK,Env:BB_PROBE_HOOK_REPLAY_LEN -ErrorAction SilentlyContinue
$env:BB_GAME_LIB   = "$pack\bbgame_probe.dll"
$env:BB_PROBE_LOG  = "$pack\evidence\runA-no-hook.log"
$env:BB_PROBE_WRITE_TEST = '1'
$env:BB_TIMEOUT    = '600'
$t0 = Get-Date
& "$repo\run.bat" $game 2>&1 | Tee-Object "$pack\evidence\runA-no-hook.stdout.log"
$runAExit = $LASTEXITCODE
$elapsedA = [int]((Get-Date) - $t0).TotalSeconds
"RunA exit=$runAExit elapsed=${elapsedA}s log=$env:BB_PROBE_LOG"
```

Run A 期望：
- 探针日志有 `event=bbgame_init.enter`、`event=host.version value=1`、
  `event=host.image value=`、`event=host.image_size value=`、
  `event=mem.read`、`event=mem.write_back ... result=ok`、
  `event=hook.not_requested value=0`、`event=bbgame_init.exit result=0`。
- stdout 有 `[TIMEOUT] Reached timeout of 600 seconds`；`$repo\out\summary.log`
  的 `Total Runtime:` ≥ 600；`$elapsedA` ≥ 600。

---

## 9. 步骤 6 — Run B（装 Hook）：先离线验证 entry 字节，再在 entry 上 replay

Hook 目标取 `analysis.json` 的 `entry`：游戏第一条 guest 指令必然被执行，
因此 `hook.triggered` 的时机是确定的。但 14 字节覆盖必须**离线确认**指令边界；
`replay14` 只有在边界与位置无关性成立时才不改变行为。

### 9.1 读取 entry 并反汇编窗口

```powershell
$analysis = Get-Content -Raw "$repo\out\analysis.json" | ConvertFrom-Json
$entry = $analysis.entry                     # 形如 "0x..."
$entry
$start = '0x{0:x}' -f [Convert]::ToInt64($entry, 16)
$stop  = '0x{0:x}' -f ([Convert]::ToInt64($entry, 16) + 32)
$objdump = 'C:\msys64\mingw64\bin\objdump.exe'   # [待核实] 路径；也可用 Zydis
if (Test-Path $objdump) {
    & $objdump -d -M intel --start-address=$start --stop-address=$stop "$repo\out\eboot.elf" 2>&1 |
        Tee-Object "$pack\evidence\step6-entry-disasm.log"
} else {
    'objdump not found; use Zydis (installed by setup-windows.ps1) to disassemble the same window' |
        Tee-Object "$pack\evidence\step6-entry-disasm.log"
}
```

判读规则（`Zydis` 或 `objdump` 输出）：
- 若第 14 字节处正好是下一条指令边界 → `BB_PROBE_HOOK_REPLAY_LEN=14`。
- 若跨过 14 的那条指令在 15 字节处结束 → `BB_PROBE_HOOK_REPLAY_LEN=15`。
  （探针只接受 14/15；其他值会写出 `hook.fail reason=replay_len_must_be_14_or_15` 并拒绝。）
- 窗口内若出现 **RIP-relative 操作数** 或 **指向窗口内部的分支**，`replay14` 不安全：
  改用 `skip` 模式 + 一个操作者确认无害的 guest offset（仅证明触发），
  稳定性由 Run A 承担；并在证据里记录原因。
- `[待核实]`：`out\eboot.elf` 是 prepare.py 重建的 ELF，不是字节精确重建，
  但代码段按 vaddr 还原；反汇编前先用 `analysis.json.entry` 与 `out\entry.bin`
  交叉检查第一条指令。

### 9.2 Run B（replay14，推荐先 120 秒快速触发）

```powershell
$pack = "$env:TEMP\bbcoop-r2-pack"
$repo = 'C:\bbport'
$game = 'C:\Games\Bloodborne\CUSA03173'
$entry = (Get-Content -Raw "$repo\out\analysis.json" | ConvertFrom-Json).entry

$env:BB_GAME_LIB            = "$pack\bbgame_probe.dll"
$env:BB_PROBE_LOG           = "$pack\evidence\runB-entry-replay.log"
$env:BB_PROBE_WRITE_TEST    = '1'
$env:BB_PROBE_HOOK_OFFSET   = $entry
$env:BB_PROBE_HOOK_SIZE     = '15'
$env:BB_PROBE_HOOK_MODE     = 'replay14'
$env:BB_PROBE_HOOK_ACK      = 'replay14'
$env:BB_PROBE_HOOK_REPLAY_LEN = '14'   # 9.1 判定为 15 时改成 15
$env:BB_TIMEOUT             = '120'    # 快速触发检查；通过后可改成 600 做合并稳定性证据

$t0 = Get-Date
& "$repo\run.bat" $game 2>&1 | Tee-Object "$pack\evidence\runB-entry-replay.stdout.log"
$runBExit = $LASTEXITCODE
$elapsedB = [int]((Get-Date) - $t0).TotalSeconds
"RunB exit=$runBExit elapsed=${elapsedB}s log=$env:BB_PROBE_LOG"
```

Run B 期望：
- `event=hook.trampoline`、`event=hook.install result=0`、`event=hook.triggered count=1`；
- 若 replay 正确：stdout 在 120 秒后出现 `[TIMEOUT] Reached timeout of 120 seconds`，
  `event=bbgame_init.exit result=0`，没有崩溃。
- 若进程在 `hook.triggered` 后立即死掉：触发证据仍然有效，但**稳定性证据不成立**；
  先试 `REPLAY_LEN=15`，再试另一处 offset 或 `skip` 模式，并记录失败日志。

### 9.3 skip 模式回退（仅触发证明）

无法离线确认指令边界时：

```powershell
$env:BB_PROBE_HOOK_OFFSET = '0x<操作者选定的 guest offset>'
$env:BB_PROBE_HOOK_SIZE   = '14'
$env:BB_PROBE_HOOK_MODE   = 'skip'
Remove-Item Env:BB_PROBE_HOOK_ACK,Env:BB_PROBE_HOOK_REPLAY_LEN -ErrorAction SilentlyContinue
$env:BB_TIMEOUT           = '120'
& "$repo\run.bat" $game 2>&1 | Tee-Object "$pack\evidence\runB-skip.stdout.log"
```

`skip` 会**改变被 hook 函数的行为**（桩函数记录后返回 0），必须选择可牺牲的函数；
稳定性不接受 skip 运行的结果。
---

## 10. 验收判据（必须可判定）

在每次运行结束后，先把端口 summary 拷成运行专属文件（`out\summary.log` 会被下一次运行覆盖）：

```powershell
Copy-Item "$repo\out\summary.log" "$pack\evidence\runA-summary.log" -Force   # Run A 之后
Copy-Item "$repo\out\summary.log" "$pack\evidence\runB-summary.log" -Force   # Run B 之后
```

用下面的 `Need` 逐条断言（全部通过才算 R-2 通过）：

```powershell
function Need([string]$file, [string]$text) {
    if (Select-String -Path $file -SimpleMatch $text -Quiet) { "OK  $text" }
    else { throw "MISSING: '$text' in $file" }
}
$A = "$pack\evidence\runA-no-hook.log"
$B = "$pack\evidence\runB-entry-replay.log"
```

| # | 判据 | 断言 |
|---|---|---|
| A1 | 插件被加载、`bbgame_init` 被调用 | `Need $A 'event=bbgame_init.enter'`；`Need $A 'event=bbgame_init.exit result=0'` |
| A2 | host 字段合法 | `Need $A 'event=host.version value=1'`；`Need $A 'event=host.image value='`；`Need $A 'event=host.image_size value='`；并核对 `image_size` 与 `out\analysis.json` 的 `image_bytes` 相等（脚本检查见下） |
| A3 | 无初始化失败 | `if (Select-String -Path $A -SimpleMatch 'event=bbgame_init.fail' -Quiet) { throw 'bbgame_init failed' }` |
| A4 | 进程稳定存活 ≥ 10 分钟 | Run A stdout 有 `[TIMEOUT] Reached timeout of 600 seconds`；`runA-summary.log` 的 `Total Runtime:` ≥ 600；且 `runAExit -eq 0` |
| A5 | 可选 Hook 安装且至少触发一次 | `Need $B 'event=hook.install result=0'`；`Need $B 'event=hook.triggered count=1'`；replay 模式还要求 `Need $B 'event=hook.trampoline'` 且 Run B 达到自己的 timeout（120/600），即 replay 后没有立即崩溃 |
| A6 | 内存写证明（二选一） | Run A 有 `event=mem.write_back offset=0x0 size=8 result=ok`；或（插件晚于最终页保护时）Run B 有 `event=hook.install result=0`（loader 写入了 14 字节跳转） |
| A7 | 没有 loader 级错误 | 两个 stdout 日志都不含 `ERROR:`（`[WATCHDOG]` 停顿告警不算；但如果进程提前退出，A4/A5 已经失败） |

Run A 的 `image_size` 与 `analysis.json` 对照：

```powershell
$analysis = Get-Content -Raw "$repo\out\analysis.json" | ConvertFrom-Json
$m = Select-String -Path $A -SimpleMatch 'event=host.image_size value=' | Select-Object -First 1
$logged = [uint64]($m.Line -replace '.*value=','')
if ($logged -ne [uint64]$analysis.image_bytes) { throw "image_size mismatch: log=$logged analysis=$($analysis.image_bytes)" }
"OK image_size=$logged (analysis.json image_bytes)"
```

**最强证据形式**：把 Run B 的 `BB_TIMEOUT=600`，若 replay 模式同时满足 A1-A7，
则一条日志同时覆盖初始化、写、Hook 触发与 10 分钟存活；Run A（无 Hook 的 600 秒）
仍然建议保留作为对照。

---

## 11. Kill criteria 与升级条件

- **两周 kill criteria**：从 Human Owner 确认「有自备 CUSA03173 v1.09 dump 的 Windows 机器 +
  端口可构建」之日起 14 个自然日内，拿不出 A1-A5 的可复现证据（日志 + 命令 + 工具输出）
  → 触发 M0 重估（对齐 REF-007 第 8 节）。
- **立即升级**（不必等两周）：
  - 打了 loader 补丁后日志仍无 `event=bbgame_init.enter`；
  - 对 plausible 的 guest offset，`hook.install.fail result=-1` 反复出现；
  - `bbgame_init` 在 image 上报告不可读/不可写且无法用备选插入点解决；
  - `BB_GAME_LIB` 被 loader 以 `BB_SKIP_GAME_CHECK` 或版本原因拒绝，而 Human Owner
    无法提供钉住版本的 dump。

---

## 12. 故障排查

| 症状 | 可能原因 / 处理 |
|---|---|
| 日志没有 `bbgame_init.enter`，进程正常跑游戏 | loader 补丁没应用、没重建，或 `BB_GAME_LIB` 路径不对/未继承。用绝对路径，确认 `out\bbport.exe` 的 LastWriteTime 在打补丁之后 |
| `LoadLibraryA ... error 126/127` | 探针 DLL 与端口 CRT/架构不匹配。在端口同一 MinGW64 下重编：`build.ps1 -Compiler gcc -GccPath C:\msys64\mingw64\bin\gcc.exe` |
| `event=hook.install.fail result=-1` | offset 不在可执行段、`size<14`、target 为空。确认 offset 是 guest p_vaddr（不是宿主指针），function 长度给 `14` 或真实长度；查 `analysis.json` / 反汇编 |
| `hook.triggered` 一直不出现 | offset 不是会被执行的函数入口；用 `analysis.json.entry` + `replay14` 做确定性触发 |
| `mem.write_back skipped=1 reason=page_not_writable` | 插件加载发生在最终 `protect` 之后；如 loader 的 `install_hook` 用了 `VirtualProtect` 则仍可用 A6 的备选；否则把插入点移到 1564 行之前 |
| `bbgame_init.fail result=12 reason=version_mismatch` | 端口里的 `bbgame.h` 与探针的 ABI 版本号不一致；重新核对头文件 |
| `image_size` 与 `analysis.json` 不一致 | boot 文件来自另一份 dump/版本，或 Run 之间 `out\` 被覆盖；重新执行步骤 1-3 |
| stdout 出现 `[WATCHDOG]` | 帧停顿快照，不是失败；只有 `ERROR:` 或提前退出影响验收 |
| Run B replay 后立即崩溃 | 9.1 的边界/位置无关性判断不成立。改 `REPLAY_LEN=15`、换 offset，或退回 `skip`（仅触发证据） |

---

## 13. 证据清单（只含日志/工具输出）

建议目录：`$pack\evidence\`

```
step0-build-selftest.log      # build.ps1 -SelfTest 输出（含 SMOKE PASS）
build.log                     # 编译器、命令、退出码
smoke_probe.log               # 无游戏 smoke 的结构化探针日志
step1-game_check.log          # game_check 输出（problem= None）
step2-build-port.log          # 未打补丁的端口构建
step3-prepare-5s.stdout.log   # 产物准备
step4-build-patched-port.log  # 打补丁后的端口构建
step6-entry-disasm.log        # entry 前 32 字节反汇编与边界判断
runA-no-hook.log              # Run A 探针日志（10 分钟，无 Hook）
runA-no-hook.stdout.log       # Run A 端口 stdout（含 [TIMEOUT]）
runA-summary.log              # Run A 的 out\summary.log 副本
runB-entry-replay.log         # Run B 探针日志（Hook 触发）
runB-entry-replay.stdout.log  # Run B 端口 stdout
runB-summary.log              # Run B 的 out\summary.log 副本
```

**禁止**把 `out\eboot.elf`、`out\boot*.bin`、`out\entry.bin`、游戏目录内容、
截图或录像放进证据（含游戏代码/画面）；`analysis.json` 只是工具元数据，可按需保留。
