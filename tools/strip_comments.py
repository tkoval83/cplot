#!/usr/bin/env python3
import os
import sys


def strip_comments(text: str) -> str:
    out = []
    i = 0
    n = len(text)
    NORMAL, LINE, BLOCK, STRING, CHAR = range(5)
    state = NORMAL
    str_delim = '"'
    escape = False

    while i < n:
        ch = text[i]

        if state == NORMAL:
            if ch == '"':
                state = STRING
                str_delim = '"'
                out.append(ch)
                i += 1
            elif ch == '\'':
                state = CHAR
                str_delim = '\''
                out.append(ch)
                i += 1
            elif ch == '/':
                # Lookahead for // or /*
                if i + 1 < n:
                    nxt = text[i + 1]
                    if nxt == '/':
                        state = LINE
                        i += 2
                        continue
                    elif nxt == '*':
                        state = BLOCK
                        i += 2
                        continue
                # not a comment
                out.append(ch)
                i += 1
            else:
                out.append(ch)
                i += 1

        elif state == LINE:
            # consume until newline, but keep the newline
            if ch == '\n':
                out.append('\n')
                state = NORMAL
            i += 1

        elif state == BLOCK:
            # consume until closing */
            if ch == '*' and i + 1 < n and text[i + 1] == '/':
                i += 2
                state = NORMAL
            else:
                i += 1

        elif state == STRING:
            out.append(ch)
            if escape:
                escape = False
            else:
                if ch == '\\':
                    escape = True
                elif ch == str_delim:
                    state = NORMAL
            i += 1

        elif state == CHAR:
            out.append(ch)
            if escape:
                escape = False
            else:
                if ch == '\\':
                    escape = True
                elif ch == str_delim:
                    state = NORMAL
            i += 1

    return ''.join(out)


def iter_source_files(root: str):
    for dirpath, dirnames, filenames in os.walk(root):
        for name in filenames:
            if name.endswith('.c') or name.endswith('.h'):
                yield os.path.join(dirpath, name)


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else 'src'
    changed = 0
    for path in iter_source_files(root):
        with open(path, 'r', encoding='utf-8') as f:
            content = f.read()
        stripped = strip_comments(content)
        if stripped != content:
            with open(path, 'w', encoding='utf-8') as f:
                f.write(stripped)
            changed += 1
            print(f"stripped: {path}")
    print(f"done. files changed: {changed}")


if __name__ == '__main__':
    main()

