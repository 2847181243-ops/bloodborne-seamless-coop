<#
.SYNOPSIS
  Local pre-CI: reproduce the CI gates BEFORE pushing.

.DESCRIPTION
  Motivation: this repo once pushed three times in a row before discovering the
  gates were red. Most gate failures are decidable purely locally (encoding,
  structure, branch name, write scope, workflow syntax, secrets), so paying a
  CI round-trip for them is waste.

  Two stages (see tools/preci/gates.yml):
    Stage 1 (default): needs no PR metadata; run before every push.
    Stage 2          : needs PR body/labels; run after opening a PR, before
                       asking for a merge.

  Design rules:
    1. REUSE, do not rewrite: stage 1 runs the same predicates as CI (same
       marker table, same SCOPE table, same required-file list) so that
       "green locally, red in CI" cannot happen for these checks.
    2. A failure must state what it checked and on what authority.
    3. Never pass silently: an unrunnable check is SKIP and is counted as
       UNVERIFIED, never as a pass.

  NOTE ON ENCODING: this file is UTF-8 **with a BOM**, and must stay that way.
  (An earlier version was pure ASCII and said so; Chinese literals were added
  later, so that claim is no longer true.) Windows PowerShell 5.1 decodes a
  BOM-less file using the ANSI codepage (GBK here), which turns every Chinese
  literal into mojibake and can break parsing outright:
      Unexpected token '[鏈塢' in expression or statement
  If a tool rewrites this file without the BOM, re-add it before committing --
  tools/preci/check_style.py or a plain `python -c` one-liner will do.

.PARAMETER Stage
  1 or 2. Default 1.

.PARAMETER Base
  Comparison base, default origin/main.

.PARAMETER PrBody
  Path to a PR body file (stage 2).

.PARAMETER Pr
  PR number. Fetches body and labels from the GitHub API (needs a token).

.PARAMETER Strict
  Treat SKIP as failure (exit 2).

.PARAMETER AllowSkip
  Treat SKIP as acceptable and exit 0. Default is the opposite: a SKIP yields
  exit 2, so that "did not check" is never confused with "checked and fine".

.EXAMPLE
  powershell -NoProfile -File tools/preci/preci.ps1

.EXAMPLE
  powershell -NoProfile -File tools/preci/preci.ps1 -Stage 2 -Pr 8
#>
[CmdletBinding()]
param(
    [ValidateSet(1, 2)]
    [int]$Stage = 1,
    [string]$Base = 'origin/main',
    [string]$PrBody,
    [int]$Pr = 0,
    [switch]$Strict,
    # Treat SKIP as acceptable and return 0. The default is the opposite on
    # purpose: "did not check" must never look like "checked and fine", so a SKIP
    # yields exit 2 unless this flag is given.
    [switch]$AllowSkip,
    # Record WHY you are proceeding despite skipped checks. This is the only way a
    # SKIP may be treated as acceptable. It must be a substantive reason: empty,
    # too short, or a platitude ("不适用" / "later" / "n/a") is rejected. If any skip
    # could be waved through with a shrug, this mechanism would be theatre.
    [string]$SkipReason,
    # Run against another working tree. Used by the negative-case tests so the
    # checks themselves are exercised, instead of a re-implementation drifting.
    [string]$Repo
)

$ErrorActionPreference = 'Continue'
if ($Repo) {
    if (-not (Test-Path $Repo)) { Write-Host "Repo not found: $Repo"; exit 3 }
    $script:Root = (Resolve-Path $Repo).Path
} else {
    $script:Root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
}
Set-Location $script:Root

# ---- messages (UTF-8 explicit; see header note) ------------------------------
$script:MsgPath = Join-Path $PSScriptRoot 'messages.json'
$script:Msg = @{}
if (Test-Path $script:MsgPath) {
    $json = [IO.File]::ReadAllText($script:MsgPath, (New-Object Text.UTF8Encoding($false)))
    $obj = ConvertFrom-Json $json
    foreach ($p in $obj.PSObject.Properties) { $script:Msg[$p.Name] = $p.Value }
}
function M {
    param([string]$Key, [object[]]$Arg)
    $t = $script:Msg[$Key]
    if (-not $t) { return "[missing message: $Key]" }
    if ($Arg -and $Arg.Count -gt 0) {
        # Coerce every argument to string FIRST. PowerShell's -f is picky: passing
        # an object (e.g. a natively-typed exception property) raises
        # "Cannot convert ... to type System.Int32", and swallowing that in a
        # catch made a broken message silently degrade into the raw template --
        # which then printed a whole PSCustomObject into the log.
        $strs = @()
        foreach ($a in $Arg) { $strs += [string]$a }
        try {
            return ,($t -f $strs)
        } catch {
            # Never fail silently: surface the formatting problem itself.
            return ("[message format error: $Key -> " + [string]$_.Exception.Message + "]")
        }
    }
    return $t
}

