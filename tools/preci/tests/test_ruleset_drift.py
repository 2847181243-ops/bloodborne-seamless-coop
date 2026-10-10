#!/usr/bin/env python3
"""Negative tests for the ruleset drift check.

A drift detector that only ever says "OK" is decoration. These cases feed it
DELIBERATELY WRONG rulesets and assert it reports drift.

The scenarios are the realistic ways enforcement quietly disappears:
  * someone deletes a required check in the web UI  -> that gate stops gating
  * someone renames a job and forgets the ruleset   -> permanent Pending
  * strict is turned off                            -> stale branches merge unchecked
  * the ruleset is set to disabled/evaluate         -> nothing is enforced
  * someone adds an extra required check that never runs -> permanent Pending
  * squash-only is relaxed                          -> merge commits enter history

The check() function is exercised with the network call patched out, so this runs
offline and in CI without depending on the live ruleset state.
"""
import importlib.util
import json
import os
import sys

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location(
    "crd", os.path.join(HERE, "..", "check_ruleset_drift.py"))
crd = importlib.util.module_from_spec(spec)
spec.loader.exec_module(crd)

REQUIRED = crd.load_expected()[0]["required"]


def ruleset(name, required=None, strict=True, enforcement="active",
            extra_rules=("deletion", "non_fast_forward"),
            merge_methods=("squash",), include=("refs/heads/main",)):
    rules = [{"type": t} for t in extra_rules]
    rules.append({"type": "pull_request", "parameters": {
        "allowed_merge_methods": list(merge_methods)}})
    rules.append({"type": "required_status_checks", "parameters": {
        "strict_required_status_checks_policy": strict,
        "required_status_checks": [{"context": c} for c in
                                   (required if required is not None else REQUIRED)]}})
    return {"name": name, "enforcement": enforcement,
            "conditions": {"ref_name": {"include": list(include)}},
            "rules": rules}


def both(rs, include=None):
    """`check()` inspects BOTH protected branches.

    An earlier version of this fixture declared only `refs/heads/main`, so `check()`
    correctly reported "develop has no protecting ruleset" and the happy-path case
    failed. The gate was right; the fixture was wrong. A real ruleset covers exactly
    one branch, so each protected branch gets its own copy here.

    `include` overrides the branch each copy targets -- used by the
    "no ruleset protects this branch" case, which must ACTUALLY point elsewhere.
    Passing the branch names and then ignoring them (as an earlier version did) made
    that case silently test nothing.
    """
    out = {}
    for br in ("main", "develop"):
        d = json.loads(json.dumps(rs))          # deep copy
        name = rs["name"] if br == "main" else rs["name"].replace("main", "develop")
        d["name"] = name
        d["conditions"] = {"ref_name": {
            "include": list(include) if include else [f"refs/heads/{br}"]}}
        out[name] = d
    return out


def fake_fetch(mapping):
    """Return a fetch() replacement that serves the given payloads."""
    def _f(url, token=None):
        for key, val in mapping.items():
            if url.endswith(key):
                return 200, val
        return 200, []
    return _f


CASES = [
    ("正常配置（应通过）", both(ruleset("main-protection")), 0),
    ("少一个必需检查（应报）",
     both(ruleset("main-protection", required=REQUIRED[:-1])), 1),
    ("多一个未声明的必需检查（应报）",
     both(ruleset("main-protection", required=REQUIRED + ["某检查"])), 1),
    ("strict 关掉了（应报）",
     both(ruleset("main-protection", strict=False)), 1),
    ("enforcement 非 active（应报）",
     both(ruleset("main-protection", enforcement="evaluate")), 1),
    ("缺少 non_fast_forward（应报）",
     both(ruleset("main-protection", extra_rules=("deletion",))), 1),
    ("允许 merge commit（应报）",
     both(ruleset("main-protection", merge_methods=("squash", "merge"))), 1),
    ("没有任何规则集作用于该分支（应报）",
     both(ruleset("main-protection"), include=("refs/heads/other",)), 1),
]

# fetch() is called for the list, then once per ruleset. Patch it to serve both.
orig = crd.fetch
passed = failed = 0
print("=" * 68)
print("  规则集漂移检查 · 负例矩阵")
print("=" * 68)

for label, payload, expect_rc in CASES:
    def patched(url, token=None, _p=payload):
        if url.endswith("/rulesets"):
            return 200, [{"id": i + 1, "name": n} for i, n in enumerate(_p)]
        for i, (n, d) in enumerate(_p.items()):
            if url.endswith(f"/rulesets/{i + 1}"):
                return 200, d
        return 200, {}
    crd.fetch = patched
    rc, problems, notes = crd.check(offline=False)
    crd.fetch = orig
    ok = rc == expect_rc
    passed += ok
    failed += (not ok)
    print(f"  [{'OK ' if ok else 'BAD'}] {label:34} rc={rc} 期望={expect_rc} 问题={len(problems)}")
    if not ok:
        for p in problems[:2]:
            print(f"        {p[:130]}")
    elif expect_rc == 1 and problems:
        print(f"        → {problems[0][:120]}")

print(f"\n  {passed}/{len(CASES)} 符合预期")
sys.exit(0 if failed == 0 else 1)
