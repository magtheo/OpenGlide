#!/usr/bin/env python3
"""FUTO encoder validation spike (spec S35 / Risk 3).

Step 1 — reproduce the model-card example: greedy-decode the sample 'computer'
trajectory with the layout-agnostic encoder. Proves the FUTO stack runs against
ADR-0001's GO.

Step 2 — robustness probe: perturb that trajectory (Gaussian noise, spatial
subsampling, timing jitter) and watch how greedy decode degrades. This is a
first, autonomous proxy for 'mouse trajectories differ from the finger data the
encoder was trained on' (Risk 3). Real mouse glides come in the recorder phase.
"""
import numpy as np
import torch
from huggingface_hub import hf_hub_download
from executorch.runtime import Runtime

# --- model-card QWERTY key centres in the normalized [0,1] keyboard frame ---
QWERTY = {
    "a": (0.10, 0.500), "b": (0.60, 0.833), "c": (0.40, 0.833), "d": (0.30, 0.500),
    "e": (0.25, 0.167), "f": (0.40, 0.500), "g": (0.50, 0.500), "h": (0.60, 0.500),
    "i": (0.75, 0.167), "j": (0.70, 0.500), "k": (0.80, 0.500), "l": (0.90, 0.500),
    "m": (0.80, 0.833), "n": (0.70, 0.833), "o": (0.85, 0.167), "p": (0.95, 0.167),
    "q": (0.05, 0.167), "r": (0.35, 0.167), "s": (0.20, 0.500), "t": (0.45, 0.167),
    "u": (0.65, 0.167), "v": (0.50, 0.833), "w": (0.15, 0.167), "x": (0.30, 0.833),
    "y": (0.55, 0.167), "z": (0.20, 0.833),
}
LETTERS = sorted(QWERTY)
MAX_KEYS = 64

# --- sample 'computer' trajectory from the FUTO model card ---
PX = [0.4141, 0.4478, 0.5, 0.5741, 0.6599, 0.7256, 0.7744, 0.8098, 0.8485, 0.867,
      0.8737, 0.8653, 0.8418, 0.8182, 0.8098, 0.7963, 0.7946, 0.8081, 0.8418, 0.8704,
      0.9057, 0.9259, 0.9545, 0.9697, 0.968, 0.9529, 0.9141, 0.8468, 0.7811, 0.7273,
      0.6869, 0.6616, 0.6582, 0.6431, 0.6061, 0.5572, 0.5067, 0.4663, 0.4495, 0.4461,
      0.4411, 0.4192, 0.3872, 0.362, 0.3283, 0.2795, 0.2391, 0.2323, 0.2407, 0.2593,
      0.2879, 0.3249, 0.3468, 0.3569]
PY = [0.8991, 0.858, 0.7876, 0.6702, 0.5352, 0.4237, 0.3357, 0.2653, 0.1655, 0.142,
      0.142, 0.2183, 0.3709, 0.588, 0.7347, 0.8462, 0.8697, 0.811, 0.6115, 0.4707,
      0.3122, 0.2066, 0.1303, 0.1068, 0.1068, 0.1068, 0.1185, 0.1596, 0.1772, 0.1772,
      0.1772, 0.189, 0.189, 0.189, 0.1831, 0.189, 0.189, 0.189, 0.189, 0.189,
      0.1831, 0.1831, 0.1831, 0.1831, 0.1831, 0.1948, 0.189, 0.1948, 0.189, 0.189,
      0.189, 0.1831, 0.1831, 0.1831]
PT = [0.0, 100, 149, 197, 246, 297, 348, 399, 449, 498, 548, 598, 648, 698, 749, 799,
      849, 949, 999, 1047, 1100, 1152, 1197, 1248, 1314, 1364, 1414, 1465, 1515, 1565,
      1614, 1666, 1715, 1851, 1898, 1951, 1998, 2049, 2097, 2165, 2231, 2279, 2331,
      2382, 2431, 2481, 2532, 2584, 2649, 2700, 2751, 2798, 2848, 2899]


