#!/usr/bin/env python3
"""Code style + comment-discipline gate.

Two sources of authority, no invented standards:

  1. `.editorconfig` -- already in the repo, and until now nothing enforced it.
     Rules enforced: final newline, line endings, trailing whitespace, tabs,
     max line length (per-section).
  2. Task book §38 (`bloodbrone.markdown:1160`), verbatim:
     "禁止：为了编译通过而删除功能；为了消除报错而大面积注释代码；..."
     -> a rule against mass-commenting code to silence errors.

WHY IT SCANS `git archive HEAD` RATHER THAN THE WORKING TREE
-----------------------------------------------------------
Measured on this repo: `git ls-files --eol` reports `i/lf w/crlf` for tracked
text. A working-tree scan therefore sees CR at the end of every line and reports
trailing whitespace / wrong line endings that CI (which checks out LF) never
sees. Scanning the committed blob keeps local and CI byte-identical. This is the
same trap that produced a false red in the CODEOWNERS check.

Severity model:
  * ERROR  -- objective, no judgement: trailing whitespace, tab indent, missing
              final newline, wrong line ending, over-long line, no file header.
  * WARN   -- heuristic: plausible commented-out code. Heuristics can be wrong,
              so a bare warning does not fail the gate; a *mass* of them (or a
              large commented block) does, matching the task book's wording
              ("大面积注释代码").
"""
import argparse
import fnmatch
import io
import json
import os
import re
import subprocess
import sys
import tarfile
import tempfile

sys.stdout.reconfigure(encoding="utf-8", errors="replace")

# ---------------------------------------------------------------------------
# Which files count as source (comment rules apply) and which are prose/config.
# ---------------------------------------------------------------------------
SRC_GLOBS = ["*.c", "*.cc", "*.cpp", "*.cxx", "*.h", "*.hh", "*.hpp", "*.hxx",
             "*.ixx", "*.inl"]
# Prose/config: editorconfig still applies, comment rules do not.
TEXT_GLOBS = SRC_GLOBS + ["*.md", "*.markdown", "*.txt", "*.yml", "*.yaml",
                          "*.json", "*.toml", "*.ini", "*.cfg", "*.cmake",
                          "CMakeLists.txt", "*.ps1", "*.py", "*.sh", "*.bat",
                          "*.cmd"]
BINARY_EXT = {".png", ".jpg", ".jpeg", ".gif", ".ico", ".exe", ".dll", ".lib",
              ".pdb", ".zip", ".7z", ".pak", ".sl2", ".bin", ".obj", ".so",
              ".dylib", ".woff", ".woff2", ".ttf"}

# Files that legitimately use CRLF (per .gitattributes)
CRLF_GLOBS = ["*.bat", "*.cmd", "*.ps1"]

# Markdown has trailing whitespace disabled in .editorconfig (two spaces = <br>)
TRAILING_WS_EXEMPT = ["*.md", "*.markdown"]

MAX_LINE = 120
# Long-line grace: a single long line is usually a URL or a wide table row.
MAX_LINE_EXEMPT_HINT = re.compile(r"https?://|^\s*\|")


def git(args, cwd):
    return subprocess.run(["git"] + args, cwd=cwd, capture_output=True,
                          text=True, encoding="utf-8", errors="replace")


