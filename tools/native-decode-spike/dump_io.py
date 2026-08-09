#!/usr/bin/env python3
"""Dump the exact input tensors Python feeds the FUTO encoder + the output it
produces, for the 'computer' trajectory. The C++ spike replays these to verify
native forward() reproduces Python bit-for-bit (within fp tolerance)."""
import os, sys
import numpy as np
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "futo-spike"))
import torch
from validate import load_encoder, resample, QWERTY, LETTERS, MAX_KEYS, PX, PY, PT

OUT = sys.argv[1] if len(sys.argv) > 1 else "/tmp/spike_io"
os.makedirs(OUT, exist_ok=True)

enc = load_encoder()
feats = torch.from_numpy(resample(PX, PY, PT)[None])           # [1,2,64] float32
keys = torch.zeros(1, MAX_KEYS, 2); mask = torch.zeros(1, MAX_KEYS, dtype=torch.bool)
for i, ch in enumerate(LETTERS):
    keys[0, i] = torch.tensor(QWERTY[ch]); mask[0, i] = True

out = enc.execute((feats, keys, mask))                          # tuple of 3
em = out[0].numpy()[0]                                          # [T,65]

feats.numpy().astype(np.float32).tofile(f"{OUT}/feats.bin")     # 1*2*64 floats
keys.numpy().astype(np.float32).tofile(f"{OUT}/keys.bin")       # 1*64*2 floats
mask.numpy().astype(np.uint8).tofile(f"{OUT}/mask.bin")         # 1*64 bytes
em.astype(np.float32).tofile(f"{OUT}/out_py.bin")               # T*65 floats

blank = em.shape[-1] - 1
g, prev = [], -1
for c in em.argmax(-1):
    if c != prev and c != blank and c < len(LETTERS):
        g.append(LETTERS[c])
    prev = c
print("PYTHON greedy:", "".join(g))
print("PYTHON output shape:", em.shape, "-> dumped feats/keys/mask/out_py to", OUT)