# ---- output and counters ----------------------------------------------------
$script:Pass = 0
$script:Fail = 0
$script:Skip = 0
$script:Failed = New-Object System.Collections.ArrayList
$script:Unverified = New-Object System.Collections.ArrayList

function Write-Head([string]$t) {
    Write-Host ''
    Write-Host ('-' * 74) -ForegroundColor DarkGray
    Write-Host "  $t" -ForegroundColor Cyan
    Write-Host ('-' * 74) -ForegroundColor DarkGray
}
# Output labels are plain Chinese, not [PASS]/[FAIL]/[SKIP]: the audience is a
# person reading a terminal, and mixing English tags into Chinese sentences was
# called out. Labels come from messages.json like every other string.
function Write-Ok([string]$t) {
    Write-Host ("  [" + (M 'lbl_pass') + "] $t") -ForegroundColor Green
    $script:Pass++
}
function Write-Bad([string]$t) {
    Write-Host ("  [" + (M 'lbl_fail') + "] $t") -ForegroundColor Red
    $script:Fail++
    [void]$script:Failed.Add($t)
}
function Write-Skip([string]$t) {
    Write-Host ("  [" + (M 'lbl_skip') + "] $t") -ForegroundColor Yellow
    $script:Skip++
    [void]$script:Unverified.Add($t)
}
function Write-Info([string]$t) { Write-Host "         $t" -ForegroundColor DarkGray }

# ---- helpers ----------------------------------------------------------------
function Get-BashPath {
    foreach ($p in @("$env:ProgramFiles\Git\bin\bash.exe",
                     "${env:ProgramFiles(x86)}\Git\bin\bash.exe",
                     "$env:LOCALAPPDATA\Programs\Git\bin\bash.exe")) {
        if (Test-Path $p) { return $p }
    }
    $c = Get-Command bash.exe -ErrorAction SilentlyContinue
    if ($c) { return $c.Source }
    return $null
}

function Invoke-Bash {
    param([string]$Script)
    $bash = Get-BashPath
    if (-not $bash) { return @{ Ok = $false; Code = -1; Out = 'bash not found' } }
    $tmp = Join-Path ([IO.Path]::GetTempPath()) ('preci-' + [guid]::NewGuid().ToString('N') + '.sh')
    # LC_ALL=C.UTF-8 is mandatory: without it sed treats the full-width colon as
    # bytes and mangles multibyte values (measured: 20/0 becomes 15/5).
    $full = "export LC_ALL=C.UTF-8`n" + $Script
    [IO.File]::WriteAllText($tmp, $full, (New-Object Text.UTF8Encoding($false)))
    try {
        $out = & $bash $tmp 2>&1 | Out-String
        return @{ Ok = ($LASTEXITCODE -eq 0); Code = $LASTEXITCODE; Out = $out.Trim() }
    } finally {
        Remove-Item $tmp -Force -ErrorAction SilentlyContinue
    }
}

function Get-ChangedFiles {
    # -c core.quotepath=false is REQUIRED. With git's default (true) a non-ASCII
    # path is rendered as octal escapes inside quotes:
    #     "docs/standards/\345\217\202..."
    # The write-scope prefix match then fails and reports a bogus out-of-scope
    # file. Measured with a Chinese filename in docs/standards/.
    # The quote-stripping is defensive: git still quotes paths containing unusual
    # characters even with quotepath=false.
    $g = @('-c', 'core.quotepath=false', 'diff', '--name-only')
    $r = @(git @g "$Base...HEAD" 2>$null)
    if (-not $r -or $r.Count -eq 0) { $r = @(git @g $Base 2>$null) }
    $out = @()
    foreach ($f in $r) {
        if (-not $f) { continue }
        $s = $f.Trim()
        if ($s.Length -ge 2 -and $s.StartsWith('"') -and $s.EndsWith('"')) {
            $s = $s.Substring(1, $s.Length - 2)
        }
        if ($s) { $out += $s }
    }
    return $out
}

function Get-HeadRef {
    $b = (git branch --show-current 2>$null)
    if ($b) { return $b.Trim() }
    if ($env:GITHUB_HEAD_REF) { return $env:GITHUB_HEAD_REF }
    return ''
}

function Get-ScopeTable {
    # Parse the SCOPE map out of branch-policy.yml so CI and local share ONE source.
    # Actual CI format (branch-policy.yml):
    #     declare -A SCOPE=(
    #       [lead]="docs/interfaces/ .github/ .dsh/ ..."
    #       [a1]="docs/re/ mod/core/"
    #     )
    # The leading "[name]=" matters -- an earlier regex without it silently
    # produced an empty table and the scope check degraded to SKIP.
    $wf = Join-Path $script:Root '.github\workflows\branch-policy.yml'
    $t = @{}
    if (-not (Test-Path $wf)) { return $t }
    $text = [IO.File]::ReadAllText($wf)
    foreach ($m in [regex]::Matches($text, '\[([a-z0-9]+)\]="([^"]*)"')) {
        $t[$m.Groups[1].Value] = @($m.Groups[2].Value -split '\s+' | Where-Object { $_ })
    }
    return $t
}

