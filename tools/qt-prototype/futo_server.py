#!/usr/bin/env python3
"""Persistent FUTO decoder server for the Qt prototype.

Reads one JSON swipe per stdin line: {"points":[{"x","y","t"}, ...]} with t in ms.
Writes one JSON result per stdout line: {"greedy":str, "candidates":[{"text","score"}], "ms":float}.

Greedy CTC + dictionary top-5 (pruned by alphabet+length) over
/usr/share/dict/american-english. Reuses the validated futo-spike decoder.
"""
import json
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "futo-spike"))
import numpy as np
import torch
from validate import load_encoder, resample, greedy_ctc, QWERTY, LETTERS, MAX_KEYS
from dictionary_decode import load_dict, word_labels, ctc_word_logprob, CHAR_TO_IDX

_KEYS = None
def keys_tensor():
    global _KEYS
    if _KEYS is None:
        keys = torch.zeros(1, MAX_KEYS, 2); mask = torch.zeros(1, MAX_KEYS, dtype=torch.bool)
        for i, ch in enumerate(LETTERS):
            keys[0, i] = torch.tensor(QWERTY[ch]); mask[0, i] = True
        _KEYS = (keys, mask)
    return _KEYS

def emissions(enc, xs, ys, ts):
    keys, mask = keys_tensor()
    feats = torch.from_numpy(resample(xs, ys, ts)[None])
    le, _, _ = enc.execute((feats, keys, mask))
    return le.numpy()[0]

def main():
    enc = load_encoder()
    dictionary = load_dict("/usr/share/dict/american-english")
    sys.stderr.write("READY\n"); sys.stderr.flush()
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            req = json.loads(line)
        except Exception:
            continue
        pts = req.get("points", [])
        if len(pts) < 4:
            print(json.dumps({"greedy": "", "candidates": [], "ms": 0.0}), flush=True)
            continue
        xs = [p["x"] for p in pts]; ys = [p["y"] for p in pts]; ts = [p["t"] for p in pts]
        t0 = time.perf_counter()
        em = emissions(enc, xs, ys, ts)
        greedy = greedy_ctc(em[None])
        glen = len(greedy)
        nonblank = em[:, :26]
        kk = 3
        top_idx = np.argpartition(-nonblank, kk, axis=1)[:, :kk].reshape(-1)
        alphabet = set(int(x) for x in top_idx)
        cands = [w for (w, idxs, wl) in dictionary if abs(wl - glen) <= 3 and idxs <= alphabet]
        scored = []
        for w in cands:
            la, sm = word_labels(w)
            scored.append((float(ctc_word_logprob(em, la, sm)), w))
        scored.sort(reverse=True)
        top = [{"text": w, "score": round(s, 2)} for s, w in scored[:5]]
        dt = (time.perf_counter() - t0) * 1000.0
        print(json.dumps({"greedy": greedy, "candidates": top, "ms": round(dt, 1)}), flush=True)

if __name__ == "__main__":
    main()
