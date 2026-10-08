#!/usr/bin/env python3
"""Fetch a PR's body and labels for pre-CI stage 2.

Separate process on purpose. Every attempt to do this inside Windows PowerShell
5.1 tripped over the same class of failure:

  * Invoke-RestMethod -> "cannot convert PSCustomObject to Int32" on nested collections
  * [System.Net.WebRequest] -> worked standalone, same conversion inside the script
  * curl.exe + ConvertFrom-Json -> ArgumentTransformationMetadataException at the
    ConvertFrom-Json call

Rather than keep guessing at PowerShell's object marshalling, the fetch lives here,
where it is trivially testable, and the caller only has to read two files.

Usage:
  python fetch_pr.py <pr-number> <out-dir>

Writes <out-dir>/pr<N>-body.md and <out-dir>/pr<N>-labels.txt
Exit codes: 0 ok, 3 no token / unusable response (caller should SKIP), 1 hard error.

Token resolution order: GITHUB_TOKEN, GH_TOKEN, then a couple of conventional
local paths. The path is never hard-coded as the only source.
"""
import json
import os
import subprocess
import sys

sys.stdout.reconfigure(encoding="utf-8", errors="replace")

REPO = "2847181243-ops/bloodborne-seamless-coop"
# GitHub's real API address. Some environments (corporate MITM, local hosts-file
# redirection) break TLS to the default address, so allow an explicit override.
API_IP = os.environ.get("PRCI_API_IP", "").strip()


def token():
    for v in ("GITHUB_TOKEN", "GH_TOKEN"):
        t = os.environ.get(v)
        if t:
            return t.strip()
    home = os.environ.get("USERPROFILE") or os.path.expanduser("~")
    for c in (os.path.join(home, ".config", "dsh", "token.txt"),
              os.path.join(home, ".config", "gh", "token.txt")):
        if os.path.isfile(c):
            return open(c, encoding="ascii", errors="replace").read().strip()
    return None


def main(argv):
    if len(argv) < 3:
        print("usage: fetch_pr.py <pr-number> <out-dir>")
        return 1
    pr = argv[1]
    out_dir = argv[2]
    os.makedirs(out_dir, exist_ok=True)

    tok = token()
    if not tok:
        print("SKIP: no GitHub token available (set GITHUB_TOKEN or GH_TOKEN)")
        return 3

    curl = os.path.join(os.environ.get("SystemRoot", r"C:\Windows"),
                        "System32", "curl.exe")
    if not os.path.isfile(curl):
        curl = "curl"
    tmp = os.path.join(out_dir, f"pr{pr}.json")
    url = f"https://api.github.com/repos/{REPO}/pulls/{pr}"
    cmd = [curl, "-sS", "--max-time", "45"]
    if API_IP:
        cmd += ["--resolve", f"api.github.com:443:{API_IP}"]
    cmd += ["-H", f"Authorization: Bearer {tok}",
            "-H", "User-Agent: preci",
            "-H", "Accept: application/vnd.github+json",
            "-o", tmp, "-w", "%{http_code}", url]
    r = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8",
                       errors="replace")
    http = (r.stdout or "").strip().splitlines()
    http = http[-1] if http else "000"
    if not os.path.isfile(tmp) or os.path.getsize(tmp) == 0:
        print(f"SKIP: empty response (http={http}) {r.stderr[:200]}")
        return 3
    try:
        data = json.load(open(tmp, encoding="utf-8"))
    except json.JSONDecodeError as e:
        print(f"SKIP: response is not JSON (http={http}): {str(e)[:160]}")
        return 3
    if "body" not in data:
        print(f"SKIP: unexpected response (http={http}): {str(data)[:200]}")
        return 3

    body_path = os.path.join(out_dir, f"pr{pr}-body.md")
    with open(body_path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(data.get("body") or "")
    labels = ",".join(l.get("name", "") for l in (data.get("labels") or []))
    open(os.path.join(out_dir, f"pr{pr}-labels.txt"), "w",
         encoding="utf-8", newline="\n").write(labels)
    head = (data.get("head") or {}).get("ref", "")
    print(f"OK http={http} head={head} labels=[{labels}] "
          f"body={len(data.get('body') or '')} chars")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
