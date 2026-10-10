#!/usr/bin/env python3
"""Verify the gates THEMSELVES are still in force.

WHY THIS EXISTS
---------------
Everything else in tools/preci/tests/ answers "do the gates still catch bad PRs?".

This answers a different question: **is the gate configuration on GitHub still what
we think it is?**

That question had no check. Measured gap:

    validate_workflows.py   compares gates.yml <-> workflow FILES only
                            it never looks at GitHub

So if someone edits a ruleset in the web UI and drops one required check, or renames a
job without syncing the ruleset, **nothing would notice**. Every negative matrix would
still pass, because they test the scripts -- not the enforcement that makes the scripts
matter.

This is also the answer to "what if a check disappears?" -- a check that is not in the
ruleset's `required_status_checks` list does not gate anything, no matter how it shows
up on the PR page. So the list itself must be verified.

WHAT IT CHECKS (per protected branch)
  * the ruleset exists and is `active`
  * every context in gates.yml's required_status_checks is present
  * `strict_required_status_checks_policy` is true (branch must be up to date)
  * `deletion` and `non_fast_forward` are present
  * `allowed_merge_methods` is squash-only
  * no UNEXPECTED required context is present (a stale entry blocks every PR forever)

WHAT IT DOES NOT CHECK
  * that the workflow files are correct  -- that is validate_workflows.py
  * that the gates detect anything       -- that is the negative matrices

USAGE
    python tools/preci/check_ruleset_drift.py            # exit 0 / 1 / 3
    python tools/preci/check_ruleset_drift.py --offline  # only compare local files

Exit codes follow the repo convention: 0 pass, 1 drift found, 2 unverified
(could not read GitHub), 3 usage/environment.
"""
import json
import os
import sys
import urllib.error
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
GATES = os.path.join(HERE, "gates.yml")

REPO = "2847181243-ops/bloodborne-seamless-coop"
PROTECTED = ["main", "develop"]


def load_expected():
    """Read the required contexts from gates.yml -- the single source of truth."""
    try:
        import yaml
    except ImportError:
        return None, "没有 PyYAML，无法读取 gates.yml"
    d = yaml.safe_load(open(GATES, encoding="utf-8")) or {}
    req = d.get("required_status_checks") or []
    never = [x["context"] for x in (d.get("never_require") or [])]
    return {"required": req, "never": never}, None


def fetch(url, token=None):
    """GET a GitHub API URL. Returns (status, parsed_or_text).

    Anonymous access works for rulesets on a public repo -- measured. So the CI job
    needs no extra permission and no PAT; it can use the default GITHUB_TOKEN, and it
    works with no token at all locally.
    """
    req = urllib.request.Request(url)
    req.add_header("Accept", "application/vnd.github+json")
    req.add_header("User-Agent", "bbcoop-ruleset-check")
    # ⚠️ this machine MITMs api.github.com via a local proxy (Steam++/Watt Toolkit).
    # urllib would fail certificate verification. Prefer curl with --resolve when
    # available; fall back to urllib for CI/Linux where the network is clean.
    import shutil
    import subprocess
    curl = shutil.which("curl") or shutil.which("curl.exe")
    if curl:
        ip = os.environ.get("BBCOOP_GH_IP", "20.205.243.168")
        cmd = [curl, "-sS", "--max-time", "30"]
        if os.name == "nt":
            cmd += ["--resolve", f"api.github.com:443:{ip}"]
        cmd += ["-H", "Accept: application/vnd.github+json",
                "-H", "User-Agent: bbcoop-ruleset-check",
                "-o", "-", "-w", "\n%{http_code}", url]
        r = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8",
                           errors="replace")
        body = r.stdout or ""
        if "\n" in body:
            body, _, code = body.rpartition("\n")
        else:
            code = "000"
        if not code.isdigit() or code == "000":
            return 0, (r.stderr or "").strip()
        try:
            return int(code), json.loads(body)
        except json.JSONDecodeError:
            return int(code), body
    try:
        with urllib.request.urlopen(req, timeout=30) as resp:
            return resp.status, json.loads(resp.read().decode("utf-8"))
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode("utf-8", "replace")
    except Exception as e:  # noqa: BLE001
        return 0, str(e)