function Get-RunBodies {
    # Extract `run: |` bodies (10-space de-indent) tagged by job name.
    param([string]$Path)
    $lines = [IO.File]::ReadAllLines($Path)
    $jobs = @{}
    $curJob = $null
    $cur = $null
    $pending = $false
    $inJobs = $false
    $flush = {
        if ($null -ne $cur -and $curJob) {
            if (-not $jobs.ContainsKey($curJob)) { $jobs[$curJob] = New-Object System.Collections.ArrayList }
            [void]$jobs[$curJob].Add(($cur -join "`n"))
        }
    }
    foreach ($raw in $lines) {
        $ln = $raw.TrimEnd("`r")
        if ($ln -match '^jobs:\s*$') { $inJobs = $true; continue }
        if ($inJobs -and -not $pending) {
            if ($ln -match '^  ([A-Za-z0-9_-]+):\s*$') {
                & $flush
                $cur = $null
                $curJob = $Matches[1]
                if (-not $jobs.ContainsKey($curJob)) { $jobs[$curJob] = New-Object System.Collections.ArrayList }
                continue
            }
        }
        if ($pending) { $pending = $false; $cur = New-Object System.Collections.ArrayList; continue }
        if ($ln -match '^\s*run: \|\s*$') { & $flush; $cur = $null; $pending = $true; continue }
        if ($null -ne $cur) {
            if ($ln.Trim() -eq '') { [void]$cur.Add(''); continue }
            if ($ln -match '^\s{10,}') { [void]$cur.Add($ln.Substring(10)); continue }
            & $flush
            $cur = $null
        }
    }
    & $flush
    return $jobs
}

# =============================================================================
Write-Host ''
Write-Host ('  ' + (M 'title')) -ForegroundColor White
Write-Host ('  ' + (M 'repo') + ': ' + $script:Root)
$curRef = Get-HeadRef
Write-Host ('  ' + (M 'stage') + ': ' + $Stage + '    ' + (M 'base') + ': ' + $Base)
Write-Host ('  ' + (M 'branch') + ': ' + $curRef)
Write-Host ('  HEAD ' + (git rev-parse --short HEAD 2>$null))

$changed = Get-ChangedFiles
Write-Info (M 'changed_n' @($changed.Count))

# CI's branch-policy matches lead/* on a plain glob, so a lead topic needs no
# hyphen ('lead/wfbad' is legal). agent/* needs <id>-<topic>. An earlier single
# regex required a hyphen for BOTH and wrongly rejected legal lead branches.
$rxBranch = '^(lead/[^/]+|agent/[a-z0-9]+-[^/]+)$'
$ref = Get-HeadRef

# ---- stage 1 ----------------------------------------------------------------
if ($Stage -ge 1) {

    Write-Head (M 'check_branch')
    if (-not $ref) {
        Write-Bad (M 'no_branch')
    } elseif ($ref -match $rxBranch) {
        if ($ref -match '^lead/') {
            Write-Ok (M 'branch_lead_ok' @($ref))
        } else {
            $id = ($ref -replace '^agent/', '') -replace '-.*$', ''
            $valid = @('a0','a1','a2','a3','a4','b1','b2','b3','b4','c1','c2','c3','c4','lead')
            if ($valid -contains $id) {
                Write-Ok (M 'branch_agent_ok' @($ref, $id))
            } else {
                Write-Bad (M 'branch_bad_agent' @($ref, $id, ($valid -join ' ')))
            }
        }
    } else {
        Write-Bad (M 'branch_bad_form' @($ref))
    }

    Write-Head (M 'check_scope')
    $scope = Get-ScopeTable
    if ($scope.Count -eq 0) {
        Write-Skip (M 'scope_noparse')
    } else {
        # Agent id must come from the AGENT segment only:
        #   lead/<topic>            -> agent is 'lead'  (topic may contain '-')
        #   agent/<id>-<topic>      -> agent is <id>
        # Using a single regex on the whole ref mis-parsed
        # 'lead/pr-guard-strict' as agent 'pr' (the first hyphen-delimited token).
        $agent = ''
        if ($ref -match '^lead/') {
            $agent = 'lead'
        } elseif ($ref -match '^agent/([a-z0-9]+)-') {
            $agent = $Matches[1]
        }
        if (-not $agent) {
            Write-Skip (M 'scope_noagent')
        } else {
            $allowed = @()
            if ($scope.ContainsKey($agent)) { $allowed = @($scope[$agent]) }
            $oob = @()
            foreach ($f in $changed) {
                $hit = $false
                foreach ($pfx in $allowed) { if ($f.StartsWith($pfx)) { $hit = $true; break } }
                if (-not $hit) { $oob += $f }
            }
            if ($oob.Count -eq 0) {
                Write-Ok (M 'scope_ok' @($changed.Count, $agent))
            } else {
                Write-Bad (M 'scope_oob' @($oob.Count, $agent, ($oob -join ', ')))
                Write-Info (M 'scope_note1')
                Write-Info (M 'scope_note2')
                Write-Info (M 'scope_allowed' @($agent, ($allowed -join ' ')))
            }
        }
    }

    Write-Head (M 'check_encoding')
    $enc = @'
P=$(mktemp)
printf '%b\n' '\347\200\271' '\345\244\212' '\345\217\217' '\345\251\225' '\345\277\224' \
  '\347\244\212' '\351\224\233' '\345\240\243' '\351\215\225' '\351\214\250' '\346\265\234' '\347\221\272' > "$P"
hit=$(git ls-files -z | xargs -0 -r grep -a -l -F -f "$P" 2>/dev/null)
rm -f "$P"
fail=0
if [ -n "$hit" ]; then
  echo "GBK mojibake residue:"
  printf '  %s\n' $hit
  fail=1
fi
bad=""
while IFS= read -r -d '' f; do
  case "$f" in
    *.png|*.jpg|*.jpeg|*.gif|*.ico|*.zip|*.dll|*.exe|*.bin|*.sl2|*.lib|*.obj|\
    *.pyc|*.pyo|*.so|*.dylib|*.pdb|*.pak|*.7z|*.ttf|*.woff|*.woff2) continue;;
  esac
  # 二进制文件即使扩展名没列到，也不该按 UTF-8 文本读。
  # 实测：tools/preci/__pycache__/*.pyc 曾因扩展名未列入而被报 invalid-UTF8。
  if head -c 4096 "$f" 2>/dev/null | LC_ALL=C grep -qP '\x00' 2>/dev/null; then continue; fi
  [ -f "$f" ] || continue
  if ! iconv -f UTF-8 -t UTF-8 "$f" >/dev/null 2>&1; then
    bad="$bad $f(invalid-UTF8)"
  elif grep -qU $'\xef\xbf\xbd' "$f" 2>/dev/null; then
    bad="$bad $f(has-U+FFFD)"
  fi
