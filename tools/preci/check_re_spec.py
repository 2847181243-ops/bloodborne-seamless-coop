#!/usr/bin/env python3
"""Machine checks for docs/re/ function records.

What this DOES check (format and self-consistency only):
  R-1  every `### <FunctionName>` record declares Confidence, from the fixed set
  R-2  Evidence is present and non-empty
  R-3  a `Confirmed` claim cites at least one checkable evidence class
       (反汇编 / 交叉引用 / 运行时 / 实验) -- this is what stops "looks right to me"
       being labelled Confirmed
  R-4  each file that contains records also carries a complete version binding

What this explicitly does NOT check (and must not be claimed to):
  * whether an address really is that function
  * whether Purpose is correct
  * whether the quoted disassembly is genuine
  * whether the Signature matches game version 1.09

Those need runtime verification, not a linter. Saying otherwise would manufacture
exactly the false confidence this file exists to prevent.
"""
import re
import sys

CONFIDENCE = ("Confirmed", "Probable", "Hypothesis", "Unknown")
EVIDENCE_CLASSES = ("反汇编", "交叉引用", "运行时", "实验")

# A record starts at "### <name>" and runs to the next heading of any level.
REC_RE = re.compile(r"^### +(.+?)\s*$", re.M)

# Files that follow the function-record convention. README is the spec itself and
# holds no records; addresses/structures hold tables, not function blocks.
RECORD_FILES = ("addresses.md", "structures.md", "findings.md")


def _field(block, name):
    """Return the value cell of a `| Field | value |` row, or None."""
    for line in block.split("\n"):
        m = re.match(r"\s*\|?\s*" + re.escape(name) + r"\s*\|(.*?)\|?\s*$", line)
        if m:
            return m.group(1).strip()
    return None


def check_re_record(rel, text, errors, warns):
    records = []
    parts = REC_RE.split(text)
    # split() yields [pre, name1, body1, name2, body2, ...]
    for i in range(1, len(parts) - 1, 2):
        records.append((parts[i].strip(), parts[i + 1]))

    if not records:
        return 0

    # R-4: version binding must exist once per file that has records
    has_ver = ("游戏版本" in text and "平台" in text
               and "Mod 版本" in text and "Signature" in text)
    if not has_ver:
        errors.append(("ERROR", rel, 0,
                       "有函数记录但缺少完整的版本绑定块（需含 游戏版本 / 平台 / "
                       "Mod 版本 / Signature）。地址不绑版本 = 游戏一更新全部作废且无人知道。"
                       "格式见 docs/re/README.md 第 2 节"))

    for name, body in records:
        conf = _field(body, "Confidence")
        ev = _field(body, "Evidence")

        # R-1
        if not conf:
            errors.append(("ERROR", rel, 0,
                           f"「{name}」没有 Confidence 字段。不写等级就无法区分"
                           f"「确认过」与「猜的」—— 这是本文件最重要的一条规则。"))
            continue
        val = conf.strip("`* ")
        if val not in CONFIDENCE:
            errors.append(("ERROR", rel, 0,
                           f"「{name}」的 Confidence 取值非法：'{val}'。"
                           f"只能是 {' / '.join(CONFIDENCE)}。"))

        # R-2
        if not ev or ev.strip("`*|- ") == "":
            errors.append(("ERROR", rel, 0,
                           f"「{name}」的 Evidence 为空。没有证据的记录不该存在。"))
            continue

        # R-3
        if val == "Confirmed":
            if not any(c in ev for c in EVIDENCE_CLASSES):
                errors.append(("ERROR", rel, 0,
                               f"「{name}」声明 Confirmed，但 Evidence 里没有提到任何一类"
                               f"可核对的证据（{' / '.join(EVIDENCE_CLASSES)}）。"
                               f"没证据就写 Confirmed 是自相矛盾 —— 要么补证据，要么改成 "
                               f"Probable / Hypothesis / Unknown。"))
    return len(records)


def check_re_docs(tree, errors, warns):
    total = 0
    for rel in sorted(tree):
        if not rel.startswith("docs/re/"):
            continue
        base = rel.rsplit("/", 1)[-1]
        if base not in RECORD_FILES:
            continue
        try:
            text = tree[rel].decode("utf-8")
        except UnicodeDecodeError:
            continue
        total += check_re_record(rel, text, errors, warns)
    if total:
        warns.append(("WARN", "docs/re/", 0,
                      f"已校验 {total} 条逆向函数记录的格式与自洽性。"
                      f"⚠️ 这不代表结论正确 —— 正确性只能靠运行时验证。"))
    return total