def load_committed_tree(repo):
    """Return {relpath: bytes} of the TRUE blob contents at HEAD.

    Two wrong ways, both measured here:

    1. Scanning the WORKING TREE sees CRLF on Windows (`git ls-files --eol`
       reports `w/crlf`) although the repo stores LF -> thousands of phantom
       "CRLF in an LF file" errors. CI checks out LF and never sees them.
    2. `git archive` looks like the fix but is NOT: archive applies the
       `.gitattributes` export filters, so a blob stored as LF is exported as
       CRLF. That produced 5441 phantom errors on this repo.

    `git cat-file --batch` returns the raw blob, byte for byte, with no
    conversion -- which is what a commit actually contains and therefore what
    the gate must judge.
    """
    listing = subprocess.run(["git", "ls-files", "-z"], cwd=repo,
                             capture_output=True)
    if listing.returncode != 0:
        raise RuntimeError("git ls-files failed")
    names = [n for n in listing.stdout.decode("utf-8", "replace").split("\0") if n]

    # `git ls-tree -r -z HEAD` gives the exact blob for each path at HEAD, so a
    # dirty working tree cannot influence the verdict either.
    ls = subprocess.run(["git", "ls-tree", "-r", "-z", "HEAD"], cwd=repo,
                        capture_output=True)
    if ls.returncode != 0:
        raise RuntimeError("git ls-tree failed")
    entries = []
    for rec in ls.stdout.decode("utf-8", "replace").split("\0"):
        if not rec:
            continue
        meta, _, path = rec.partition("\t")
        parts = meta.split()
        if len(parts) >= 3 and parts[1] == "blob":
            entries.append((parts[2], path))
    if not entries:
        return {}

    out = {}
    buf = "".join(f"{sha}\n" for sha, _ in entries).encode()
    r = subprocess.run(["git", "cat-file", "--batch"], cwd=repo, input=buf,
                       capture_output=True)
    if r.returncode != 0:
        raise RuntimeError("git cat-file failed: " +
                           (r.stderr or b"").decode("utf-8", "replace")[:200])
    data = r.stdout
    pos = 0
    for sha, path in entries:
        nl = data.index(b"\n", pos)
        header = data[pos:nl].decode("utf-8", "replace")
        size = int(header.split()[2])
        start = nl + 1
        out[path] = data[start:start + size]
        pos = start + size + 1  # skip the trailing newline
    return out


def matches(name, globs):
    base = os.path.basename(name)
    return any(fnmatch.fnmatch(name, g) or fnmatch.fnmatch(base, g) for g in globs)


def check_editorconfig(name, data, errors):
    ext = os.path.splitext(name)[1].lower()
    if ext in BINARY_EXT:
        return
    if b"\x00" in data[:4096]:
        return  # looks binary; skip
    if not matches(name, TEXT_GLOBS):
        return

    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError as e:
        errors.append(("ERROR", name, 0, f"not valid UTF-8: {e}"))
        return

    # final newline
    if text and not text.endswith("\n"):
        errors.append(("ERROR", name, len(text.splitlines()),
                       "missing final newline (editorconfig: insert_final_newline = true)"))

    crlf_ok = matches(name, CRLF_GLOBS)
    is_md = matches(name, TRAILING_WS_EXEMPT)
    long_exempt = 0

    for i, line in enumerate(text.split("\n"), 1):
        # line endings: a stray CR means CRLF in an LF file
        if line.endswith("\r"):
            if not crlf_ok:
                errors.append(("ERROR", name, i,
                               "CRLF line ending in a file declared LF "
                               "(.editorconfig end_of_line = lf; .gitattributes text)"))
            line = line[:-1]

        if not is_md and line != line.rstrip():
            errors.append(("ERROR", name, i,
                           "trailing whitespace (trim_trailing_whitespace = true)"))

        if not is_md and len(line) > MAX_LINE:
            if MAX_LINE_EXEMPT_HINT.search(line):
                long_exempt += 1
            else:
                errors.append(("ERROR", name, i,
                               f"line is {len(line)} chars > {MAX_LINE} "
                               f"(editorconfig max_line_length)"))

        # tab indentation in code/config (Makefile keeps tabs)
        if os.path.basename(name) != "Makefile" and re.match(r"^\t", line):
            errors.append(("ERROR", name, i,
                           "tab indentation (editorconfig indent_style = space)"))

    return long_exempt


