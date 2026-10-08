#!/usr/bin/env python3
"""Shared helpers for the pre-CI regression tests.

WHY THIS EXISTS
---------------
These tests used to live in a scratch directory under %TEMP%, with absolute paths
baked in. That meant:

  * they were invisible to collaborators and to CI;
  * nothing re-ran them, so a check could silently stop detecting anything;
  * every new session re-derived them from memory.

A gate that is never re-run is decoration. Putting the tests in the repository is
what turns "we tested it once" into "it is tested".

All paths here are derived from this file's own location, so the tests run from
anywhere and on any machine.
"""
import os
import subprocess
import sys

# tools/preci/tests/_paths.py -> repo root is three levels up
TESTS_DIR = os.path.dirname(os.path.abspath(__file__))
PRECI_DIR = os.path.dirname(TESTS_DIR)
TOOLS_DIR = os.path.dirname(PRECI_DIR)
ROOT = os.path.dirname(TOOLS_DIR)


def repo_root():
    return ROOT


def path(*parts):
    return os.path.join(ROOT, *parts)


def read(path_parts, encoding="utf-8"):
    return open(path(*path_parts), encoding=encoding).read()


def run_ps(args, timeout=None):
    """Run preci.ps1 / any PowerShell script and return (rc, output)."""
    cmd = ["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass"] + list(args)
    r = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8",
                       errors="replace", timeout=timeout, cwd=ROOT)
    return r.returncode, (r.stdout or "") + (r.stderr or "")


def run_bash(script_path, cwd=None, env=None):
    """Run a bash script with Git Bash and return (rc, output)."""
    r = subprocess.run([BASH, script_path], capture_output=True, text=True,
                       encoding="utf-8", errors="replace",
                       cwd=cwd or ROOT, env=env)
    return r.returncode, (r.stdout or "") + (r.stderr or "")


def _find_bash():
    """Locate bash portably.

    Windows uses Git Bash; CI's ubuntu runner has /usr/bin/bash. An earlier version
    only probed the Windows paths, so `have("bash")` reported the suite as SKIPPED
    on Linux -- which would have silently disabled the gate matrices in CI.
    """
    import shutil
    for p in (r"C:\Program Files\Git\bin\bash.exe",
              r"C:\Program Files (x86)\Git\bin\bash.exe",
              "/bin/bash", "/usr/bin/bash", "/usr/local/bin/bash"):
        if os.path.isfile(p):
            return p
    found = shutil.which("bash")
    return found or "bash"


def _find_git():
    """Locate git portably.

    Two suites hardcoded `C:\\Program Files\\Git\\cmd\\git.exe` as REAL_GIT. That
    passed locally and failed on the ubuntu runner with
        FileNotFoundError: [Errno 2] No such file or directory: 'C:\\\\Program Files\\\\Git\\\\cmd\\\\git.exe'
    The CI run is what surfaced it -- which is exactly why these suites belong in CI.
    """
    import shutil
    for p in (r"C:\Program Files\Git\cmd\git.exe",
              r"C:\Program Files (x86)\Git\cmd\git.exe"):
        if os.path.isfile(p):
            return p
    found = shutil.which("git")
    return found or "git"


BASH = _find_bash()
GIT = _find_git()

# ── tiny test-framework ─────────────────────────────────────────────────────
# Kept deliberately small: no dependency to install, identical output shape across
# every suite, and a non-zero exit when anything fails so CI can gate on it.


class Suite:
    def __init__(self, name):
        self.name = name
        self.passed = 0
        self.failed = 0
        self.failures = []

    def check(self, label, got, expected, detail=""):
        ok = got == expected
        if ok:
            self.passed += 1
            print(f"  [OK ] {label:<46} got={got}")
        else:
            self.failed += 1
            self.failures.append((label, expected, got, detail))
            print(f"  [BAD] {label:<46} expect={expected} got={got}")
            if detail:
                for line in detail.splitlines()[:4]:
                    print(f"         {line.strip()[:150]}")
        return ok

    def check_true(self, label, cond, detail=""):
        return self.check(label, bool(cond), True, detail)

    def finish(self):
        total = self.passed + self.failed
        print(f"\n  {self.name}: {self.passed}/{total} 通过")
        if self.failed:
            print("  失败详情:")
            for label, exp, got, detail in self.failures:
                print(f"    - {label}: expect={exp} got={got}")
                if detail:
                    for line in detail.splitlines()[:6]:
                        print(f"        {line.strip()[:160]}")
        return 0 if self.failed == 0 else 1
