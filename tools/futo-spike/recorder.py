#!/usr/bin/env python3
"""Mouse-swipe recorder + FUTO greedy decode (Risk 3 measurement).

v1.1 change: highlights the prompted word's FIRST key and instructs the user to
start the glide on it — a controlled test of the start-position hypothesis for
the glides-2-6 anomaly (paths traced 'river' regardless of target).

v1 scope (unchanged): in-window capture only (no X pointer grab yet); greedy,
dictionary-free live decode. The corpus is re-decoded offline with
dictionary_decode.py for real top-1/top-3, so you only glide once.

Controls:
  hold LMB + move   glide the prompted word, starting on the HIGHLIGHTED key
  release LMB       decode + record, advance
  right-click       skip
  Esc / close       quit, print summary
"""
import json
import os
import random
import sys
import time
import tkinter as tk

from validate import load_encoder, decode, QWERTY   # reuse the validated decoder

WORDS = [
    "computer", "hello", "world", "keyboard", "mouse", "window", "open", "glide",
    "water", "house", "music", "plant", "story", "light", "river", "table",
    "money", "party", "stone", "north", "green", "break", "place", "force",
]
CANVAS_W, CANVAS_H = 960, 420


class Recorder:
    def __init__(self, root, corpus_path, words):
        self.root = root
        self.corpus_path = corpus_path
        self.words = words
        self.idx = 0
        self.swiping = False
        self.pts = []
        self.t0 = 0.0
        self.n = 0
        self.exact = 0
        print("loading FUTO encoder...", flush=True)
        self.encoder = load_encoder()
        print("encoder ready", flush=True)

        root.title("OpenGlide swipe recorder")
        self.prompt = tk.Label(root, text="", font=("sans", 20, "bold"))
        self.prompt.pack(pady=6)
        self.result = tk.Label(root, text="start each glide ON the highlighted key", font=("sans", 13))
        self.result.pack(pady=2)
        self.stats = tk.Label(root, text="0 glides | 0 exact", font=("sans", 11))
        self.stats.pack(pady=2)
        self.hint = tk.Label(root, text="right-click = skip    Esc = quit", font=("sans", 9), fg="#666")
        self.hint.pack()
        self.canvas = tk.Canvas(root, width=CANVAS_W, height=CANVAS_H, bg="#fafafa",
                                 highlightthickness=1, highlightbackground="#999")
        self.canvas.pack(padx=8, pady=8)
        self.canvas.bind("<ButtonPress-1>", self.on_press)
        self.canvas.bind("<B1-Motion>", self.on_motion)
        self.canvas.bind("<ButtonRelease-1>", self.on_release)
        self.canvas.bind("<ButtonPress-3>", self.on_skip)
        root.bind("<Escape>", lambda e: root.destroy())
        self.update_prompt()

    def draw_keyboard(self, highlight=None):
        self.canvas.delete("keys")
        kw, kh = 60, 60
        for ch, (cx, cy) in QWERTY.items():
            x, y = cx * CANVAS_W, cy * CANVAS_H
            if ch == highlight:
                fill, outline, width = "#ffe08a", "#e0a800", 3
            else:
                fill, outline, width = "#fff", "#bbb", 1
            self.canvas.create_rectangle(x - kw/2, y - kh/2, x + kw/2, y + kh/2,
                                         outline=outline, fill=fill, width=width, tags="keys")
            self.canvas.create_text(x, y, text=ch.upper(), font=("sans", 16, "bold"), tags="keys")

    def update_prompt(self):
        w = self.words[self.idx % len(self.words)]
        self.prompt.config(text=f"Glide the word:  {w.upper()}   (start on the yellow key)")
        self.draw_keyboard(highlight=w[0])

    def on_press(self, e):
        self.canvas.delete("path")
        self.swiping = True
        self.pts = []
        self.t0 = time.perf_counter()
        self._record(e)

    def on_motion(self, e):
        if self.swiping:
            self._record(e)

    def _record(self, e):
        ms = (time.perf_counter() - self.t0) * 1000.0
        self.pts.append((e.x / CANVAS_W, e.y / CANVAS_H, ms))

    def on_release(self, e):
        if not self.swiping:
            return
        self.swiping = False
        self._record(e)
        if len(self.pts) < 4:
            self.result.config(text="(too short — glide across more keys)")
            return
        target = self.words[self.idx % len(self.words)]
        xs = [p[0] for p in self.pts]
        ys = [p[1] for p in self.pts]
        ts = [p[2] for p in self.pts]

        t0 = time.perf_counter()
        pred = decode(self.encoder, xs, ys, ts)
        dt = (time.perf_counter() - t0) * 1000.0
        hit = (pred == target)
        self.n += 1
        self.exact += int(hit)

        self.result.config(text=f"decoded: {pred!r}   target: {target!r}   "
                                f"{'✓ EXACT' if hit else '✗ near-miss'}   ({dt:.0f} ms)")
        pct = 100 * self.exact / max(self.n, 1)
        self.stats.config(text=f"{self.n} glides | {self.exact} exact ({pct:.0f}%)")

        flat = []
        for x, y, _ in self.pts:
            flat += [x * CANVAS_W, y * CANVAS_H]
        if len(flat) >= 4:
            self.canvas.create_line(*flat, fill="#0066cc", width=2, tags="path", smooth=True)

        entry = {
            "schema_version": 1,
            "target": target,
            "layout_id": "en_qwerty_v1",
            "points": [{"x": round(x, 4), "y": round(y, 4), "time_us": int(ms * 1000)}
                       for (x, y, ms) in self.pts],
            "candidates": [{"text": pred, "score": 0.0, "source": "SwipeEngine-greedy"}],
            "selected": pred,
            "decode_ms": round(dt, 1),
        }
        with open(self.corpus_path, "a") as f:
            f.write(json.dumps(entry) + "\n")
        print(f"[{self.n}] target={target!r} pred={pred!r} {'HIT' if hit else 'miss'} "
              f"({dt:.0f}ms, {len(self.pts)} pts)", flush=True)

        self.idx += 1
        self.update_prompt()

    def on_skip(self, e):
        self.idx += 1
        self.update_prompt()


def main():
    corpus = sys.argv[1] if len(sys.argv) > 1 else "corpus.jsonl"
    words = WORDS[:]
    random.seed(7)   # different order from the first session
    random.shuffle(words)
    root = tk.Tk()
    app = Recorder(root, corpus, words)
    print("READY", flush=True)
    root.mainloop()
    pct = 100 * app.exact / max(app.n, 1)
    print(f"\n=== SESSION DONE: {app.n} glides, {app.exact} exact-match ({pct:.0f}%) ===", flush=True)
    print(f"corpus: {os.path.abspath(corpus)}", flush=True)


if __name__ == "__main__":
    main()