def resample(px, py, pt, T=64):
    """Resample a variable-length trajectory to T evenly-spaced points -> [2, T]."""
    x, y, t = map(np.asarray, (px, py, pt))
    t = t - t[0]
    if t[-1] > 1e-3:
        n60 = max(2, round(t[-1] / (1000.0 / 60.0)) + 1)
        tt = np.linspace(0.0, t[-1], n60)
        x, y = np.interp(tt, t, x), np.interp(tt, t, y)
    idx = np.linspace(0, len(x) - 1, T)
    rx = np.interp(idx, np.arange(len(x)), x)
    ry = np.interp(idx, np.arange(len(y)), y)
    return np.stack([rx, ry], axis=0).astype(np.float32)


def greedy_ctc(log_emissions):
    blank = log_emissions.shape[-1] - 1
    out, prev = [], -1
    for c in log_emissions[0].argmax(axis=-1):
        if c != prev and c != blank and c < len(LETTERS):
            out.append(LETTERS[c])
        prev = c
    return "".join(out)


def load_encoder():
    pte = hf_hub_download("futo-org/futo-swipe", "honorable_sturgeon/model_fp32.pte")
    return Runtime.get().load_program(pte).load_method("forward")


_KEYS = None
def keys_tensor():
    global _KEYS
    if _KEYS is None:
        keys = torch.zeros(1, MAX_KEYS, 2)
        mask = torch.zeros(1, MAX_KEYS, dtype=torch.bool)
        for i, ch in enumerate(LETTERS):
            keys[0, i] = torch.tensor(QWERTY[ch])
            mask[0, i] = True
        _KEYS = (keys, mask)
    return _KEYS


def decode(encoder, px, py, pt):
    keys, mask = keys_tensor()
    feats = torch.from_numpy(resample(px, py, pt)[None])
    log_em, _, _ = encoder.execute((feats, keys, mask))
    return greedy_ctc(log_em.numpy())


def main():
    print("loading FUTO encoder (honorable_sturgeon)...")
    enc = load_encoder()

    base = decode(enc, PX, PY, PT)
    ok = base == "computer"
    print(f"\n[1] baseline clean 'computer' trajectory -> {base!r}   target='computer'   {'PASS' if ok else 'FAIL'}")

    rng = np.random.default_rng(0)
    px, py, pt = map(np.asarray, (PX, PY, PT))

    print("\n[2] robustness probe — greedy decode under perturbation:")
    print("    (how much can the trajectory deviate before greedy decode breaks?)")
    print("\n    spatial Gaussian noise:")
    for sigma in [0.005, 0.01, 0.02, 0.04, 0.08]:
        nx = np.clip(px + rng.normal(0, sigma, px.shape), 0, 1)
        ny = np.clip(py + rng.normal(0, sigma, py.shape), 0, 1)
        print(f"      sigma={sigma:<6} -> {decode(enc, nx, ny, pt)!r}")

    print("\n    spatial subsampling (fewer raw points, then resampled to 64):")
    for keep in [0.75, 0.5, 0.35, 0.25]:
        n = max(4, int(len(px) * keep))
        idx = np.linspace(0, len(px) - 1, n).astype(int)
        print(f"      keep={keep:<5} ({n:>2} pts) -> {decode(enc, px[idx], py[idx], pt[idx])!r}")

    print("\n    timing jitter (ms-scale noise on timestamps):")
    for jit_ms in [20, 50, 100, 200]:
        jt = np.maximum(pt + rng.normal(0, jit_ms, pt.shape), 0)
        jt = np.sort(np.maximum.accumulate(jt))            # keep monotonic
        jt[0] = 0
        print(f"      jitter~{jit_ms:>3}ms -> {decode(enc, px, py, jt)!r}")

    print("\n    speed warp (uniform time stretch/compress, same path):")
    for scale in [0.5, 2.0, 4.0]:
        print(f"      time x{scale:<3} -> {decode(enc, px, py, pt * scale)!r}")


if __name__ == "__main__":
    main()
