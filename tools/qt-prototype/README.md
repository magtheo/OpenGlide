# qt-prototype — Phase 1 working prototype (spec §28)

Qt6/QML glide keyboard end-to-end: **hold LMB → glide → FUTO decodes → top word
is injected into whatever holds keyboard focus** (Firefox/editor/terminal).

## Components
- `src/swipesurface.{h,cpp}` — `QQuickItem` gesture surface (spec §4.2): grabs
  the mouse for the whole glide, records normalized-unclamped points (§6.3) +
  emits live `cursorMoved` for key-pop.
- `src/decoderbridge.{h,cpp}` — **native** decoder: owns a `SwipeEngine`
  (ExecuTorch C++, see `../native-decode-spike/`); `decode()` runs on a worker
  thread, serialized by a busy flag (stale-discard, ADR-0003). Replaces the old
  QProcess/Python path.
- `src/injector.{h,cpp}` — commits the top word via a Linux `uinput` virtual
  keyboard (the layout-bound raw-key fallback, ADR-0002). Single chars, words,
  and `backspace(n)` (for one-click correction + the FUTO delete gesture).
- `main.qml` — QWERTY grid (key-pop) + candidate bar + action row
  (comma/space/period/backspace with hold-⌫ swipe-left-delete / swipe-right-undo)
  + committed-text mirror. Pure-binding status state machine.
- `futo_server.py` — **superseded** by the native `SwipeEngine`; kept as the
  validated Python reference decoder.

## Build / run
Needs the ExecuTorch source tree + build-critical submodules under
`../../third_party/executorch/` (gitignored; see `../native-decode-spike/README.md`
for the one-time setup) and the `futo-spike` venv (provides `cmake>=3.29` and the
`torchgen`/`pyyaml` the ExecuTorch codegen needs).
```
../../tools/futo-spike/.venv/bin/cmake -B build -S . -DCMAKE_BUILD_TYPE=Release \
  -DPYTHON_EXECUTABLE=$PWD/../futo-spike/.venv/bin/python
../../tools/futo-spike/.venv/bin/cmake --build build -j4
DISPLAY=:0 QT_QPA_PLATFORM=xcb ./build/openglide-qt
```
First build compiles ExecuTorch + XNNPACK (~15–20 min); later builds are cached.
The decoder loads in ~0.2 s (no Python warmup). Window flags
`Qt.WindowStaysOnTopHint | Qt.WindowDoesNotAcceptFocus` keep keyboard focus on the
target app (spec §17) — confirmed on XWayland.

## Prototype scope / known limits (deliberate)
- Decoder is **native C++** (`SwipeEngine`): ~0.1 ms model load, ~7 ms forward,
  ~79 ms dict decode avg (worst-case ~350 ms; was ~1350 ms with Python). Decode
  is a prefix-shared **trie CTC** forward — one DFS over the dictionary trie,
  exact (~1.8× faster than per-word scoring).
- Injection: **IBus commit** (primary) — the app hosts a pass-through `openglide`
  engine, self-activates it on startup, forwards physical keys, and commits via
  `ibus_engine_commit_text` (UTF-8, layout-independent; ADR-0002). Verified
  `æøå 🫐` into a focused sink. `uinput` (layout-bound, ASCII) is the fallback
  (`src/ibus_engine.{c,h}`).
- Out-of-window capture depends on Qt's mouse-grab semantics on the platform.

## Lessons baked in
- evdev `KEY_*` are physical scancodes (QWERTY order), **not** alphabetical —
  `KEY_A + (c-'a')` is wrong (and `'m'` collides with `KEY_LEFTSHIFT`). Use the
  explicit per-letter table in `injector.cpp`.
- `SwipeSurface` t is emitted in **ms** (FUTO expects ms), not µs.
- QML: never assign a property that has a declarative binding (it destroys the
  binding) — keep `status.text` a pure binding.
