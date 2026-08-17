#!/usr/bin/env python3
"""band_probe.py — what do the decoder's HARD prunes cost, measured on our corpora.

`decode()` scores a dictionary word only if it survives three gates. Two of them
are plain arithmetic on lengths, so they can be measured with no model, no
ExecuTorch and no GPU — which matters, because every other accuracy question in
ADR-0006 is blocked on hardware.

  G2a  DFS depth cap     prefix length < greedy_len + 3      (swipe_engine.cpp:245)
  G2b  length band       |word_len - greedy_len| <= 3        (swipe_engine.cpp:242)
  G3   lexicon filter    2 <= word_len <= 12                 (swipe_engine.cpp:139)

(The third gate, the per-timestep top-3 `alph` prune, needs the model's
emissions — that one is `corpus_test --diagnose`'s job.)

Run:  python3 band_probe.py
"""
import collections
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
CORPORA = [
    (os.path.join(HERE, "../futo-spike/corpus-controlled.jsonl"), "controlled", set()),
    # RESULTS.md / ADR-0006 F1: entries #2-6 of corpus.jsonl are duplicated or
    # mislabelled (all decode to "river"); excluded by index, not by outcome.
    (os.path.join(HERE, "../futo-spike/corpus.jsonl"), "uncontrolled", {1, 2, 3, 4, 5}),
]
FREQ = os.path.join(HERE, "word_freq.txt")
BAND = 3          # the shipped constant, both directions


def greedy_of(rec):
    for c in rec.get("candidates", []):
        if c.get("source") == "SwipeEngine-greedy":
            return c["text"]
    return None


def load_glides():
    out = []
    for path, tag, skip in CORPORA:
        if not os.path.exists(path):
            print(f"  (missing: {path})", file=sys.stderr)
            continue
        with open(path) as f:
            for i, line in enumerate(f):
                if i in skip:
                    continue
                rec = json.loads(line)
                g = greedy_of(rec)
                if g is not None:
                    out.append((tag, rec["target"], g))
    return out


def main():
    glides = load_glides()
    if not glides:
        sys.exit("no corpora found")

    print(f"=== length band on {len(glides)} recorded glides "
          f"(corpus.jsonl #2-6 excluded per RESULTS.md) ===\n")
    print(f"{'target':<14}{'greedy':<16}{'wlen':>5}{'glen':>5}{'w-g':>5}   verdict")
    pruned, diffs = [], collections.Counter()
    for tag, target, g in sorted(glides, key=lambda r: len(r[1]) - len(r[2])):
        d = len(target) - len(g)
        diffs[d] += 1
        ok = abs(d) <= BAND
        if not ok:
            pruned.append((target, g))
        print(f"{target:<14}{g[:15]:<16}{len(target):>5}{len(g):>5}{d:>+5}   "
              f"{'scored' if ok else 'NEVER SCORED (length-pruned)'}")

    print(f"\n(word_len - greedy_len) distribution: {dict(sorted(diffs.items()))}")
    print(f"exact-length greedy: {diffs[0]}/{len(glides)}  "
          f"within +/-1: {diffs[-1] + diffs[0] + diffs[1]}/{len(glides)}")
    print(f"length-pruned: {len(pruned)}/{len(glides)}"
          + (f"  -> {', '.join(f'{t} (greedy {g!r})' for t, g in pruned)}" if pruned else ""))
    print("\nReading: on clean strokes the greedy length is already right, so the")
    print("band does no discrimination work there — it only ever binds on the")
    print("sloppy/truncated tail, which is exactly the input we want to tolerate.")

    if not os.path.exists(FREQ):
        return
    words = [w for w in (l.split('\t')[0].strip().lower() for l in open(FREQ))
             if w.isalpha() and w.isascii()]
    by_len = collections.Counter(len(w) for w in words)
    n = len(words)
    kept = sum(v for k, v in by_len.items() if 2 <= k <= 12)
    print(f"\n=== what widening it would cost ===")
    print(f"proxy lexicon: {n} words (word_freq.txt; the real one is")
    print(f"/usr/share/dict/american-english, ~71k — treat ratios as indicative)")
    print(f"G3 lexicon filter keeps {kept} ({kept / n:.1%}); "
          f"drops {sum(v for k, v in by_len.items() if k > 12)} words of 13+ letters\n")
    print(f"{'greedy len':>10}{'band +/-3':>11}{'band +/-5':>11}{'ratio':>8}")
    for glen in range(3, 9):
        b3 = sum(v for k, v in by_len.items() if abs(k - glen) <= 3 and 2 <= k <= 12)
        b5 = sum(v for k, v in by_len.items() if abs(k - glen) <= 5 and 2 <= k <= 12)
        print(f"{glen:>10}{b3:>11}{b5:>11}{b5 / b3:>8.2f}")
    print("\nThese are words eligible BY LENGTH. Actual DFS cost grows far slower:")
    print("the alph prune cuts the set first, and the trie shares prefixes.")


if __name__ == "__main__":
    main()
