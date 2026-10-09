#!/usr/bin/env python3
"""Code style + comment-discipline gate.

Two sources of authority, no invented standards:

  1. `.editorconfig` -- already in the repo, and until now nothing enforced it.
     Rules enforced: final newline, line endings, trailing whitespace, tabs,
     max line length (per-section).
  2. Task book §38 (`bloodborne.markdown:1160`), verbatim:
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
              ".dylib", ".woff", ".woff2", ".ttf",
              # compiled Python: binary, and it slipped into a commit once
              ".pyc", ".pyo"}

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
#
# NOTE: re.M is required. With plain re.match() the `^` anchor matches only the
# START OF THE WHOLE STRING, so the loop below ran on line 1 and nothing else --
# 20 lines of commented-out code sailed straight through. Found by the negative
# matrix, not by reading the code.
CODE_LIKE = [
    re.compile(r"^\s*//\s*(if|else|for|while|switch|case|return|break|continue|"
               r"goto|try|catch|throw|do)\b.*[;{)]", re.M),
    # function call, with an optional return type:  // int f(x);  |  // f(x);
    re.compile(r"^\s*//\s*(?:\w[\w:<>,*&\s]*\s+)?\w+\s*\([^)]*\)\s*;", re.M),
    # assignment, with an optional type:  // int value0 = compute(0);
    # An earlier version allowed only ONE identifier before `=`, so the extremely
    # common "type name = ..." form never matched. Found by the negative matrix.
    re.compile(r"^\s*//\s*(?:\w[\w:<>,*&\s]*\s+)?\w+\s*=(?!=)\s*[^;]{0,80};", re.M),
    re.compile(r"^\s*//\s*[{}]\s*$", re.M),
    re.compile(r"^\s*//\s*#\s*(include|define|pragma)\b", re.M),
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
            # .search, not .match: the patterns are anchored with ^ and compiled
            # with re.M, so search is what tests each line correctly. Using
            # .match here only ever examined line 1.
            if pat.search(line):
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


def _cmake_call_args(text, command, target):
    """Concatenate the arguments of EVERY `command(target ...)` call.

    Two traps found by the negative tests, both silent:

    1. A non-greedy regex `(.*?\\)` stops at the first ')' -- which here lands
       inside a generator expression like `$<$<COMPILE_LANGUAGE:CXX>:/permissive->`,
       truncating the option list.
    2. Even with correct paren balancing, reading only the FIRST call is wrong:
       this file expresses the standard as several separate
       `target_compile_options(bbcoop_warnings INTERFACE ...)` calls, so the first
       one holds only `/W4 /WX` and every later flag looked absent
       (baseline reported 4 phantom errors).

    So: balance parens AND accumulate across all matching calls.
    """
    pat = re.compile(r"\b" + re.escape(command) + r"\s*\(\s*" + re.escape(target) + r"\b")
    parts = []
    for m in pat.finditer(text):
        i = text.index("(", m.start())
        depth = 0
        for j in range(i, len(text)):
            c = text[j]
            if c == "(":
                depth += 1
            elif c == ")":
                depth -= 1
                if depth == 0:
                    parts.append(text[i + 1:j])
                    break
    return "\n".join(parts) if parts else None


def check_re_docs(tree, errors, warns):
    """Delegate the docs/re/ function-record checks to their own module.

    Kept separate because that module has a distinctly different job: it validates a
    DOCUMENT FORMAT, not code style. It also carries an explicit list of what it does
    NOT check, which is important enough to live next to the rules themselves.
    """
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    try:
        import check_re_spec
    except ImportError as e:  # pragma: no cover - only if the file goes missing
        warns.append(("WARN", "tools/preci/check_re_spec.py", 0,
                      f"无法导入逆向文档校验模块：{e}"))
        return 0
    return check_re_spec.check_re_docs(tree, errors, warns)


def check_constraint_severity(tree, errors, warns):
    """Require a severity tier on every security constraint.

    Why: all 19 constraints in docs/security/constraints.md carry the identical
    `违反后果：FAIL`. That loses the distinction that matters -- violating "no
    persistent identifier" cannot be undone, while "missing packet size limit" is
    routine rework. With one value, nobody can tell how careful to be.

    Reported as a WARNING, not an error, on purpose:
      * hard-failing every PR until AI-C2 lands the change would block unrelated
        work, and people would learn to route around the gate;
      * a warning stays visible and reports exactly how many entries are missing,
        so the debt is concrete rather than a sentence in a document.

    Upgrade to an error once docs/security/constraints.md is compliant. That takes a
    deliberate edit, so the escalation cannot happen by accident.
    """
    rel = "docs/security/constraints.md"
    if rel not in tree:
        return
    try:
        text = tree[rel].decode("utf-8")
    except UnicodeDecodeError:
        return

    blocks = re.split(r"^### ", text, flags=re.M)[1:]
    tiers = ("致命", "严重", "普通")
    total = 0
    missing = []
    no_reason = []
    for b in blocks:
        head = b.split("\n", 1)[0].strip()
        m = re.match(r"(C-\d+)", head)
        if not m:
            continue
        total += 1
        cid = m.group(1)
        cons = None
        for line in b.split("\n"):
            mm = re.search(r"违反后果\*{0,2}\s*[:：]\s*(.+)$", line)
            if mm:
                cons = mm.group(1).strip()
                break
        if not cons or not cons.startswith(tiers):
            missing.append(cid)
            continue
        # The level alone is not enough -- it must say WHY in one sentence.
        rest = cons
        for t in tiers:
            if rest.startswith(t):
                rest = rest[len(t):]
                break
        rest = rest.lstrip(" ——-－:：")
        if len(rest) < 6:
            no_reason.append(cid)

    if not total:
        return
    # The file must also describe the tiers, otherwise a reader cannot interpret them.
    if "致命" not in text or "严重" not in text:
        warns.append(("WARN", rel, 0,
                      "缺少三级（致命/严重/普通）的定义说明 —— 级别无法被解释。"
                      "要求见 docs/standards/handoff-约束分级.md"))
    if missing:
        warns.append(("WARN", rel, 0,
                      f"{len(missing)}/{total} 条约束的「违反后果」未按三级标注："
                      f"{', '.join(missing[:8])}{' …' if len(missing) > 8 else ''}。"
                      f"当前全是同一个 FAIL，无法区分「不可逆」与「可返工」。"
                      f"判定与理由见 docs/standards/handoff-约束分级.md"))
    if no_reason:
        warns.append(("WARN", rel, 0,
                      f"{len(no_reason)} 条只写了级别没写理由：{', '.join(no_reason[:8])}。"
                      f"级别是分类，理由才是判据。"))


def check_build_standards(tree, errors):
    """Refuse silent removal of the compiler standards.

    Rationale: CMakeLists.txt already FATAL_ERRORs at configure time if /W4 or /WX
    go missing -- but that only fires when someone actually configures a build.
    On a machine with no cmake (like this one) nothing would catch it until CI.
    Checking the flags textually means the pre-CI catches it immediately.

    This is a META-check: it protects the tool that protects the build.
    """
    p = "CMakeLists.txt"
    if p not in tree:
        return
    try:
        raw = tree[p].decode("utf-8")
    except UnicodeDecodeError:
        return

    # Parse ONLY the argument list of target_compile_options(bbcoop_warnings ...).
    #
    # A whole-file substring search is self-defeating here: the flags we look for
    # also appear in (a) this file's explanatory comments, (b) CMake's own
    # configure-time assertion messages ("...里找不到 /WX"), and (c)
    # message(STATUS ...) output. With those present, deleting the real option
    # still leaves the substring in the file, so the check could never fire.
    # Measured: the negative test "delete /WX" reported 0 errors for exactly this
    # reason.
    # The option lists contain generator expressions like
    #     $<$<COMPILE_LANGUAGE:CXX>:/permissive->
    # whose own ')' terminates any non-greedy regex match, truncating the list so
    # flags after that point are never inspected. Two regex attempts failed this
    # way (the baseline case reported 4 phantom errors); paren balancing is used
    # instead -- see _cmake_call_args.
    opts = _cmake_call_args(raw, "target_compile_options", "bbcoop_warnings")
    if opts is None:
        errors.append(("ERROR", p, 0,
                       "CMakeLists.txt 里找不到 bbcoop_warnings 的 target_compile_options —— "
                       "编译标准应集中在这个接口目标上定义，不要各模块自行决定。"))
        return

    REQUIRED = {
        "/W4": "警告等级 4",
        "/WX": "警告即错误（零警告的真正执行者）",
        "/permissive-": "标准符合性（关闭 MSVC 宽松解析）",
        "/utf-8": "源码与执行字符集按 UTF-8（防 GBK 误码）",
        "/external:W0": "第三方头文件不产生警告",
        "/external:anglebrackets": "外部头文件按 <> 形式识别",
    }
    for flag, why in REQUIRED.items():
        if flag not in opts:
            errors.append(("ERROR", p, 0,
                           f"编译标准被削弱：bbcoop_warnings 的编译选项里找不到 {flag}"
                           f"（{why}）。写进注释或断言消息都不算。"
                           f"改之前先读 docs/standards/build-standards.md。"))

    # 链接期加固
    lopts = _cmake_call_args(raw, "target_link_options", "bbcoop_warnings")
    if lopts is None:
        errors.append(("ERROR", p, 0, "找不到 bbcoop_warnings 的 target_link_options（链接期加固）"))
    else:
        for flag, why in {"/guard:cf": "控制流保护", "/DYNAMICBASE": "ASLR",
                          "/NXCOMPAT": "数据执行保护", "/HIGHENTROPYVA": "64 位高位随机化"}.items():
            if flag not in lopts:
                errors.append(("ERROR", p, 0,
                               f"链接期加固被削弱：找不到 {flag}（{why}）"))

    # The self-assert must stay, otherwise removing it also removes the safety net.
    if "FATAL_ERROR" not in raw:
        errors.append(("ERROR", p, 0,
                       "CMakeLists.txt 缺少配置阶段自检（message(FATAL_ERROR ...)）—— "
                       "没有它，标准被删掉时构建不会立刻失败。"))
    # Docs must exist: a standard nobody can read is not a standard.
    doc = "docs/standards/build-standards.md"
    if doc not in tree:
        errors.append(("ERROR", doc, 0, "缺少编译标准文档（被 CMakeLists.txt 引用）"))


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

    check_build_standards(tree, errors)
    check_constraint_severity(tree, errors, warns)
    check_re_docs(tree, errors, warns)

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
