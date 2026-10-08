#!/usr/bin/env powershell
<#
  一键准备本地工具链（不需要管理员）。

  为什么需要这个脚本
  ------------------
  仓库的 CI 用 MSVC 编译，但**本机没有 MSVC 且装它需要管理员权限**。
  结果是"本地无法编译"，本地预检只能报"跳过构建" —— 而跳过不等于通过。

  这个脚本用 pip 把一整套工具装进**项目内的隔离目录** tools/toolchain/：
    cmake        4.4.4    配置与构建
    ninja        1.13     生成器（顺便产出 compile_commands.json）
    zig          0.16     自带完整 C/C++ 工具链（含 libc++），可当本地编译器
    clang-format 23.1     格式化
    clang-tidy   22.1     静态分析

  它**不做**什么
  --------------
  * 不装 MSVC，也不假装 zig 等价于 MSVC。见下面的重要限制。
  * 不修改系统 PATH，不写注册表，不需要管理员权限。
  * 不提交任何东西：tools/toolchain/ 已在 .gitignore 里。

  ⚠️ 重要限制：本地编译器不是 MSVC
  ---------------------------------
  任务书第 84 行说明 bbport 是**原生 x86-64 Windows 进程**，Mod 要注入其中。
  因此：

    * zig/clang 用 C++ ABI 命名规则与异常模型，**与 MSVC 不同**；
    * 如果将来要链接 MSVC 编译的 .lib（例如 Detours），本地 zig 构建会失败；
    * 纯头文件 + Win32 API + 自己写的代码，本地 zig 能编、能跑，适合"冒烟测试"。

  所以正确的用法是：
    **本地用 zig 快速验证"代码写错了没有"；CI 用 MSVC 做权威构建。**
    CI 的构建门禁仍然是 required check，本地这次安装不会削弱它。

.PARAMETER Check
  只检查现状，不安装。

.EXAMPLE
  powershell -NoProfile -File tools/toolchain/setup-toolchain.ps1

.EXAMPLE
  powershell -NoProfile -File tools/toolchain/setup-toolchain.ps1 -Check
#>
[CmdletBinding()]
param(
    [switch]$Check
)

$ErrorActionPreference = 'Stop'
# 本脚本放在 tools/ 下（会被提交），而它安装的工具在 tools/toolchain/（被忽略）。
# 两者刻意分开：脚本必须能被协作者拿到；工具链是每人本地各装一份、绝不入版本库
# （约 540 MB，且含平台相关二进制）。
$script:Here = $PSScriptRoot
$script:Root = (Resolve-Path (Join-Path $Here '..')).Path
$script:Libs = Join-Path $Here 'toolchain\pylibs'

function Find-Python {
    # 优先用 PATH 上的 python；找不到就试几个常见位置。
    foreach ($c in @('python', 'python3', 'py')) {
        $cmd = Get-Command $c -ErrorAction SilentlyContinue
        if ($cmd) { return $cmd.Source }
    }
    foreach ($p in @(
        (Join-Path $env:LOCALAPPDATA 'Programs\Python\Python313\python.exe'),
        (Join-Path $env:LOCALAPPDATA 'Programs\Python\Python312\python.exe'),
        "$env:USERPROFILE\.dsh\dsh-runtimes\dsh-primary-runtime\dependencies\python\python.exe"
    )) {
        if (Test-Path $p) { return $p }
    }
    return $null
}

function Get-ToolPath([string]$ExeName) {
    # 先看 PATH：ninja 就是这种情况 —— PyPI 的 ninja 包只有 Python 包装，
    # 不含二进制，所以真 ninja 来自 PATH（例如 Python 的 Scripts 目录）。
    $onPath = Get-Command $ExeName -ErrorAction SilentlyContinue
    if ($onPath) { return $onPath.Source }
    if (-not (Test-Path $script:Libs)) { return $null }
    $f = Get-ChildItem $script:Libs -Recurse -Filter $ExeName -ErrorAction SilentlyContinue |
         Where-Object { $_.FullName -match '\\data\\bin\\' } |
         Select-Object -First 1
    if ($f) { return $f.FullName }
    $f2 = Get-ChildItem $script:Libs -Recurse -Filter $ExeName -ErrorAction SilentlyContinue |
          Select-Object -First 1
    if ($f2) { return $f2.FullName }
    return $null
}