done < <(git ls-files -z)
if [ -n "$bad" ]; then
  echo "encoding problems:$bad"
  fail=1
fi
if [ "$fail" -eq 0 ]; then echo "OK_ALL_TEXT_UTF8"; fi
exit $fail
'@
    $r = Invoke-Bash $enc
    if ($r.Ok) { Write-Ok (M 'enc_ok') } else { Write-Bad (M 'enc_bad' @($r.Out)) }

    Write-Head (M 'check_codeowners')
    $co = @'
# IMPORTANT: validate the GIT BLOB, not the working-tree file.
# Measured on this repo: `git ls-files --eol` reports i/lf w/crlf for
# .github/CODEOWNERS. Reading the working tree makes every blank line contain a
# stray CR, so `case "$line" in '')` no longer matches and blank separator lines
# get flagged as "indented" -- a false red that CI (which checks out LF) never
# sees. Reading the blob keeps local and CI byte-identical.
fail=0
found=""
for f in CODEOWNERS .github/CODEOWNERS docs/CODEOWNERS; do
  if git cat-file -e "HEAD:$f" 2>/dev/null; then found="$f"; break; fi
done
if [ -z "$found" ]; then echo "no CODEOWNERS file in git"; exit 1; fi
tmp=$(mktemp)
git show "HEAD:$found" > "$tmp"
lineno=0
rules=0
while IFS= read -r line || [ -n "$line" ]; do
  lineno=$((lineno+1))
  case "$line" in ''|'#'*) continue;; esac
  if printf '%s' "$line" | grep -qE '^[[:space:]]+'; then
    echo "line $lineno must not be indented: $line"; fail=1; continue
  fi
  rest=$(printf '%s' "$line" | sed -E 's/^[^[:space:]]+[[:space:]]+//')
  case "$rest" in
    @*) rules=$((rules+1));;
    *) echo "line $lineno has no owner: $line"; fail=1;;
  esac
done < "$tmp"
if [ "$rules" -eq 0 ]; then echo "no valid rule lines"; fail=1; fi
# CODEOWNERS paths conventionally carry a leading slash; accept both.
for p in mod/security/ mod/network/ relay/ docs/audit/ docs/re/ tools/audit/ tools/pentest/; do
  if ! grep -qE "^[[:space:]]*/?$p" "$tmp"; then
    echo "missing required ownership entry $p (task book sec 48)"; fail=1
  fi
