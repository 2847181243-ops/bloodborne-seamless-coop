#!/usr/bin/env python3
"""Extract workflow run-bodies tagged by job name.

Index-based extraction is fragile: inserting a step shifts every later index.
This helper parses job names so tests can ask for "<job>" by name.
"""
import re
import os
import sys
import _paths as P  # noqa: E402  (shared location-independent paths)


def parse_jobs(path):
    """Return {job_name: [run_body, ...]} with 10-space de-indentation.

    Only keys after the top-level `jobs:` line are treated as job names —
    `on:` also has 2-space children (e.g. `pull_request:`) that are not jobs.
    """
    lines = open(path, encoding="utf-8").read().split("\n")
    jobs = {}
    cur_job = None
    cur = None
    pending = False
    in_jobs = False

    def flush():
        if cur is not None and cur_job:
            jobs[cur_job].append("\n".join(cur))

    for raw in lines:
        ln = raw.rstrip("\r")
        if re.match(r"^jobs:\s*$", ln):
            in_jobs = True
            continue
        if in_jobs and not pending:
            m_job = re.match(r"^  ([A-Za-z0-9_-]+):\s*$", ln)
            if m_job:
                # close any body that was still open, then start the new job
                flush()
                cur = None
                cur_job = m_job.group(1)
                jobs.setdefault(cur_job, [])
                continue
        if pending:
            pending = False
            cur = []
            continue
        if re.match(r"^\s*run: \|\s*$", ln):
            flush()
            cur, pending = None, True
            continue
        if cur is not None:
            if ln.strip() == "":
                cur.append("")
                continue
            if re.match(r"^\s{10,}", ln):
                cur.append(ln[10:])
                continue
            # a non-indented line ends the body; the job-name check above will
            # pick up the new job on the next iteration. Do NOT flush twice.
            flush()
            cur = None
    flush()
    return jobs


if __name__ == "__main__":
    import os
    import sys
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    WF = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__)))), ".github", "workflows", "pr-guard.yml")
    j = parse_jobs(WF)
    for name, bodies in j.items():
        print(f"{name}: {len(bodies)} run-body(ies)")
        for i, b in enumerate(bodies):
            first = next((l for l in b.splitlines() if l.strip()), "")
            print(f"   [{i}] {len(b.splitlines()):>3} lines | {first[:70]}")
