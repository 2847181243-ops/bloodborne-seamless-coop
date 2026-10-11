<#

.SYNOPSIS

    Builds bbgame_probe.dll (and optionally runs the game-free smoke test).



.DESCRIPTION

    Tool order (per M0-R2 requirement):

      1. repo-bundled zig at <RepoRoot>\tools\toolchain\pylibs\ziglang\zig.exe

         (this is the compiler zig-cxx.cmd wraps; zig-cxx.cmd itself only runs

         "zig c++", so for C we call zig.exe cc directly);

      2. zig.exe found on PATH;

      3. gcc on PATH, or a known MinGW-w64 location (MINGW_DIR / W64DEVKIT_DIR /

         C:\msys64\mingw64\bin\gcc.exe).

    Every failure prints the exact reason and exits non-zero. Nothing outside the

    pack folder is written. The repo is only read (toolchain discovery).



.PARAMETER RepoRoot

    Path to the bloodborne-seamless-coop checkout that contains tools\toolchain.

    Default: D:\bloodborne-seamless-coop (override with BBCOOP_REPO or this param).



.PARAMETER Compiler

    auto (default) | zig | gcc.



.PARAMETER GccPath

    Explicit gcc.exe to use with -Compiler gcc.



.PARAMETER SelfTest

    Also build smoke_host.exe and run it against the DLL (fake BbGameHost, no game).



.EXAMPLE

    powershell -ExecutionPolicy Bypass -File build.ps1 -SelfTest

#>

[CmdletBinding()]

param(

    [string]$RepoRoot = $(if ($env:BBCOOP_REPO) { $env:BBCOOP_REPO } else { 'D:\bloodborne-seamless-coop' }),

    [ValidateSet('auto','zig','gcc')][string]$Compiler = 'auto',

    [string]$GccPath = '',

    [string]$OutputDir = '',

    [switch]$SelfTest

)



$ErrorActionPreference = 'Stop'

$PackDir = $PSScriptRoot

if (-not $PackDir) { $PackDir = Split-Path -Parent $MyInvocation.MyCommand.Path }

if (-not (Test-Path -LiteralPath $PackDir)) { throw "pack directory not found: $PackDir" }

if (-not $OutputDir) { $OutputDir = $PackDir }

$OutputDir = [System.IO.Path]::GetFullPath($OutputDir)

if (-not (Test-Path -LiteralPath $OutputDir)) { New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null }



$BuildLog = Join-Path $OutputDir 'build.log'

$Utf8NoBom = New-Object System.Text.UTF8Encoding($false)

[System.IO.File]::WriteAllText($BuildLog, '', $Utf8NoBom)   # start fresh



function Log([string]$message) {

    $line = "[{0}] {1}" -f (Get-Date -Format 'yyyy-MM-ddTHH:mm:ssK'), $message

    [System.IO.File]::AppendAllText($BuildLog, $line + "`n", $Utf8NoBom)

    Write-Host $line

}



function Fail([string]$reason) {

    Log "FATAL: $reason"

    Write-Host "FATAL: $reason" -ForegroundColor Red

    exit 2

}



function Invoke-Tool {

    param([string]$Exe, [string[]]$Arguments, [string]$What)

    $capture = Join-Path $env:TEMP ("bbcoop-build-" + [guid]::NewGuid().ToString('N') + '.log')

    Log ("RUN {0}: {1} {2}" -f $What, $Exe, ($Arguments -join ' '))

    $oldEap = $ErrorActionPreference

    $ErrorActionPreference = 'Continue'   # native stderr must not throw before we read $LASTEXITCODE

    & $Exe @Arguments *> $capture

    $code = $LASTEXITCODE

    $ErrorActionPreference = $oldEap

    if (Test-Path -LiteralPath $capture) {

        $body = Get-Content -Raw -LiteralPath $capture -ErrorAction SilentlyContinue

        if ($body) { [System.IO.File]::AppendAllText($BuildLog, $body, $Utf8NoBom); Write-Host $body.TrimEnd() }

        Remove-Item -LiteralPath $capture -Force -ErrorAction SilentlyContinue

    }

    Log ("EXIT {0}: {1}" -f $What, $code)

    return $code

}



Log "pack=$PackDir repoRoot=$RepoRoot output=$OutputDir compiler=$Compiler pid=$PID"



# ---------------------------------------------------------------- tool discovery

$zigCandidates = @()

if ($RepoRoot) { $zigCandidates += (Join-Path $RepoRoot 'tools\toolchain\pylibs\ziglang\zig.exe') }

$zig = $zigCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1

if (-not $zig) {

    $cmd = Get-Command zig -ErrorAction SilentlyContinue

    if ($cmd) { $zig = $cmd.Source }

}

$zigOrigin = if ($zig -and $zigCandidates -contains $zig) {

    'repo-bundled tools\toolchain\pylibs\ziglang'

} else { 'PATH' }



$gccCandidates = @()

if ($GccPath) { $gccCandidates += $GccPath }

if ($env:MINGW_DIR)    { $gccCandidates += (Join-Path $env:MINGW_DIR 'bin\gcc.exe') }

if ($env:W64DEVKIT_DIR){ $gccCandidates += (Join-Path $env:W64DEVKIT_DIR 'bin\gcc.exe') }

$gccCandidates += 'C:\msys64\mingw64\bin\gcc.exe'

$gcc = $gccCandidates | Where-Object { $_ -and (Test-Path -LiteralPath $_) } | Select-Object -First 1

if (-not $gcc) {

    $cmd = Get-Command gcc -ErrorAction SilentlyContinue

    if ($cmd) { $gcc = $cmd.Source }

}