done
rm -f "$tmp"
if [ "$fail" -eq 0 ]; then echo "CO_OK $found $rules"; fi
exit $fail
'@
    $r = Invoke-Bash $co
    if ($r.Ok) {
        $parts = ($r.Out -split '\s+')
        Write-Ok (M 'co_ok' @($parts[1], $parts[2]))
    } else {
        Write-Bad (M 'co_bad' @($r.Out))
    }

    Write-Head (M 'check_wf')
    $wfDir = Join-Path $script:Root '.github\workflows'
    $wfs = @(Get-ChildItem $wfDir -Filter '*.yml' -ErrorAction SilentlyContinue)
    if ($wfs.Count -eq 0) {
        Write-Skip (M 'wf_none')
    } else {
        # (a) bash -n on every run body
        $bad = @()
        $bash = Get-BashPath
        foreach ($w in $wfs) {
            $jobs = Get-RunBodies -Path $w.FullName
            foreach ($job in $jobs.Keys) {
                $i = 0
                foreach ($b in $jobs[$job]) {
                    $i++
                    if (-not $bash) { continue }
                    $tmp = Join-Path ([IO.Path]::GetTempPath()) ('wfchk-' + [guid]::NewGuid().ToString('N') + '.sh')
                    [IO.File]::WriteAllText($tmp, $b, (New-Object Text.UTF8Encoding($false)))
                    $null = & $bash -n $tmp 2>&1
                    if ($LASTEXITCODE -ne 0) { $bad += "$($w.Name):$job#$i" }
                    Remove-Item $tmp -Force -ErrorAction SilentlyContinue
                }
            }
        }
        if ($bad.Count -eq 0) { Write-Ok (M 'wf_ok' @($wfs.Count)) }
        else { Write-Bad (M 'wf_bad' @($bad -join ', ')) }

        # (b) real YAML parse + structural invariants.
        # bash -n only proves the run bodies are syntactically valid shell; it says
        # nothing about YAML structure, `needs:` targets, or the documented Pending
        # trap from path-filtered required checks. Soft dependency: needs a Python
        # with PyYAML. Absence is reported as SKIP (counted as UNVERIFIED), never
        # as a pass.
        $py = $null
        # Prefer a project-local install (tools/preci/.pylibs) so a machine with no
        # PATH python still gets structure validation. Then PATH.
        $localPy = Join-Path $PSScriptRoot '.pylibs'
        foreach ($cand in @('python', 'python3', 'py')) {
            $c = Get-Command $cand -ErrorAction SilentlyContinue
            if ($c) { $py = $c.Source; break }
        }
        if ($py) {
            if (Test-Path $localPy) {
                $env:PYTHONPATH = $localPy + [IO.Path]::PathSeparator + $env:PYTHONPATH
            }
            $vscript = Join-Path $PSScriptRoot 'validate_workflows.py'
            if (-not (Test-Path $vscript)) {
                Write-Skip (M 'yaml_skip')
                Write-Info (M 'yaml_skip2')
            } else {
                $out = (& $py $vscript $wfDir 2>&1 | Out-String)
                $code = $LASTEXITCODE
                if ($code -eq 0) {
                    Write-Ok (M 'yaml_ok' @($wfs.Count))
                } elseif ($code -eq 3 -or $out -match 'SKIP:') {
                    # A missing PyYAML is UNVERIFIED, not a failure -- otherwise a
                    # machine without it would look like a broken repo.
                    Write-Skip (M 'yaml_skip')
                    Write-Info (M 'yaml_skip2')
                } else {
                    $why = (($out.Trim() -split "`n") | Where-Object { $_ -match '^\s*!' }) -join ' ; '
                    if (-not $why) { $why = (($out.Trim() -split "`n") | Select-Object -Last 1) }
                    Write-Bad (M 'yaml_bad' @([string]$why))
                }
            }
        } else {
            Write-Skip (M 'yaml_skip')
            Write-Info (M 'yaml_skip2')
        }
    }

    Write-Head (M 'check_structure')
    $ds = Join-Path $script:Root '.github\workflows\docs-structure.yml'
    if (-not (Test-Path $ds)) {
        Write-Skip (M 'st_nofile')
    } else {
        $text = [IO.File]::ReadAllText($ds)
        $req = @()
        foreach ($m in [regex]::Matches($text, "(?m)^\s*'([^']+\.(md|yml|yaml|txt|cfg|json))'\s*$")) {
            $req += $m.Groups[1].Value
        }
        if ($req.Count -eq 0) {
            foreach ($m in [regex]::Matches($text, '(?m)^\s*"([^"]+\.(md|yml|yaml|txt|cfg|json))"\s*$')) {
                $req += $m.Groups[1].Value
            }
        }
        if ($req.Count -eq 0) {
            Write-Skip (M 'st_noparse')
        } else {
            $miss = @($req | Where-Object { -not (Test-Path (Join-Path $script:Root $_)) })
            if ($miss.Count -eq 0) { Write-Ok (M 'st_ok' @($req.Count)) }
            else { Write-Bad (M 'st_bad' @($miss -join ', ')) }
        }
    }

    Write-Head (M 'check_secrets')
    $sec = @'
pat='ghp_[A-Za-z0-9]{20,}|github_pat_[A-Za-z0-9_]{20,}'
pat="$pat|gho_[A-Za-z0-9]{20,}|ghs_[A-Za-z0-9]{20,}"
pat="$pat|-----BEGIN[A-Z ]*PRIVATE KEY-----"
pat="$pat|xox[baprs]-[A-Za-z0-9-]{10,}"
hits=$(git diff "origin/main...HEAD" 2>/dev/null | grep -E '^\+' | grep -vE '^\+\+\+' | grep -nE "$pat" || true)
if [ -n "$hits" ]; then
  echo "$hits" | head -5
  exit 1
