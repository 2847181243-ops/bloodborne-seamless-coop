#!/usr/bin/env python3
"""Re-test branch-policy after adding issues:read and API-failure discrimination.

The gh shim now simulates: success (prints labels) or failure (non-zero exit),
which is what distinguishes a 403 from a genuine "label missing".
"""
import os
import re
import shutil
import stat
import subprocess
import sys
import tempfile
import _paths as P  # noqa: E402  (shared location-independent paths)

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
REPO = P.ROOT
WORKFLOW = os.path.join(REPO, ".github", "workflows", "branch-policy.yml")
BASH = P.BASH
REAL_GIT = r"C:\Program Files\Git\cmd\git.exe"

lines = open(WORKFLOW, encoding="utf-8").read().split("\n")
bodies, cur, pending = [], None, False
for raw in lines:
    ln = raw.rstrip("\r")
    if pending:
        pending = False
        cur = []
        continue
    if re.match(r"^\s*run: \|\s*$", ln):
        if cur is not None:
            bodies.append("\n".join(cur))
        cur, pending = None, True
        continue
    if cur is not None:
        if ln.strip() == "":
            cur.append("")
            continue
        if re.match(r"^\s{10,}", ln):
            cur.append(ln[10:])
            continue
        bodies.append("\n".join(cur))
        cur = None
if cur is not None:
    bodies.append("\n".join(cur))
body = bodies[0]
print(f"branch-policy run body: {len(body.splitlines())} lines")

SHIM = tempfile.mkdtemp(prefix="shim2-")
gh_shim = os.path.join(SHIM, "gh")
open(gh_shim, "w", encoding="utf-8", newline="\n").write(
    """#!/usr/bin/env bash
# stub: GH_SHIM_MODE=ok -> print labels; =fail -> non-zero + error text
if [[ "${GH_SHIM_MODE:-ok}" == "fail" ]]; then
  echo "gh: Not Found (HTTP 403)" >&2
  exit 1
fi
echo "${GH_SHIM_LABELS:-}" | tr ',' '\\n' | sed '/^$/d'
exit 0
""")
os.chmod(gh_shim, os.stat(gh_shim).st_mode | stat.S_IEXEC)

git_shim = os.path.join(SHIM, "git")
open(git_shim, "w", encoding="utf-8", newline="\n").write(
    f"""#!/usr/bin/env bash
if [[ "$1" == "fetch" ]]; then exit 0; fi
exec "{REAL_GIT.replace(chr(92), '/')}" "$@"
""")
os.chmod(git_shim, os.stat(git_shim).st_mode | stat.S_IEXEC)

GATE = os.path.join(SHIM, "gate.sh")
open(GATE, "w", encoding="utf-8", newline="\n").write(
    'export PATH="%s:$PATH"\n' % SHIM + body + "\n")

CASES = [
    # name, extra out-of-scope files, PR body, gh mode, labels, expect
    ("all in scope", [], "body", "ok", "", 0),
    ("out-of-scope + cross-agent issue", ["docs/security/x.md"],
     "为什么改\n\n关联 Issue：#5", "ok", "cross-agent", 0),
    ("out-of-scope + issue lacking label", ["docs/security/x.md"],
     "关联 Issue：#5", "ok", "bug", 1),
    ("out-of-scope + no issue in body", ["docs/security/x.md"],
     "没有引用任何 issue", "ok", "cross-agent", 1),
    ("out-of-scope + empty body", ["docs/security/x.md"], "", "ok", "cross-agent", 1),
    ("out-of-scope + issues/N form", ["docs/security/x.md"],
     "见 https://github.com/o/r/issues/7", "ok", "cross-agent", 0),
    # NEW: API failure must be reported as an API problem, not "label missing"
    ("API failure (403) distinct from label", ["docs/security/x.md"],
     "关联 Issue：#5", "fail", "", 1),
    ("in-scope only, body irrelevant", [".github/x.yml"], "whatever", "ok", "", 0),
]

passed = failed = 0
fails = []
for name, extra, body_text, mode, labels, expect in CASES:
    work = tempfile.mkdtemp(prefix="bp2-")
    try:
        def run(args, cwd=work):
            return subprocess.run(args, cwd=cwd, capture_output=True, text=True,
                                  encoding="utf-8", errors="replace")

        run([REAL_GIT, "init", "-q", "-b", "main"])
        run([REAL_GIT, "config", "user.email", "t@t"])
        run([REAL_GIT, "config", "user.name", "t"])
        open(os.path.join(work, "README.md"), "w").write("base\n")
        run([REAL_GIT, "add", "-A"])
        run([REAL_GIT, "commit", "-qm", "base"])
        run([REAL_GIT, "update-ref", "refs/remotes/origin/main", "HEAD"])
        run([REAL_GIT, "checkout", "-q", "-b", "lead/test"])
        for rel in extra:
            p = os.path.join(work, rel)
            os.makedirs(os.path.dirname(p), exist_ok=True)
            open(p, "w").write("x\n")
        run([REAL_GIT, "add", "-A"])
        run([REAL_GIT, "commit", "-qm", "case"])

        env = dict(os.environ)
        env.update({"HEAD_REF": "lead/test", "BASE_REF": "main", "PRBODY": body_text,
                    "GH_SHIM_MODE": mode, "GH_SHIM_LABELS": labels,
                    "GITHUB_REPOSITORY": "owner/repo"})
        r = subprocess.run([BASH, "-e", "-o", "pipefail", GATE], cwd=work,
                           capture_output=True, text=True, encoding="utf-8",
                           errors="replace", env=env)
        out = (r.stdout or "") + (r.stderr or "")
        ok = (r.returncode == expect)
        # for the API-failure case, assert the message names the API, not the label
        if ok and mode == "fail":
            ok = ("读取 Issue" in out and "标签失败" in out)
        if ok:
            passed += 1
        else:
            failed += 1
            fails.append((name, expect, r.returncode, out))
        print(f"  [{'PASS' if ok else 'FAIL'}] {name:44} expect={expect} got={r.returncode}")
    finally:
        shutil.rmtree(work, ignore_errors=True)

dump = os.path.join(tempfile.gettempdir(), "bp2-failures.txt")
with open(dump, "w", encoding="utf-8") as fh:
    for name, expect, got, out in fails:
        fh.write(f"\n===== {name} (expect={expect} got={got}) =====\n{out}\n")
print(f"\nmatrix: {passed} passed, {failed} failed")
if failed:
    print(f"detail -> {dump}")
shutil.rmtree(SHIM, ignore_errors=True)
sys.exit(0 if failed == 0 else 1)
