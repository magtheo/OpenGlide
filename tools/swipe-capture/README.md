# swipe-capture

Spec-aligned gesture capture (technical spec §4.2 / §6.2 / §6.3) — the
minimal `SwipeSurface` the spec prescribes, in C/Xlib rather than the eventual
Qt `QQuickItem` (Qt6 dev isn't installed here).

## What it does that the tkinter recorder does not
- **Pointer grab** (`XGrabPointer` on LMB press): the glide keeps being captured
  after the cursor leaves the window (§6.2). The tkinter recorder silently drops
  out-of-window motion.
- **Unclamped coordinates**: points are normalized to the keyboard frame but
  recorded with their true value, so overshoot (`x`/`y` outside `[0,1]`) is
  preserved as signal (§6.3). Clipping, if ever needed, belongs in the decoder
  adapter, not the collector.

Capture only — it writes the swipe to a corpus JSONL (`docs/data-formats.md`
schema, `candidates` empty). Decode offline with
`../futo-spike/dictionary_decode.py`.

## Build / run
```
make
./swipe_capture capture.jsonl     # hold LMB on the yellow key, glide (off-keyboard is captured), release
                                   # right-click = skip    q / Esc = quit
```

## Notes
- XGrabPointer works under X11 / XWayland. On native Wayland there is no
  arbitrary client pointer grab (by design) — the product surface must carry an
  input-method / layer-shell role instead (see ADR-0002, `docs/RESULTS.md`).
