#!/usr/bin/env python3
"""context_rescore_probe.py — ADR-0006 layer 2 probe (ships before any change
reaches the product decoder, per the ADR's step 3).

Two checks, mirroring how ambigMargin and the doubling cap were tuned:

1. REGRESSION CHECK — replay rescore() over every corpus this project has
   (controlled/live), using each glide's own recorded `context` field. This is
   the free part: glides recorded before the context-logging change carry no
   `context` at all, and rescore() is defined as a no-op with no context — so
   this check is really asking "did the logging/plumbing break anything,"
   not "does context help" (there isn't enough real sequential data yet for
   that — see docs/data-formats.md on the `context` field).

2. LAMBDA SWEEP — a hand-built minimal-pairs set (context, candidates,
   correct-answer), same shape as the F3 "say hello / need help" example in
   ADR-0006. Sweeps lambda and reports: fixes (was wrong, now right), breaks
   (was right, now wrong), no-ops. Pick the lambda with fixes > 0 and breaks
   == 0, same acceptance gate as every other accuracy change in this project.

Usage:
    python3 context_rescore_probe.py [count_2w.txt] [corpus-live.jsonl ...]
"""
import json
import math
import sys
from pathlib import Path

HERE = Path(__file__).parent


def load_bigrams(path):
    bg = {}
    with open(path) as f:
        for line in f:
            parts = line.rstrip("\n").split("\t")
            if len(parts) != 2:
                continue
            words, count = parts
            w1, _, w2 = words.partition(" ")
            if not w2:
                continue
            bg.setdefault(w1, {})[w2] = float(count)
    return bg


def rescore(prev, candidates, bigrams, lam):
    """candidates: list of (text, ctc_score). Returns (winner_idx, overridden, margin)."""
    if not prev or len(candidates) < 2:
        return 0, False, 0.0
    combined = []
    for text, score in candidates:
        count = bigrams.get(prev, {}).get(text, 0.0)
        combined.append(score + lam * math.log(count + 1.0))
    best = max(range(len(combined)), key=lambda i: combined[i])
    rest = [c for i, c in enumerate(combined) if i != best]
    margin = combined[best] - max(rest)
    return best, best != 0, margin


# ---------------------------------------------------------------------------
# 1. Regression check over every real corpus recorded so far.
# ---------------------------------------------------------------------------
def regression_check(bigrams, corpus_paths, lam):
    print(f"=== regression check (lambda={lam}) ===")
    total = overridden = with_context = 0
    for path in corpus_paths:
        if not Path(path).exists():
            print(f"  (skip, not found: {path})")
            continue
        glides = {}
        for line in open(path):
            line = line.strip()
            if not line:
                continue
            r = json.loads(line)
            if "glide_id" in r:
                glides[r["glide_id"]] = r
        n_ctx = 0
        n_over = 0
        for g in glides.values():
            cands = [(c["text"], c["score"]) for c in g.get("candidates", [])]
            ctx = g.get("context") or []
            prev = ctx[-1] if ctx else ""
            if prev:
                n_ctx += 1
            _, overridden_flag, _ = rescore(prev, cands, bigrams, lam)
            if overridden_flag:
                n_over += 1
                print(f"  OVERRIDE in {Path(path).name}: prev={prev!r} "
                      f"cands={[c[0] for c in cands[:3]]} -> idx flipped")
        print(f"  {Path(path).name}: {len(glides)} glides, {n_ctx} with context, {n_over} overridden")
        total += len(glides)
        overridden += n_over
        with_context += n_ctx
    print(f"  TOTAL: {total} glides, {with_context} with context, {overridden} overridden\n")


# ---------------------------------------------------------------------------
# 2. Lambda sweep over a hand-built minimal-pairs set.
#
# Each case: (prev_word, [(text, ctc_score), ...], correct_index).
# CTC scores are hand-set to mirror the failure this is meant to fix: the
# WRONG candidate leads on CTC alone (a plausible near-tie a real decode
# could produce), and only the previous word disambiguates which one the
# user meant. This is a synthetic set — it proves the mechanism, it does not
# stand in for a real-glide validation, which still needs the live corpus to
# grow enough `context`-bearing glides on minimal-pair words.
# ---------------------------------------------------------------------------
# Each entry's candidate list MUST be pre-sorted descending by score (index 0
# = CTC top-1), matching what the real decoder hands the rescorer — this is
# asserted below rather than assumed, after an earlier version of this file
# silently mismatched the two for 4 of 8 entries.
MINIMAL_PAIRS = [
    ("say",  [("help", 5.0), ("hello", 4.8)], "hello"),  # ADR-0006 F3, dir. 1: must flip
    ("need", [("help", 5.0), ("hello", 4.8)], "help"),   # ADR-0006 F3, dir. 2: must NOT flip
    ("to",   [("think", 6.2), ("tink", 6.0)], "think"),  # already right: must NOT flip
    ("their", [("hows", 4.5), ("house", 4.0)], "house"), # "their house": must flip
    ("over", [("their", 5.7), ("there", 5.5)], "there"), # "over there": must flip
    ("its",  [("color", 5.0), ("colour", 4.9)], "color"),# spelling variant: must NOT flip
    ("i",    [("thing", 5.3), ("think", 5.0)], "think"), # "i think": must flip
    ("good", [("think", 5.4), ("thing", 5.0)], "thing"), # "good thing": must flip
]


def lambda_sweep(bigrams, lambdas):
    print("=== lambda sweep (hand-built minimal pairs) ===")
    for prev, cands, correct in MINIMAL_PAIRS:
        scores = [s for _, s in cands]
        assert scores == sorted(scores, reverse=True), f"not CTC-sorted: {prev} {cands}"
    print(f"{'lambda':>8}  {'fixes':>5}  {'breaks':>6}  {'noop':>5}   detail")
    for lam in lambdas:
        fixes = breaks = noop = 0
        detail = []
        for prev, cands, correct in MINIMAL_PAIRS:
            correct_idx = [t for t, _ in cands].index(correct)
            winner, overridden, margin = rescore(prev, cands, bigrams, lam)
            was_right = correct_idx == 0
            now_right = winner == correct_idx
            if not was_right and now_right:
                fixes += 1
                detail.append(f"FIX {prev} {cands[0][0]}->{cands[winner][0]}")
            elif was_right and not now_right:
                breaks += 1
                detail.append(f"BREAK {prev} {cands[0][0]}->{cands[winner][0]}")
            elif not overridden:
                noop += 1
        print(f"{lam:>8.1f}  {fixes:>5}  {breaks:>6}  {noop:>5}   " + "; ".join(detail))


if __name__ == "__main__":
    bigram_path = sys.argv[1] if len(sys.argv) > 1 else str(HERE / "count_2w.txt")
    corpus_paths = sys.argv[2:] if len(sys.argv) > 2 else [
        str(HERE.parent / "futo-spike" / "corpus-controlled.jsonl"),
        str(HERE.parent / "futo-spike" / "corpus.jsonl"),
        str(Path.home() / ".local/share/openglide/corpus-live.jsonl"),
    ]
    print(f"loading bigrams from {bigram_path} ...")
    bigrams = load_bigrams(bigram_path)
    print(f"  {sum(len(v) for v in bigrams.values())} pairs, {len(bigrams)} distinct prev words\n")

    regression_check(bigrams, corpus_paths, lam=1.0)
    lambda_sweep(bigrams, [0.0, 0.5, 1.0, 1.5, 2.0, 3.0])
