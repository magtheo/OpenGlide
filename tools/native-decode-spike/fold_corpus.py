#!/usr/bin/env python3
"""Fold corpus-live.jsonl amendments into a labeled corpus (ADR-0006 step 1).

The app appends two kinds of line to ~/.local/share/openglide/corpus-live.jsonl:

  glide records  {"glide_id": ..., "target": <top-1 guess>, "points": [...],
                  "candidates": [...], "selected": ...}
  amendments     {"amend_of": <glide_id>, "selected": <the user's correction>}
  drops          {"drop_of": <glide_id>}   — the word was chip-deleted; no label

A glide's top-1 guess is NOT a trustworthy label — it is exactly the thing being
measured. The trustworthy labels are (a) corrected-by-chip (an amend line names
the glide and the true word) and (b) accepted-and-left (no amend: the user saw
the word land and did not correct it within the history window).

This tool merges amendments into their glides and emits:
  <out>.jsonl  records with target/selected updated, "corrected": true|false
  <out>.tsv    the flat form corpus_test reads (same as dump_corpus.py)

Caveat carried in the output header: a word deleted via chip (deleteHistory)
emits NO amend — intent is unknown — so those glides keep their top-1 label but
are marked "deleted": true. Exclude them with --keep-deleted if you want only
surviving words. Unamended-but-accepted labels are still weaker than explicit
corrections (a user may not bother correcting in casual text); if you need
certainty, use only --corrected for gold labels.

Usage: fold_corpus.py [in.jsonl] [out_prefix] [--keep-deleted] [--corrected]
"""
import json, sys, os

src = sys.argv[1] if len(sys.argv) > 1 else os.path.expanduser(
    "~/.local/share/openglide/corpus-live.jsonl")
out = sys.argv[2] if len(sys.argv) > 2 else "/tmp/corpus-folded"
keep_deleted = "--keep-deleted" in sys.argv
only_corrected = "--corrected" in sys.argv

glides, amends, drops = {}, [], []
with open(src) as f:
    for line in f:
        line = line.strip()
        if not line:
            continue
        try:
            r = json.loads(line)
        except Exception:
            print(f"skipping unparsable line: {line[:60]}...", file=sys.stderr)
            continue
        if "amend_of" in r:
            amends.append(r)
        elif "drop_of" in r:
            drops.append(r)
        elif "glide_id" in r:
            glides[r["glide_id"]] = r

n_corr = 0
for a in amends:
    g = glides.get(a["amend_of"])
    if g is None:
        print(f"amend for unknown glide {a['amend_of']} (pre-recording?) — dropped", file=sys.stderr)
        continue
    g["target"] = g["selected"] = a["selected"]
    g["corrected"] = True
    n_corr += 1
for d in drops:
    g = glides.get(d["drop_of"])
    if g is not None:
        g["deleted"] = True

rows = [g for gid, g in sorted(glides.items())
        if len(g.get("points", [])) >= 4
        and (keep_deleted or not g.get("deleted"))
        and (not only_corrected or g.get("corrected"))]

with open(out + ".jsonl", "w") as o:
    for g in rows:
        o.write(json.dumps(g, separators=(",", ":")) + "\n")
with open(out + ".tsv", "w") as o:
    for g in rows:
        pts = "".join(f"\t{p['x']:.6f}\t{p['y']:.6f}\t{p['time_us']/1000.0:.3f}"
                      for p in g["points"])
        o.write(f"{g['target']}\t{len(g['points'])}{pts}\n")

n = len(rows)
print(f"{src}: {len(glides)} glides, {len(amends)} amends ({n_corr} applied), "
      f"{len(drops)} drops\n"
      f"-> {n} usable glides ({n_corr} corrected) : {out}.jsonl / {out}.tsv\n"
      f"score with: ./build/corpus_test <pte> <dict> {out}.tsv word_freq.txt --diagnose")
