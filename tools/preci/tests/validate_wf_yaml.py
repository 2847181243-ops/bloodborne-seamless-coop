#!/usr/bin/env python3
"""Validate every workflow with a real YAML parser + structural invariants.

Structural checks that matter for this repo (each maps to a documented GitHub
behaviour):
  * no workflow-level `paths:` on a workflow whose job is a required check
    -> documented: filtered workflows leave checks Pending forever
  * every job has `runs-on`
  * every `needs:` target exists in the same workflow
  * required-check contexts (from tools/preci/gates.yml) match actual job `name:`
  * the aggregate `gate` job uses `if: always()`
"""
import os
import sys
import tempfile

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
_local_yaml = os.path.join(tempfile.gettempdir(), "pyyaml")
if os.path.isdir(_local_yaml):
    sys.path.insert(0, _local_yaml)  # optional local PyYAML, absent on CI
import yaml  # noqa: E402
import _paths as P  # noqa: E402  (shared location-independent paths)

WF = P.path(".github", "workflows")
# Workflows that BACK a required status check. Path filtering on these would leave
# the check permanently Pending (documented GitHub behaviour).
REQUIRED_BACKING_WORKFLOWS = {
    "docs-structure.yml", "branch-policy.yml", "pr-guard.yml",
    "repo-integrity.yml", "build.yml",
}
notes = []
problems = []
print(f"PyYAML {yaml.__version__}\n")
for fn in sorted(os.listdir(WF)):
    if not fn.endswith((".yml", ".yaml")):
        continue
    p = os.path.join(WF, fn)
    try:
        d = yaml.safe_load(open(p, encoding="utf-8"))
    except yaml.YAMLError as e:
        problems.append(f"{fn}: YAML 解析失败: {e}")
        print(f"  [BAD ] {fn}: YAML 解析失败")
        continue

    jobs = d.get("jobs") or {}
    names = {}
    for jn, j in jobs.items():
        if not isinstance(j, dict):
            problems.append(f"{fn}: job {jn} 不是映射")
            continue
        if "runs-on" not in j and "uses" not in j:
            problems.append(f"{fn}: job {jn} 缺 runs-on")
        names[jn] = j.get("name", jn)
        # needs may be a scalar string; iterating it would split into characters
        # (an earlier version reported needs 'd','e','t','e','c','t' for "detect").
        nd = j.get("needs")
        deps = []
        if isinstance(nd, str):
            deps = [nd]
        elif isinstance(nd, list):
            deps = nd
        for dd in deps:
            if dd not in jobs:
                problems.append(f"{fn}: job {jn} 的 needs '{dd}' 不存在")
        if jn == "gate" and str(j.get("if", "")).strip() != "always()":
            problems.append(f"{fn}: gate job 必须用 if: always()（官方文档：依赖失败 job 的 job 可能不阻断合并）")

    # workflow-level path filtering.
    # PyYAML implements YAML 1.1, where the bare key `on` parses as the boolean
    # True -- so d.get('on') silently returns None and triggers look empty.
    on = d.get("on", d.get(True))
    trigs = []
    if isinstance(on, dict):
        trigs = list(on.keys())
    elif isinstance(on, str):
        trigs = [on]
    elif isinstance(on, list):
        trigs = on
    for t in ("pull_request", "push"):
        conf = on.get(t) if isinstance(on, dict) else None
        if isinstance(conf, dict) and ("paths" in conf or "paths-ignore" in conf):
            # The Pending trap only bites workflows that BACK a required check.
            # labels.yml legitimately path-filters a push-only workflow and is not
            # (and must not be) a required check, so it is not a problem.
            if fn in REQUIRED_BACKING_WORKFLOWS:
                problems.append(
                    f"{fn}: {t} 使用了 paths/paths-ignore，而该 workflow 支撑必需检查 —— "
                    f"官方文档：被过滤的 workflow 其检查会永久 Pending，不可作为必需检查")
            else:
                notes.append(f"{fn}: {t} 有 paths 过滤，但该 workflow 不支撑必需检查 → 合规")

    print(f"  [OK  ] {fn:22} jobs={len(jobs)} triggers={trigs} "
          f"names={[names[k] for k in names][:3]}")

# required contexts vs actual job names
import re
gates = open(P.path("tools", "preci", "gates.yml"), encoding="utf-8").read()
req = re.findall(r"^  - (.+)$", gates.split("required_status_checks:")[1]
                 .split("never_require:")[0], re.M)
allnames = set()
for fn in sorted(os.listdir(WF)):
    if not fn.endswith(".yml"):
        continue
    try:
        d = yaml.safe_load(open(os.path.join(WF, fn), encoding="utf-8"))
    except Exception:
        continue
    for jn, j in (d.get("jobs") or {}).items():
        if isinstance(j, dict):
            allnames.add(j.get("name", jn))
print("\n=== gates.yml 声称的必需上下文 vs 实际存在的 job name ===")
for r in req:
    ok = r in allnames
    print(f"  [{'OK ' if ok else 'MISSING'}] {r}")
    if not ok:
        problems.append(f"gates.yml 的必需上下文 '{r}' 在任何 workflow 中都不存在")

print("\n=== 实际全部 job name ===")
for n in sorted(allnames):
    print(f"   {n}")
print(f"\n  另外发现 gate job —— 若改为只 require 'gate'，需要它先跑过一次（7 天规则）")

if notes:
    print("\n=== 说明（非问题） ===")
    for n in notes:
        print("  -", n)
print(f"\n问题数: {len(problems)}")
for x in problems:
    print("  !", x)
sys.exit(1 if problems else 0)