fi
echo "SEC_OK"
'@
    $r = Invoke-Bash $sec
    if ($r.Ok) { Write-Ok (M 'sec_ok') } else { Write-Bad (M 'sec_bad' @($r.Out)) }

    Write-Head (M 'check_style')
    # Enforces .editorconfig (which existed but nothing checked) and the task
    # book's comment rule: bloodbrone.markdown:1160 forbids mass-commenting code
    # to silence errors. Scans the COMMITTED BLOBs, not the working tree -- see
    # check_style.py's docstring for the two false-positive traps that caused.
    $pyStyle = $null
    foreach ($cand in @('python', 'python3', 'py')) {
        $c = Get-Command $cand -ErrorAction SilentlyContinue
        if ($c) { $pyStyle = $c.Source; break }
    }
    $styleScript = Join-Path $PSScriptRoot 'check_style.py'
    if (-not $pyStyle -or -not (Test-Path $styleScript)) {
        Write-Skip (M 'style_skip')
    } else {
        $sout = (& $pyStyle $styleScript --repo $script:Root 2>&1 | Out-String)
        $scode = $LASTEXITCODE
        $nfiles = 0
        if ($sout -match 'scanned\s+(\d+)') { $nfiles = [int]$Matches[1] }
        if ($scode -eq 0) {
            Write-Ok (M 'style_ok' @($nfiles))
            $warns = (($sout.Trim() -split "`n") | Where-Object { $_ -match '^\s*~ ' })
            if ($warns) { Write-Info (M 'style_warn' @([string]$warns.Count)) }
        } else {
            $first = (($sout.Trim() -split "`n") |
                      Where-Object { $_ -match '^\s*! ' } |
                      ForEach-Object { $_.Trim().Substring(2).Trim() } |
                      Select-Object -First 3) -join ' / '
            $nerr = 0
            if ($sout -match 'errors=(\d+)') { $nerr = [int]$Matches[1] }
            Write-Bad (M 'style_bad' @([string]$nerr, [string]$first))
        }
    }

    Write-Head (M 'check_build')
    $cml = Join-Path $script:Root 'CMakeLists.txt'
    if (-not (Test-Path $cml)) {
        Write-Ok (M 'bd_nocmake')
        Write-Info (M 'bd_nocmake_note')
    } else {
        $cmake = Get-Command cmake -ErrorAction SilentlyContinue
        if (-not $cmake) {
            Write-Skip (M 'bd_nobinary')
        } else {
            Write-Info (M 'bd_running')
            $bd = Join-Path ([IO.Path]::GetTempPath()) ('preci-build-' + [guid]::NewGuid().ToString('N'))
            $cfg = & cmake -S $script:Root -B $bd -A x64 2>&1 | Out-String
            if ($LASTEXITCODE -ne 0) {
                $tail = ($cfg.Trim() -split "`n" | Select-Object -Last 5) -join ' / '
                Write-Bad (M 'bd_cfg_fail' @($tail))
            } else {
                $bld = & cmake --build $bd --config Release 2>&1 | Out-String
                if ($LASTEXITCODE -ne 0) {
                    $tail = ($bld.Trim() -split "`n" | Select-Object -Last 8) -join ' / '
                    Write-Bad (M 'bd_build_fail' @($tail))
                } else {
                    Write-Ok (M 'bd_ok')
                }
            }
            Remove-Item $bd -Recurse -Force -ErrorAction SilentlyContinue
        }
    }
}