# ---------------------------------------------------------------------------
# Comment discipline
# ---------------------------------------------------------------------------
# Heuristic: a commented-out LINE OF CODE. Deliberately conservative -- it must
# look like code, not like prose. Every pattern is anchored so that a sentence
# ending in a period is never mistaken for code.
CODE_LIKE = [
    re.compile(r"^\s*//\s*(if|else|for|while|switch|case|return|break|continue|"
               r"goto|try|catch|throw|do)\b.*[;{)]"),
    re.compile(r"^\s*//\s*(?:[A-Za-z_][\w:<>,*&\s]*\s+)?[A-Za-z_]\w*\s*\([^)]*\)\s*;"),
    re.compile(r"^\s*//\s*[A-Za-z_]\w*\s*(?:=|\.|->)\s*[^;]{0,80};"),
    re.compile(r"^\s*//\s*[{}]\s*$"),
    re.compile(r"^\s*//\s*#\s*(include|define|pragma)\b"),
]
TRUE_COMMENT = re.compile(r"^\s*//\s*(TODO|FIXME|HACK|XXX|NOTE|WARN|WARNING)\b")
FILE_HEADER_HINT = re.compile(r"^\s*(//|/\*)")


def check_comments(name, data, errors, warns, mass_comment_limit):
    if not matches(name, SRC_GLOBS):
        return
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError:
        return
    lines = text.split("\n")

    # 1) file header: every source file should say what it is and which Agent owns it.
    #    Required by CONTRIBUTING's ownership model (task book §48) -- a file with no
    #    stated owner is exactly how ownership silently rots.
    head = [l for l in lines[:15] if l.strip()]
    if not (head and FILE_HEADER_HINT.match(head[0])):
        errors.append(("ERROR", name, 1,
                       "missing file header comment (expected // ... stating purpose and owner)"))

    # 2) commented-out code
    hits = []
    for i, line in enumerate(lines, 1):
        if TRUE_COMMENT.match(line):
            continue  # a real TODO/FIXME marker, not dead code
        for pat in CODE_LIKE:
            if pat.match(line):
                hits.append(i)
                break
    if hits:
        # A few scattered ones are common and reviewable; the task book forbids
        # "大面积" (mass) commenting, so only a mass/contiguous block is fatal.
        contiguous = 1
        best = 1
        for a, b in zip(hits, hits[1:]):
            contiguous = contiguous + 1 if b == a + 1 else 1
            best = max(best, contiguous)
        sev = "ERROR" if (len(hits) >= mass_comment_limit or best >= 5) else "WARN"
        (errors if sev == "ERROR" else warns).append(
            (sev, name, hits[0],
             f"looks like commented-out code: {len(hits)} line(s), longest run {best}"
             + (" -- task book §38 forbids mass-commenting code to silence errors"
                if sev == "ERROR" else " (reviewable)")))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo", default=".")
    ap.add_argument("--json", action="store_true")
    ap.add_argument("--mass-comment-limit", type=int, default=15,
                    help="commented-out-code lines in one file that make it fatal")
    ap.add_argument("--strict-warn", action="store_true",
                    help="treat warnings as errors (use in CI)")
    args = ap.parse_args()

    repo = os.path.abspath(args.repo)
    tree = load_committed_tree(repo)

    errors, warns = [], []
    scanned = 0
    for name in sorted(tree):
        if name.startswith(".git"):
            continue
        if os.path.splitext(name)[1].lower() in BINARY_EXT:
            continue
        scanned += 1
        check_editorconfig(name, tree[name], errors)
        check_comments(name, tree[name], errors, warns, args.mass_comment_limit)

    if args.json:
        print(json.dumps({
            "scanned": scanned,
            "errors": [{"sev": s, "file": f, "line": l, "msg": m}
                       for s, f, l, m in errors],
            "warnings": [{"sev": s, "file": f, "line": l, "msg": m}
                         for s, f, l, m in warns],
        }, ensure_ascii=False, indent=2))
    else:
        print(f"scanned {scanned} committed files")
        for s, f, l, m in errors:
            print(f"  ! {f}:{l}: {m}")
        for s, f, l, m in warns:
            print(f"  ~ {f}:{l}: {m}")
        print(f"errors={len(errors)} warnings={len(warns)}")

    if errors:
        return 1
    if args.strict_warn and warns:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
