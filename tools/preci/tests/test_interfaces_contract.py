#!/usr/bin/env python3
"""Contract suite: the frozen v1 interfaces still hold their promises.

T-004's acceptance is "可独立编译，不依赖模块头文件". That has a weak reading and a
strong one:

  weak   -- all headers together compile
  strong -- each header compiles on its own, with nothing included before it

The strong reading is the one that matters. If `isession.h` only compiles because
`imodule.h` happened to be included first, then a module that needs only `isession.h`
breaks -- and the breakage shows up in that module's code, not here.

So this compiles every header individually, then builds and runs the full contract
test (which also instantiates a skeleton module and proves the null-safety of
service lookup).

Two traps worth keeping in mind if this ever needs changing:

  * zig's `c++` driver does NOT support `-fsyntax-only`. It fails with
    `error: FileNotFound` pointing at the SOURCE file, which sends you hunting for a
    missing file that is present. Use `-c -o` instead.

  * No compiler available is "unverified" (exit 2), never "passed". The repo's exit
    convention treats "did not check" and "checked and fine" as different things.
"""
import os
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _paths as P  # noqa: E402

ROOT = P.ROOT
IFACE = os.path.join(ROOT, "src", "interfaces")
CONTRACT_CPP = os.path.join(ROOT, "tests", "contract",
                            "compile_interfaces_standalone.cpp")
HEADERS = ["version.h", "errors.h", "module_context.h", "imodule.h",
           "itransport.h", "isession.h", "igameplay.h"]


def find_compiler():
    """Prefer the project toolchain, then anything on PATH."""
    zig = os.path.join(ROOT, "tools", "toolchain", "pylibs", "ziglang", "zig.exe")
    if os.path.isfile(zig):
        return [zig, "c++"]
    for name in ("clang++", "g++", "c++"):
        p = shutil.which(name)
        if p:
            return [p]
    return None


print("=" * 68)
print("  接口层契约（T-004 验收）")
print("=" * 68)

if not os.path.isdir(IFACE):
    print(f"  [跳过] 接口目录不存在：{IFACE}")
    sys.exit(2)

missing = [h for h in HEADERS if not os.path.isfile(os.path.join(IFACE, h))]
if missing:
    print(f"  [BAD] 缺少接口头文件：{missing}")
    sys.exit(1)
print(f"  接口头文件 {len(HEADERS)} 个都在")

cc = find_compiler()
if cc is None:
    print("  [跳过] 本机没有 C++ 编译器（zig / clang++ / g++ 都找不到）")
    print("         这属于「未能验证」，不是通过。")
    sys.exit(2)
print(f"  编译器：{os.path.basename(cc[0])}")

passed = failed = 0
tmp = tempfile.mkdtemp(prefix="iface-")


def record(label, ok, detail=""):
    global passed, failed
    passed += ok
    failed += (not ok)
    print(f"  [{'OK ' if ok else 'BAD'}] {label:34} {detail}")
    return ok


# ── 1. each header compiles alone ──────────────────────────────────────────
for h in HEADERS:
    src = os.path.join(tmp, h.replace(".h", ".cpp"))
    with open(src, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(f'#include "{h}"\nint main() {{ return 0; }}\n')
    obj = os.path.join(tmp, h.replace(".h", ".o"))
    r = subprocess.run(cc + ["-std=c++20", "-c", f"-I{IFACE}", src, "-o", obj],
                       capture_output=True, text=True, encoding="utf-8",
                       errors="replace", timeout=600)
    ok = r.returncode == 0
    record(f"{h} 单独编译", ok, "" if ok else "失败")
    if not ok:
        for line in ((r.stderr or "") + (r.stdout or "")).splitlines()[:5]:
            print("        " + line.strip()[:160])

# ── 2. the full contract test builds AND runs ──────────────────────────────
if os.path.isfile(CONTRACT_CPP):
    exe = os.path.join(tmp, "contract.exe")
    r = subprocess.run(cc + ["-std=c++20", f"-I{IFACE}", CONTRACT_CPP, "-o", exe],
                       capture_output=True, text=True, encoding="utf-8",
                       errors="replace", timeout=900)
    if record("契约测试编译", r.returncode == 0, ""):
        run = subprocess.run([exe], capture_output=True, text=True,
                             encoding="utf-8", errors="replace", timeout=60)
        record("契约测试运行", run.returncode == 0,
               f"退出码={run.returncode}")
    else:
        for line in ((r.stderr or "") + (r.stdout or "")).splitlines()[:8]:
            print("        " + line.strip()[:165])
else:
    record("契约测试存在", False, f"缺 {CONTRACT_CPP}")

# ── 3. no dependency on a module or kernel header ──────────────────────────
bad = []
for h in HEADERS:
    t = open(os.path.join(IFACE, h), encoding="utf-8").read()
    for line in t.split("\n"):
        s = line.strip()
        if s.startswith('#include "') and not any(s.endswith(f'"{x}"')
                                                  for x in HEADERS):
            bad.append((h, s))
record("接口层不依赖 modules/ 或 kernel/", not bad,
       "" if not bad else f"{bad}")

print(f"\n  {passed}/{passed + failed} 符合预期")
sys.exit(0 if failed == 0 else 1)
