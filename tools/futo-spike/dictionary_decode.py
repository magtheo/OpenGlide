#!/usr/bin/env python3
"""Offline dictionary-constrained decode over the recorded mouse-swipe corpus.

For each saved glide we re-run the FUTO encoder to get per-timestep log
emissions, then CTC-score every dictionary word (pruned by an alphabet + length
filter) and rank them. Reports real top-1/top-3/top-5 WORD accuracy — the metric
greedy can't give — plus per-glide diagnostics to spot capture issues.

Run (from tools/futo-spike):
  .venv/bin/python dictionary_decode.py [corpus.jsonl] [/usr/share/dict/american-english]
"""
import json
import sys

import numpy as np
import torch

from validate import load_encoder, resample, greedy_ctc, QWERTY, LETTERS, MAX_KEYS

BLANK = 64  # 64 key slots (we use 0..25) + blank = index 64
CHAR_TO_IDX = {c: i for i, c in enumerate(LETTERS)}


def keys_tensor():
    keys = torch.zeros(1, MAX_KEYS, 2)
    mask = torch.zeros(1, MAX_KEYS, dtype=torch.bool)
    for i, ch in enumerate(LETTERS):
        keys[0, i] = torch.tensor(QWERTY[ch])
        mask[0, i] = True
    return keys, mask


def emissions_for(encoder, xs, ys, ts):
    keys, mask = keys_tensor()
    feats = torch.from_numpy(resample(xs, ys, ts)[None])
    log_em, _, _ = encoder.execute((feats, keys, mask))
    return log_em.numpy()[0]  # [T, 65]


def load_dict(path):
    """Return list of (word, frozenset(letter indices), length)."""
    out = []
    with open(path) as f:
        for w in f:
            w = w.strip().lower()
            if 2 <= len(w) <= 12 and all(c in CHAR_TO_IDX for c in w):
                out.append((w, frozenset(CHAR_TO_IDX[c] for c in w), len(w)))
    return out


def word_labels(word):
    labels = [BLANK]
    for ch in word:
        labels.append(CHAR_TO_IDX[ch])
        labels.append(BLANK)
    labels_arr = np.array(labels)
    skip = np.zeros(len(labels), dtype=bool)
    for i in range(2, len(labels)):
        skip[i] = labels[i] != labels[i - 2]
    return labels_arr, skip


def ctc_word_logprob(em, labels_arr, skip_mask):
    """Vectorized CTC forward (log space)."""
    T = em.shape[0]
    L = len(labels_arr)
    NEG = -1e18
    log = np.full(L, NEG)
    log[0] = em[0, labels_arr[0]]
    if L > 1:
        log[1] = em[0, labels_arr[1]]
    for t in range(1, T):
        from_prev = np.concatenate(([NEG], log[:-1]))
        raw_prev2 = np.concatenate(([NEG, NEG], log[:-2]))
        from_prev2 = np.where(skip_mask, raw_prev2, NEG)
        s = np.logaddexp(np.logaddexp(log, from_prev), from_prev2)
        log = s + em[t, labels_arr]
    total = log[L - 1]
    if L > 1:
        total = np.logaddexp(total, log[L - 2])
    return total


def main():
    corpus = sys.argv[1] if len(sys.argv) > 1 else "corpus.jsonl"
    dict_path = sys.argv[2] if len(sys.argv) > 2 else "/usr/share/dict/american-english"
    print("loading dictionary...", flush=True)
    dictionary = load_dict(dict_path)
    print(f"  {len(dictionary)} candidate words (a-z, len 2-12)", flush=True)
    print("loading FUTO encoder...", flush=True)
    enc = load_encoder()
    entries = [json.loads(l) for l in open(corpus)]
    N = len(entries)

    top1 = top3 = top5 = greedy_hit = 0
    print(f"\n{'#':>2} {'target':<10} {'greedy':<10} {'top1':<10} {'top3candidates':<24} {'rank':>4}  diagnostics")
    print("-" * 100)
    for i, e in enumerate(entries, 1):
        pts = e["points"]
        xs = [p["x"] for p in pts]
        ys = [p["y"] for p in pts]
        ts = [p["time_us"] / 1000.0 for p in pts]
        em = emissions_for(enc, xs, ys, ts)
        greedy = greedy_ctc(em[None])
        glen = len(greedy)

        # alphabet prune: union of top-k non-blank letters per timestep
        nonblank = em[:, :26]
        kk = 3
        top_idx = np.argpartition(-nonblank, kk, axis=1)[:, :kk].reshape(-1)
        alphabet = set(int(x) for x in top_idx)

        cands = [w for (w, idxs, wl) in dictionary
                 if abs(wl - glen) <= 3 and idxs <= alphabet]
        target = e["target"]
        if target not in cands:
            cands.append(target)  # fairness: always allow target to be scored

        scored = []
        for w in cands:
            la, sm = word_labels(w)
            scored.append((float(ctc_word_logprob(em, la, sm)), w))
        scored.sort(reverse=True)
        top = scored[:5]
        rank = next((r + 1 for r, (_, w) in enumerate(scored) if w == target), 999)

        greedy_hit += (greedy == target)
        top1 += (rank == 1)
        top3 += (rank <= 3)
        top5 += (rank <= 5)

        conf = float(np.exp(em.max(axis=1)).mean())
        xr = (min(xs), max(xs)); yr = (min(ys), max(ys))
        diag = f"n={len(pts)} conf={conf:.2f} x[{xr[0]:.2f},{xr[1]:.2f}] y[{yr[0]:.2f},{yr[1]:.2f}]"
        t3 = ",".join(w for _, w in top[:3])
        print(f"{i:>2} {target:<10} {greedy:<10} {top[0][1]:<10} {t3:<24} {rank:>4}  {diag}")

    print("\n=== AGGREGATE (n=%d) ===" % N)
    print(f"  greedy exact : {greedy_hit}/{N} = {100*greedy_hit/N:.0f}%")
    print(f"  dict top-1   : {top1}/{N} = {100*top1/N:.0f}%")
    print(f"  dict top-3   : {top3}/{N} = {100*top3/N:.0f}%")
    print(f"  dict top-5   : {top5}/{N} = {100*top5/N:.0f}%")


if __name__ == "__main__":
    main()
