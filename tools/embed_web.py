#!/usr/bin/env python3
"""Embed a web asset into a C header as a NUL-terminated byte array.

Usage: embed_web.py <input file> <output header> <C symbol>
The header defines `static const char <symbol>[]` and `<SYMBOL>_LEN` (without the NUL).
"""
import sys


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    src, dst, sym = sys.argv[1:]
    data = open(src, "rb").read()
    lines = []
    for off in range(0, len(data), 16):
        lines.append("    " + ", ".join(f"0x{b:02x}" for b in data[off:off + 16]) + ",")
    guard = f"{sym.upper()}_H"
    with open(dst, "w", newline="\n") as out:
        out.write(f"/* Generated from {src} by tools/embed_web.py. Do not edit. */\n")
        out.write(f"#ifndef {guard}\n#define {guard}\n\n")
        out.write(f"#define {sym.upper()}_LEN {len(data)}u\n\n")
        out.write(f"static const char {sym}[] = {{\n")
        out.write("\n".join(lines))
        out.write("\n    0x00\n};\n\n#endif\n")


if __name__ == "__main__":
    main()
