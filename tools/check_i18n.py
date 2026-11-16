#!/usr/bin/env python3
"""Checks that every language of each page's I18N table says the same things as English.

Same keys, same number of list items, same function arguments and the same ${values}. A missing
key would show "undefined" on the page. Run by `make test`.
"""
import re
import sys
from pathlib import Path

PAGES = [Path(__file__).resolve().parent.parent / "web" / name for name in ("index.html", "start.html")]
OPEN, CLOSE = "{[(", "}])"


def skip_string(s, i):
    quote = s[i]
    i += 1
    while s[i] != quote:
        i += 2 if s[i] == "\\" else 1
    return i + 1


def end_of_block(s, i):
    """The index just after the bracket that closes the one at s[i]."""
    depth = 0
    while True:
        c = s[i]
        if c in "'\"`":
            i = skip_string(s, i)
            continue
        if c in OPEN:
            depth += 1
        elif c in CLOSE:
            depth -= 1
            if depth == 0:
                return i + 1
        i += 1


def split_entries(body):
    """The top-level `key: value` pairs of an object's body."""
    entries, i = {}, 0
    while True:
        m = re.compile(r"\s*,?\s*(?:'([^']+)'|(\w+))\s*:\s*").match(body, i)
        if not m:
            return entries
        key, i = m.group(1) or m.group(2), m.end()
        start = i
        while i < len(body) and body[i] != ",":
            c = body[i]
            if c in "'\"`":
                i = skip_string(body, i)
            elif c in OPEN:
                i = end_of_block(body, i)
            elif c == "=" and body[i + 1] == ">":
                i += 2
            else:
                i += 1
        entries[key] = body[start:i].strip()


def languages(page):
    start = page.index("const I18N = {")
    table = page[page.index("{", start):end_of_block(page, page.index("{", start))]
    return {name: split_entries(value.strip()[1:-1]) for name, value in split_entries(table[1:-1]).items()}


def shape(value):
    """What a translation must keep from the English value, whatever the words."""
    args = re.match(r"\(?([\w, ]*)\)?\s*=>", value)
    if value.startswith("{"):
        return "object", sorted(split_entries(value[1:-1]))
    if value.startswith("["):
        items = 0
        i = 1
        while i < len(value) - 1:
            if value[i] in "'\"`":
                i = skip_string(value, i)
                items += 1
            else:
                i += 1
        return "list", items
    return ("function" if args else "text", args.group(1).replace(" ", "") if args else "",
            sorted(set(re.findall(r"\$\{([^}]+)\}", value))))


def check(page):
    table = languages(page.read_text(encoding="utf-8"))
    english = table["en"]
    problems = []
    for name, lang in table.items():
        for key in english.keys() - lang.keys():
            problems.append(f"{name}: {key} is missing")
        for key in lang.keys() - english.keys():
            problems.append(f"{name}: {key} is not in English")
        for key in english.keys() & lang.keys():
            if shape(english[key]) != shape(lang[key]):
                problems.append(f"{name}: {key} differs: {shape(lang[key])} vs English {shape(english[key])}")
    for problem in problems:
        print(f"FAIL: {page.name}: {problem}")
    print(f"check_i18n: {page.name}: {len(table)} languages, {len(english)} keys, {len(problems)} problems")
    return len(problems)


def main():
    return 1 if sum(check(page) for page in PAGES) else 0


if __name__ == "__main__":
    sys.exit(main())
