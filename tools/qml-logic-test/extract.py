#!/usr/bin/env python3
"""Pull named QML functions out of main.qml verbatim so they can be unit-tested.

Extracting the real source (rather than keeping a hand-copied duplicate) is the
whole point: a copy drifts, and then the test passes while the app is broken.
Brace-balanced so one-liner functions don't swallow their neighbours.
"""
import re, sys

WANT = {"pushHistory", "trimHistory", "replaceHistory", "deleteHistory", "correct",
        "commitDecoded", "deleteChar", "deleteWord", "tapPunct", "tapSpace",
        "typeKey", "consumeShift", "shifted", "tapDeleteWord", "undoDelete",
        "clearUndo", "undoWord", "entryLive"}

def strip_comment(line):
    q = line.find('//')
    return line[:q] if q >= 0 else line

def extract(path, want=WANT):
    src = open(path).read().split('\n')
    out, i, found = [], 0, set()
    while i < len(src):
        m = re.match(r'^    function (\w+)\(', src[i])
        if m and m.group(1) in want:
            found.add(m.group(1))
            blk, depth = [], 0
            while i < len(src):
                blk.append(src[i])
                c = strip_comment(src[i])
                depth += c.count('{') - c.count('}')
                i += 1
                if depth == 0 and '{' in strip_comment(blk[0]):
                    break
            out.append('\n'.join(blk))
        else:
            i += 1
    missing = want - found
    if missing:
        sys.exit(f"extract.py: not found in {path}: {sorted(missing)}")
    return '\n'.join(out)

if __name__ == '__main__':
    qml = sys.argv[1] if len(sys.argv) > 1 else '../qt-prototype/main.qml'
    dest = sys.argv[2] if len(sys.argv) > 2 else '/tmp/openglide_extracted.js'
    open(dest, 'w').write(extract(qml))
    print(f"extracted {len(WANT)} functions -> {dest}")
