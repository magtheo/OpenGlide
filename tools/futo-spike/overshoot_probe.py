#!/usr/bin/env python3
"""Overshoot probe (spec §6.3): does the FUTO encoder tolerate coordinates
outside the [0,1] keyboard frame? §6.3 says overshoot is signal and must NOT be
clipped by the collector — this checks whether the decoder actually handles it.

Takes the known-good model-card 'computer' trajectory and injects genuinely
out-of-bounds excursions, then greedy-decodes each.

Run (from tools/futo-spike):
  .venv/bin/python overshoot_probe.py
"""
from validate import load_encoder, decode, PX, PY, PT


def tail_overshoot():
    """Continue past the last key to x=1.3 (clearly out of bounds)."""
    px, py, pt = list(PX), list(PY), list(PT)
    lx, ly, lt = px[-1], py[-1], pt[-1]
    for k in range(1, 9):
        px.append(lx + (1.3 - lx) * k / 8); py.append(ly); pt.append(lt + 30 * k)
    return px, py, pt


def head_overshoot():
    """Start before the first key at x=-0.3 (clearly out of bounds)."""
    px, py, pt = list(PX), list(PY), list(PT)
    fx, fy, ft = px[0], py[0], pt[0]
    for k in range(1, 9):
        px.insert(0, fx + (-0.3 - fx) * k / 8); py.insert(0, fy); pt.insert(0, max(0, ft - 30 * k))
    return px, py, pt


def mid_overshoot():
    """Insert an out-of-bounds spike mid-trajectory."""
    px, py, pt = list(PX), list(PY), list(PT)
    mid = len(px) // 2
    for k in range(5):
        px.insert(mid, 1.12 + 0.03 * k); py.insert(mid, py[mid] + 0.10); pt.insert(mid, pt[mid] + 20 * k)
    return px, py, pt


def main():
    enc = load_encoder()
    base = decode(enc, PX, PY, PT)
    print(f"baseline (clean, in-bounds): {base!r}   target 'computer'\n")
    for name, fn in [
        ("tail overshoot  (last key -> x=1.3)", tail_overshoot),
        ("head overshoot  (first key -> x=-0.3)", head_overshoot),
        ("mid  overshoot  (spike to x>1.1)",       mid_overshoot),
    ]:
        xs, ys, ts = fn()
        xmin, xmax = min(xs), max(xs)
        out = sum(1 for x in xs if x < 0 or x > 1)
        pred = decode(enc, xs, ys, ts)
        ok = "OK" if pred == "computer" else "BROKEN"
        print(f"  {name:<38} x[{xmin:+.2f},{xmax:+.2f}] out_pts={out:<2} -> {pred!r:<12} {ok}")
    print("\nverdict: if all still 'computer', overshoot is safe to pass unclamped (§6.3 holds);")
    print("         if any break, handle overshoot in the decoder adapter, not the collector.")


if __name__ == "__main__":
    main()