$chosen = $null

if ($Compiler -eq 'zig') {

    if (-not $zig) {

        Fail ("Compiler=zig requested but zig.exe was not found " +

              "(repo tools\toolchain\pylibs\ziglang\zig.exe or PATH).")

    }

    $chosen = 'zig'

} elseif ($Compiler -eq 'gcc') {

    if (-not $gcc) {

        Fail ("Compiler=gcc requested but gcc.exe was not found " +

              "(PATH, MINGW_DIR, W64DEVKIT_DIR, C:\msys64\mingw64).")

    }

    $chosen = 'gcc'

} else {

    if ($zig) { $chosen = 'zig' }

    elseif ($gcc) { $chosen = 'gcc' }

    else {

        Fail ("No compiler found. Tried repo-bundled zig " +

              "(tools\toolchain\pylibs\ziglang\zig.exe), zig on PATH, gcc on PATH, " +

              "MINGW_DIR, W64DEVKIT_DIR and C:\msys64\mingw64\bin\gcc.exe.")

    }

}



if ($chosen -eq 'zig') {

    Log "toolchain: zig from $zigOrigin ($zig)"

    $null = Invoke-Tool -Exe $zig -Arguments @('version') -What 'zig --version'

} else {

    Log "toolchain: gcc ($gcc)"

    $null = Invoke-Tool -Exe $gcc -Arguments @('--version') -What 'gcc --version'

}



# ---------------------------------------------------------------- build DLL

$src = Join-Path $PackDir 'bbgame_probe.c'

$hdr = Join-Path $PackDir 'bbgame.h'

if (-not (Test-Path -LiteralPath $src)) { Fail "missing $src" }

if (-not (Test-Path -LiteralPath $hdr)) { Fail "missing $hdr" }

$dll = Join-Path $OutputDir 'bbgame_probe.dll'



if ($chosen -eq 'zig') {

    $toolArgs = @('cc','-target','x86_64-windows-gnu','-std=c11','-O2','-Wall','-Wextra','-shared',

              '-I', $PackDir, '-o', $dll, $src)

    $code = Invoke-Tool -Exe $zig -Arguments $toolArgs -What 'build bbgame_probe.dll (zig)'

} else {

    $toolArgs = @('-std=c11','-O2','-Wall','-Wextra','-shared','-I',$PackDir,'-o',$dll,$src)

    $code = Invoke-Tool -Exe $gcc -Arguments $toolArgs -What 'build bbgame_probe.dll (gcc)'

}

if ($code -ne 0) { Fail "DLL build failed with exit code $code (see $BuildLog)." }

if (-not (Test-Path -LiteralPath $dll)) { Fail "compiler reported success but $dll does not exist." }

$dllSize = (Get-Item -LiteralPath $dll).Length

if ($dllSize -le 0) { Fail "$dll is empty." }

Log "OK: $dll ($dllSize bytes)"



# ---------------------------------------------------------------- self test

if ($SelfTest) {

    Log 'SelfTest: building smoke_host.exe (fake BbGameHost, no game required)'

    $smokeSrc = Join-Path $PackDir 'smoke_host.c'

    if (-not (Test-Path -LiteralPath $smokeSrc)) { Fail "missing $smokeSrc" }

    $smokeExe = Join-Path $OutputDir 'smoke_host.exe'

    if ($chosen -eq 'zig') {

        $sargs = @('cc','-target','x86_64-windows-gnu','-std=c11','-O2','-Wall','-Wextra',

                   '-I', $PackDir, '-o', $smokeExe, $smokeSrc)

        $code = Invoke-Tool -Exe $zig -Arguments $sargs -What 'build smoke_host.exe (zig)'

    } else {

        $sargs = @('-std=c11','-O2','-Wall','-Wextra','-I',$PackDir,'-o',$smokeExe,$smokeSrc)

        $code = Invoke-Tool -Exe $gcc -Arguments $sargs -What 'build smoke_host.exe (gcc)'

    }

    if ($code -ne 0) { Fail "smoke_host build failed with exit code $code." }



    $smokeLog = Join-Path $OutputDir 'smoke_probe.log'

    $env:BB_PROBE_LOG = $smokeLog

    $env:BB_PROBE_WRITE_TEST = '1'

    $code = Invoke-Tool -Exe $smokeExe -Arguments @($dll, $smokeLog) -What 'run smoke_host.exe'

    if ($code -ne 0) { Fail "smoke test failed with exit code $code (see $BuildLog)." }

    if (-not (Test-Path -LiteralPath $smokeLog)) { Fail "smoke test passed but $smokeLog is missing." }

    $smokeText = Get-Content -Raw -LiteralPath $smokeLog

    $needles = @(

        'event=bbgame_init.enter',

        'event=host.version value=1',

        'event=mem.write_back offset=0x0 size=8 result=ok',

        'event=hook.install result=0',

        'event=hook.triggered count=1',

        'event=hook.trampoline',

        'event=hook.triggered count=2',

        'event=bbgame_init.exit result=0',

        'reason=version_mismatch'

    )

    foreach ($needle in $needles) {

        if ($smokeText -notmatch [regex]::Escape($needle)) { Fail "smoke log is missing '$needle'." }

    }

    Log "OK: SelfTest passed; smoke log $smokeLog"

}



Log 'BUILD OK'

Write-Host ''

Write-Host "bbgame_probe.dll: $dll"

if ($SelfTest) { Write-Host "smoke_host.exe : $(Join-Path $OutputDir 'smoke_host.exe')" }

Write-Host "build.log      : $BuildLog"

exit 0