# ---- stage 2 ----------------------------------------------------------------
if ($Stage -ge 2) {

    Write-Head (M 'stage2_title')

    $repoSlug = '2847181243-ops/bloodborne-seamless-coop'
    $token = $null
    foreach ($v in @('GITHUB_TOKEN', 'GH_TOKEN')) {
        # NOTE: `$env:$v` is not valid in PowerShell 5.1; use the .NET API.
        $val = [Environment]::GetEnvironmentVariable($v)
        if ($val) { $token = $val; break }
    }
    if (-not $token) {
        foreach ($c in @((Join-Path $env:USERPROFILE '.config\dsh\token.txt'),
                         (Join-Path $env:USERPROFILE '.config\gh\token.txt'))) {
            if (Test-Path $c) { $token = (Get-Content $c -Raw).Trim(); break }
        }
    }

    $labels = ''
    if ($Pr -gt 0) {
        Write-Info (M 's2_fetch' @($Pr))
        # Delegate the fetch to fetch_pr.py. Three inline attempts under Windows
        # PowerShell 5.1 all failed on object marshalling (Invoke-RestMethod,
        # System.Net.WebRequest, curl+ConvertFrom-Json each raised
        # "cannot convert PSCustomObject to Int32" or an
        # ArgumentTransformationMetadataException). A separate process that only
        # has to write two files is testable and boring -- which is the point.
        $pyExe = $null
        foreach ($cand in @('python', 'python3', 'py')) {
            $c = Get-Command $cand -ErrorAction SilentlyContinue
            if ($c) { $pyExe = $c.Source; break }
        }
        $fetcher = Join-Path $PSScriptRoot 'fetch_pr.py'
        if (-not $pyExe -or -not (Test-Path $fetcher)) {
            Write-Skip (M 's2_fetch_fail' @($Pr, 'python or fetch_pr.py unavailable'))
        } else {
            $deadline = Join-Path ([IO.Path]::GetTempPath()) 'preci-fetch'
            $fout = (& $pyExe $fetcher ([string]$Pr) $deadline 2>&1 | Out-String)
            $fcode = $LASTEXITCODE
            if ($fcode -ne 0) {
                $why = (($fout.Trim() -split "`n") | Select-Object -Last 1)
                Write-Skip (M 's2_fetch_fail' @($Pr, [string]$why))
            } else {
                $pf = Join-Path $deadline "pr$Pr-body.md"
                $lf = Join-Path $deadline "pr$Pr-labels.txt"
                if (Test-Path $pf) { $PrBody = $pf }
                if (Test-Path $lf) { $labels = (Get-Content $lf -Raw); if ($null -eq $labels) { $labels = '' } }
                Write-Info (M 's2_fetched' @($Pr, '', [string]$labels))
            }
        }
    }

    if (-not $PrBody -or -not (Test-Path $PrBody)) {
        Write-Skip (M 's2_nobody')
        Write-Info (M 's2_usage1')
        Write-Info (M 's2_usage2')
    } else {
        $bash = Get-BashPath
        $prGuard = Join-Path $script:Root '.github\workflows\pr-guard.yml'
        $jobs = Get-RunBodies -Path $prGuard
        $tpl = @()
        if ($jobs.ContainsKey('template')) { $tpl = @($jobs['template']) }
        $gate = @()
        if ($jobs.ContainsKey('security-gate')) { $gate = @($jobs['security-gate']) }

        $pre = "export LC_ALL=C.UTF-8`nexport BASE_REF=main`nexport GITHUB_REPOSITORY=$repoSlug`n"
        if ($token) { $pre += "export GITHUB_TOKEN=$token`n" }
        if ($labels) { $pre += "export LABELS='$labels'`n" }
        $bodyUnix = $PrBody -replace '\\', '/'
        # The gate scripts read $BODY (the name used by the workflow's `env:` block).
        # Exporting only PRBODY left BODY unset -- and when the parent shell happened
        # to carry a stale BODY, the gate silently validated THAT instead of the PR
        # under test (observed: a corrected PR body kept failing on the old text).
        # Export BODY explicitly; keep PRBODY for compatibility.
        $pre += "export BODY=`"`$(cat '$bodyUnix')`"`n"
        $pre += "export PRBODY=`"`$BODY`"`n"

        $names = @((M 's2_step1'), (M 's2_step2'))
        for ($i = 0; $i -lt $tpl.Count -and $i -lt 2; $i++) {
            $nm = $names[$i]
            if (-not $bash) { Write-Skip (M 's2_nobash' @($nm)); continue }
            $s2name = 'preci-s2-' + $i + '-' + [guid]::NewGuid().ToString('N') + '.sh'
            $tmp = Join-Path ([IO.Path]::GetTempPath()) $s2name
            [IO.File]::WriteAllText($tmp, ($pre + $tpl[$i]), (New-Object Text.UTF8Encoding($false)))
            $out = & $bash $tmp 2>&1 | Out-String
            $code = $LASTEXITCODE
            Remove-Item $tmp -Force -ErrorAction SilentlyContinue
            if ($code -eq 0) {
                Write-Ok (M 's2_pass' @($nm))
                ($out -split "`n") | Where-Object { $_ -match ([char]0x2705) } | ForEach-Object { Write-Info $_.Trim() }
            } else {
                Write-Bad (M 's2_fail' @($nm))
                ($out -split "`n") | Where-Object { $_ -match '::error' } | ForEach-Object { Write-Info $_.Trim() }
            }
        }

        if ($gate.Count -gt 0) {
            $nm = M 's2_step3'
            if (-not $bash) {
                Write-Skip (M 's2_nobash' @($nm))
            } else {
                $tmp = Join-Path ([IO.Path]::GetTempPath()) ('preci-gate-' + [guid]::NewGuid().ToString('N') + '.sh')
                [IO.File]::WriteAllText($tmp, ($pre + $gate[0]), (New-Object Text.UTF8Encoding($false)))
                $out = & $bash $tmp 2>&1 | Out-String
                $code = $LASTEXITCODE
                Remove-Item $tmp -Force -ErrorAction SilentlyContinue
                if ($code -eq 0) {
                    Write-Ok (M 's2_pass' @($nm))
                } else {
                    Write-Bad (M 's2_fail' @($nm))
                    ($out -split "`n") | Where-Object { $_ -match '::error' } | ForEach-Object { Write-Info $_.Trim() }
                }
            }
        }
    }
}

