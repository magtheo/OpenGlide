#!/usr/bin/env python3
"""Flatten a swipe corpus JSONL into a TSV the C++ corpus_test reads (no JSON dep
in C++): one glide per line — `target<TAB>n<TAB>x y t_ms<TAB>x y t_ms...`."""
import json, sys
corp = sys.argv[1] if len(sys.argv) > 1 else "../../futo-spike/corpus-controlled.jsonl"
out = sys.argv[2] if len(sys.argv) > 2 else "/tmp/corpus_flat.txt"
k = 0
with open(corp) as f, open(out, "w") as o:
    for line in f:
        line = line.strip()
        if not line:
            continue
        try:
            r = json.loads(line)
        except Exception:
            continue
        pts = r.get("points", [])
        if len(pts) < 4:
            continue
        o.write(r.get("target", "") + "\t" + str(len(pts)))
        for p in pts:
            o.write(f"\t{p['x']:.6f}\t{p['y']:.6f}\t{p['time_us']/1000.0:.3f}")
        o.write("\n")
        k += 1
print(f"wrote {k} glides -> {out}")