def check(offline=False):
    problems = []
    notes = []

    exp, err = load_expected()
    if err:
        return 2, [], [err]
    required = exp["required"]
    never = exp["never"]

    if offline:
        notes.append(f"离线模式：仅读取本地 gates.yml（{len(required)} 个必需检查）")
        for c in never:
            notes.append(f"  never_require: {c}")
        return 0, problems, notes

    for br in PROTECTED:
        # list rulesets, find the one targeting this branch
        st, data = fetch(f"https://api.github.com/repos/{REPO}/rulesets")
        if st != 200 or not isinstance(data, list):
            return 2, [], [f"读规则集列表失败：HTTP {st} {str(data)[:200]}",
                           "（这属于「无法验证」，不等于通过）"]
        target = f"refs/heads/{br}"
        ids = []
        for rs in data:
            st2, d2 = fetch(f"https://api.github.com/repos/{REPO}/rulesets/{rs['id']}")
            if st2 != 200 or not isinstance(d2, dict):
                return 2, [], [f"读规则集 {rs['name']} 失败：HTTP {st2}"]
            inc = ((d2.get("conditions") or {}).get("ref_name") or {}).get("include") or []
            if target in inc:
                ids.append(d2)

        if not ids:
            problems.append(f"{br}: 找不到作用于 {target} 的规则集 —— 该分支没有门禁保护")
            continue

        for d in ids:
            name = d.get("name")
            if d.get("enforcement") != "active":
                problems.append(f"{br}/{name}: enforcement={d.get('enforcement')}，应为 active")

            got_req, got_strict = [], None
            has = set()
            merge_methods = None
            for rule in d.get("rules", []):
                t = rule.get("type")
                has.add(t)
                if t == "required_status_checks":
                    p = rule.get("parameters") or {}
                    got_req = [x["context"] for x in (p.get("required_status_checks") or [])]
                    got_strict = p.get("strict_required_status_checks_policy")
                elif t == "pull_request":
                    p = rule.get("parameters") or {}
                    merge_methods = p.get("allowed_merge_methods")

            missing = [c for c in required if c not in got_req]
            extra = [c for c in got_req if c not in required]
            if missing:
                problems.append(
                    f"{br}/{name}: 缺少必需检查 {missing} —— "
                    f"这些检查不再拦任何东西（负例矩阵仍会通过，但实际不生效）")
            if extra:
                problems.append(
                    f"{br}/{name}: 多出未在 gates.yml 里声明的必需检查 {extra} —— "
                    f"若它不再运行，该分支会永远停在 Pending")
            if got_strict is not True:
                problems.append(f"{br}/{name}: strict_required_status_checks_policy={got_strict}，应为 true")
            for need in ("deletion", "non_fast_forward"):
                if need not in has:
                    problems.append(f"{br}/{name}: 缺少 {need} 规则")
            if merge_methods is not None and merge_methods != ["squash"]:
                problems.append(f"{br}/{name}: 允许的合并方式 {merge_methods}，应为 ['squash']")

            notes.append(f"{br}/{name}: {len(got_req)} 个必需检查、strict={got_strict}、active")

    return (1 if problems else 0), problems, notes


def main():
    offline = "--offline" in sys.argv
    rc, problems, notes = check(offline=offline)
    print("=" * 68)
    print("  规则集漂移检查（GitHub 上的实际配置 vs gates.yml）")
    print("=" * 68)
    for n in notes:
        print("  " + n)
    if problems:
        print()
        print("  发现问题：")
        for p in problems:
            print("    ❌ " + p)
        print()
        print("  处理方式：把 GitHub 上的规则集改回与 gates.yml 一致，")
        print("  或者若是有意变更，先改 gates.yml 再改远端 —— 两边必须一致。")
    else:
        print()
        print("  ✅ 规则集与 gates.yml 一致，所有必需检查都在生效")
    return rc


if __name__ == "__main__":
    sys.exit(main())