# ---- summary ----------------------------------------------------------------
Write-Head (M 'sum_title')
Write-Host (M 'sum_line' @($script:Pass, $script:Fail, $script:Skip)) -ForegroundColor White
if ($script:Failed.Count -gt 0) {
    Write-Host ''
    Write-Host ('  ' + (M 'sum_fix')) -ForegroundColor Red
    $script:Failed | ForEach-Object { Write-Host "    - $_" -ForegroundColor Red }
}
if ($script:Unverified.Count -gt 0) {
    Write-Host ''
    Write-Host ('  ' + (M 'sum_unverified')) -ForegroundColor Yellow
    $script:Unverified | ForEach-Object { Write-Host "    - $_" -ForegroundColor Yellow }
}

  # ---- explicit skip with a trace ---------------------------------------------
  # A SKIP alone is ambiguous: it cannot be distinguished from a check nobody
  # noticed. -SkipReason turns "I chose to proceed" into a recorded, reviewable
  # fact. The reason is validated, not just stored.
  $skipTrace = Join-Path $PSScriptRoot 'skip-trace.json'
  if ($SkipReason) {
      $reason = $SkipReason.Trim()
      # Platitudes that carry no information. Compared case-insensitively after
      # stripping whitespace and punctuation.
      $banned = @('不适用', '不相关', 'n/a', 'na', 'later', '以后', '待定', '无', 'none',
                  'todo', 'tbd', '不知道', 'skip', '忽略', '随便', 'ok', 'fine')
      $norm = ($reason -replace '[\s\p{P}]', '').ToLower()
      $isBanned = $false
      foreach ($b in $banned) { if ($norm -eq $b) { $isBanned = $true } }
      if ($reason.Length -lt 12) {
          Write-Host ''
          Write-Host ('  ' + (M 'skipreason_short')) -ForegroundColor Red
          Write-Host ('    ' + $reason) -ForegroundColor DarkGray
          exit 3
      }
      if ($isBanned) {
          Write-Host ''
          Write-Host ('  ' + (M 'skipreason_platitude')) -ForegroundColor Red
          Write-Host ('    ' + $reason) -ForegroundColor DarkGray
          exit 3
      }
      $obj = [ordered]@{
          recorded_at   = (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ')
          reason        = $reason
          stage         = $Stage
          passed        = $script:Pass
          failed        = $script:Fail
          skipped       = $script:Skip
          unverified    = @($script:Unverified)
          branch        = (& git rev-parse --abbrev-ref HEAD 2>$null)
          head          = (& git rev-parse --short HEAD 2>$null)
      }
      # StdEncoding (UTF-8 without BOM) written by hand: ConvertTo-Json + Set-Content
      # in PowerShell 5.1 defaults to UTF-16/ANSI, which would corrupt the Chinese
      # reason and break the encoding gate.
      $json = ($obj | ConvertTo-Json -Depth 5)
      [System.IO.File]::WriteAllText($skipTrace, $json, (New-Object System.Text.UTF8Encoding($false)))
      Write-Host ''
      Write-Host ('  ' + (M 'skipreason_recorded')) -ForegroundColor Yellow
      Write-Host ('    ' + $reason) -ForegroundColor DarkGray
      Write-Host ('    ' + $skipTrace) -ForegroundColor DarkGray
  } elseif ($script:Skip -gt 0 -and -not $Strict) {
      Write-Host ''
      Write-Host ('  ' + (M 'skipreason_required')) -ForegroundColor Yellow
  }

# Exit codes carry meaning, not just 0/1. A caller (a human, a script, a teammate)
# should be able to tell "failed" apart from "could not verify" without reading text.
#   0 = pass
#   1 = real failure(s) -- fix them
#   2 = passed, but something was NOT VERIFIED (SKIP), or a requested stage could
#       not run. This is deliberately distinct from 0: treating "did not check" as
#       "checked and fine" is how false confidence is manufactured.
#   3 = could not start (bad arguments / missing environment)
$exit = 0
# A recorded, validated reason is what makes proceeding acceptable.
$documentedSkip = [bool]$SkipReason
if ($script:Fail -gt 0) { $exit = 1 }
elseif ($script:Skip -gt 0 -and $Strict) { $exit = 2 }
elseif ($script:Skip -gt 0 -and -not $AllowSkip -and -not $documentedSkip) { $exit = 2 }
elseif ($Stage -ge 2 -and (-not $PrBody -or -not (Test-Path $PrBody))) { $exit = 2 }

Write-Host ''
if ($exit -eq 1) {
    Write-Host ('  ' + (M 'res_fail')) -ForegroundColor Red
} elseif ($exit -eq 2) {
    Write-Host ('  ' + (M 'res_strict')) -ForegroundColor Yellow
} elseif ($Stage -eq 1) {
    Write-Host ('  ' + (M 'res_stage1')) -ForegroundColor Green
    Write-Host ('  ' + (M 'usage_stage2')) -ForegroundColor DarkGray
} else {
    Write-Host ('  ' + (M 'res_stage2')) -ForegroundColor Green
}
Write-Host ''
exit $exit
