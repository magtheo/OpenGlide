# qt-prototype — Phase 1 working prototype (spec §28)

Qt6/QML glide keyboard end-to-end: **hold LMB → glide → FUTO decodes → top word
is injected into whatever holds keyboard focus** (Firefox/editor/terminal).

## Components
- `src/swipesurface.{h,cpp}` — `QQuickItem` gesture surface (spec §4.2): grabs
  the mouse for the whole glide, records normalized-unclamped points (§6.3).
- `src/decoderbridge.{h,cpp}` — talks to `futo_server.py` over `QProcess`
  stdio (one JSON swipe in, one JSON result out).
- `src/injector.{h,cpp}` — commits the top word via a Linux `uinput` virtual
  keyboard (the layout-bound raw-key fallback, ADR-0002).
- `futo_server.py` — persistent Python decoder: greedy + dictionary top-5 over
  `/usr/share/dict/american-english`, reusing the validated `futo-spike` code.
- `main.qml` — QWERTY grid + candidate bar + committed-text mirror.

## Build / run
```
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
DISPLAY=:0 QT_QPA_PLATFORM=xcb ./build/openglide-qt
```
The `futo-spike` venv (torch/executorch) and HF model cache must exist (they do
on this machine). The window runs with `Qt.WindowStaysOnTopHint |
Qt.WindowDoesNotAcceptFocus` so gliding in it does **not** steal keyboard focus
from the target app (spec §17) — confirmed on XWayland.

## Prototype scope / known limits (deliberate)
- Decoder is the **Python** spike over a pipe (~100–300 ms/glide for the
  dictionary CTC) — the native C++ `SwipeEngine` (ADR-0003 worker) replaces it.
- Injection is **ASCII-only** via `uinput` (raw keys are layout-bound, ADR-0002).
  Arbitrary UTF-8 needs the `input-method-v2`/IBus path (not wired here).
- Out-of-window capture depends on Qt's mouse-grab semantics on the platform.

## Lessons baked in
- evdev `KEY_*` are physical scancodes (QWERTY order), **not** alphabetical —
  `KEY_A + (c-'a')` is wrong (and `'m'` collides with `KEY_LEFTSHIFT`). Use the
  explicit per-letter table in `injector.cpp`.
- `SwipeSurface` t is emitted in **ms** (FUTO expects ms), not µs.