$py = Find-Python
if (-not $py) {
    Write-Host '找不到 Python。请先安装 Python 3，或把 python 放进 PATH。' -ForegroundColor Red
    exit 1
}

Write-Host ''
Write-Host '  本地工具链准备（不需要管理员）' -ForegroundColor White
Write-Host "  仓库   : $script:Root"
Write-Host "  隔离目录: $script:Libs"
Write-Host "  Python : $py"

$pkgs = @('cmake', 'ninja', 'ziglang', 'clang-format', 'clang-tidy')

if ($Check) {
    Write-Host ''
    Write-Host '  --- 现状 ---' -ForegroundColor Cyan
    foreach ($e in @('cmake.exe', 'ninja.exe', 'zig.exe', 'clang-format.exe', 'clang-tidy.exe')) {
        $p = Get-ToolPath $e
        if ($p) {
            Write-Host ("  [有] {0,-18} {1}" -f $e, $p.Replace($script:Libs, '.')) -ForegroundColor Green
        } else {
            Write-Host ("  [无] {0}" -f $e) -ForegroundColor Yellow
        }
    }
    Write-Host ''
    exit 0
}

Write-Host ''
Write-Host '  --- 安装（可能几分钟，zig 约 170 MB）---' -ForegroundColor Cyan
New-Item -ItemType Directory -Force -Path $script:Libs | Out-Null
foreach ($p in $pkgs) {
    Write-Host ("  安装 {0} ..." -f $p)
    & $py -m pip install --quiet --no-warn-script-location --upgrade `
        --target $script:Libs $p 2>&1 | Where-Object { $_ -notmatch '^\s*$' } | Select-Object -Last 3
    if ($LASTEXITCODE -ne 0) {
        Write-Host ("    !! {0} 安装失败（退出码 {1}）" -f $p, $LASTEXITCODE) -ForegroundColor Yellow
    }
}

Write-Host ''
Write-Host '  --- 安装结果 ---' -ForegroundColor Cyan
$missing = @()
foreach ($e in @('cmake.exe', 'ninja.exe', 'zig.exe', 'clang-format.exe', 'clang-tidy.exe')) {
    $p = Get-ToolPath $e
    if ($p) {
        Write-Host ("  [有] {0,-18} {1}" -f $e, $p.Replace($script:Libs, '.')) -ForegroundColor Green
    } else {
        Write-Host ("  [无] {0}" -f $e) -ForegroundColor Yellow
        $missing += $e
    }
}

Write-Host ''
if ($missing.Count -eq 0) {
    Write-Host '  全部就绪。' -ForegroundColor Green
} else {
    Write-Host ("  缺少: {0}" -f ($missing -join ', ')) -ForegroundColor Yellow
}
Write-Host ''
Write-Host '  下一步：' -ForegroundColor White
Write-Host '    powershell -NoProfile -File tools/preci/preci.ps1'
Write-Host '  预检会自动发现在这套工具，并真正尝试配置与编译。'
Write-Host ''
Write-Host '  ⚠️ 本地编译器是 zig（Clang 前端），不是 MSVC。' -ForegroundColor Yellow
Write-Host '     它能做"代码写错了没有"的冒烟测试，但 ABI 与 MSVC 不同。' -ForegroundColor Yellow
Write-Host '     权威构建仍然在 CI 上用 MSVC 完成（required check）。' -ForegroundColor Yellow
Write-Host '     MSVC 需要管理员权限安装，本脚本装不了。' -ForegroundColor Yellow
Write-Host ''
