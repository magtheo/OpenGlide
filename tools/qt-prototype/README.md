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

### Platform selection is in-process (src/main.cpp), not in run.sh

The KDE/GNOME paths need xcb; only wlroots compositors (sway/Hyprland/niri/river)
get native Wayland (layer-shell). The binary decides **before** constructing
`QGuiApplication` — a platform cannot change afterwards:

1. `QT_QPA_PLATFORM` already set (session, power user, `run.sh`) → respected as-is
2. `OPENGLIDE_QPA` → explicit override
3. otherwise: desktop-based default (xcb unless wlroots)

Why: a bare launch on KDE otherwise picks native Wayland, where kwin freezes
layer-shell margins at surface creation and clients cannot self-position — the
window is born at (0,0) and cannot be dragged (measured 2026-08-17). The env-var
decision used to live only in `run.sh`, which an installed copy never runs.
Invalid combos warn on startup (`[window] …` lines).

### Installing

```
cmake --install build          # or with --prefix /usr for system paths
```
installs the binary and `org.openglide.OpenGlide.desktop`, which deliberately
carries **no wrapper and no environment** — the .desktop, a panel launcher and a
bare `openglide-qt` in a shell all take the same in-process platform path.

Note on logs: `qInfo`/`qWarning` output follows Qt's default routing — stderr
normally, but the **journal** when the process runs under systemd
(`JOURNAL_STREAM` set). If `[window]` lines are "missing" from a redirected
stderr, check `journalctl --user`.

## Prototype scope / known limits (deliberate)
- Decoder is **native C++** (`SwipeEngine`): ~0.1 ms model load, ~7 ms forward,
  ~79 ms dict decode avg (worst-case ~350 ms; was ~1350 ms with Python). Decode
  is a prefix-shared **trie CTC** forward — one DFS over the dictionary trie,
  exact (~1.8× faster than per-word scoring).
- **Tap-to-type**: a tap (press, <5% movement) types the nearest key; a drag
  glides. They coexist on one surface (`src/swipesurface.{h,cpp}`).
- **The app says what it is doing.** A state pill in the chrome shows `decoding…`,
  `loading decoder… N s`, `busy — glide again`, `too short — glide further`,
  `decode stalled — glide again`, `decoder stopped — restart`. Every one of these
  states existed before and rendered in exactly ONE place — the opt-in
  diagnostics line — so by default a 480 ms decode, a dropped glide and a decoder
  that never loaded all looked identical: nothing happening. The pill borrows the
  two leftmost chip slots (candidates are always empty while it shows, and the
  slots fill oldest-first) and the slots never shift. The strings carry no typed
  text, no candidate, no geometry — structure only, which is the side of the
  ADR-0004 §1 line that is explicitly allowed. Transient notes expire after 2.6 s.
- **Refused glides resolve instead of hanging.** `DecoderBridge::decode` now
  returns a bool: FALSE means the glide was refused (engine not ready, or a decode
  already running — ADR-0003 stale-discard) and no `candidatesReady` is coming.
  Both paths used to set `pending` and then discover the refusal, leaving the UI
  waiting on a signal that never arrived: 20 s of dead keyboard ending in an
  invisible message. `glideFinished()` is now the single exit point and every
  branch leaves a state that resolves.
- **The key under the cursor is highlighted between glides** (`hoverMoved` /
  `hoverLeft` on `SwipeSurface`). A finger lifts; a mouse cursor stays where the
  last word left it, and RESULTS.md ("Start-position hypothesis — CONFIRMED")
  measures the cost of starting a glide on the wrong key: dict top-1 81% → 94%
  once each glide begins on the target's first letter. Deliberately a **ring**,
  not the fill-and-scale a glide uses — "you are here" and "the glide is crossing
  here" are different facts. The ring is restored from the stroke's last point on
  release, so it does not blank after every tap. Shift, ⌫, the action row and the
  symbols layer get the same ring on hover.
- **Double-letter recovery**: the glide passes a doubled key once (greedy emits
  single — god, helo), so candidates equal to the greedy with one letter doubled
  (good, hello) get +4.5 nats. (A frequency prior was tried but rejected: "help"
  beats "hello" on both CTC and frequency.) `decoderbridge` logs top-5 scores/glide.
- **Personalization**: per-user word counts (`~/.local/share/openglide/user_freq.tsv`)
  boost your own words in decode (score += λ·log(count+1), λ=2.0) — learns YOUR
  vocabulary (the right "learn common words"). Bumped on glide commit + correction.
- **Layout (ADR-0005)**: one grid unit `u = contentWidth/10` drives every element,
  so the letter block spans the full width (no letterbox) and the action row
  shares its columns and outer edges. Shift + ⌫ fill the 1.5u wings at each end
  of row 3 that used to be empty. Glide surface went from **26.8% → 60%** of the
  window; the compact default is 440×220, not 900×360. Resize by `−`/`+` stepper, S/M/L
  presets, corner grabs, or the top/bottom edges; geometry persists. `×`
  **collapses to a draggable puck** rather than quitting — quit is in the ⋯ menu,
  because in a mouse-only session a one-click quit is unrecoverable. Status line
  and committed mirror are behind the ⋯ → Diagnostics toggle (ADR-0004).
- **Appearance follows the desktop** by default, with persistent System / Light /
  Dark selection independent from the keyboard colorway. Appearance offers
  Neutral, Ocean, Forest, Plum, Rose and Amber presets plus a mouse-first custom
  RGB picker with live preview and a `#RRGGBB` readout. A color is a seed rather
  than a raw background: it lightly tints the body, chrome and keys, then becomes
  stronger for hover, gliding and selection; warning and destructive colors keep
  their semantic meaning. The normalized decoder geometry and all hit targets
  are unchanged.
- **Key geometry is loaded, not hard-coded** (spec §7.2): `languages/en/layout.json`
  → `SwipeEngine::set_layout()` → read back → `decoder.keys` → QML. One file, both
  consumers. A malformed layout is rejected whole and the built-in QWERTY stands.
- **Key routing** (`OPENGLIDE_KEY_ROUTING`): `pass` is the **default**, verified on
  hardware — the engine returns FALSE for every key, so IBus delivers it down the
  normal path and both the compositor and the client see the real event. Typing,
  Super+1 and Alt+Tab all work. `forward` (the old behaviour) re-injects a
  *synthetic* event the compositor never sees, which broke every globally grabbed
  shortcut; `auto` (pass Super only) was the interim compromise. Both remain as
  fallbacks.
- **Content logging** is `OPENGLIDE_LOG_CONTENT=1` (alias: `OPENGLIDE_KEY_DEBUG`).
  It prints every key you press to stderr and announces itself loudly — it is a
  keystroke log, so it is per-session, never persisted, never on by default
  (ADR-0004 amendment). **The same opt-in also records glides to a corpus**
  (ADR-0006 step 1): every decoded glide appends `points + candidates + greedy`
  to `~/.local/share/openglide/corpus-live.jsonl`, chip corrections append
  `amend_of` lines naming the true word, chip deletes append `drop_of`. The
  banner says where the file is and that deleting it is always fine — it
  contains what you typed. `../native-decode-spike/fold_corpus.py` folds the
  amendments into labeled records and emits the flat TSV `corpus_test` scores;
  see data-formats.md "Live corpus" for the line schemas.
- **⌫**: tap = delete the last **word** (undoable — an "↶ undo word" chip appears in
  the chrome for 8 s — labelled as an action, since a bare "↶ word" read as a
  strange suggestion and restores the word *and* its alternatives); hold = one
  char immediately, then repeat every 70 ms; hold+swipe-left = multi-word delete
  with the dot gauge; swipe-right = undo within that gesture.
- **Output is serialized** (`src/injector.{h,cpp}`): commit / commitExact /
  typeChar / backspace all queue onto ONE worker thread in submission order; the
  UI thread only enqueues. Two bugs in one fix — uinput backspace needs ~17 ms of
  real sleeps per character and used to run on the UI thread (recent-word
  correction can delete 70+ chars = a second of freeze), and uinput writes land
  immediately while IBus commits are queued, so "commit then backspace" could
  invert. The worker uses `og_ibus_commit_sync`, which blocks — harmless off the
  UI thread, and it is what restores ordering. `injector.pending()` exposes the
  queue depth (shown in diagnostics).
- **Preedit probe**: `set_capabilities` records what the focused client declares;
  diagnostics shows `preedit: YES/no (caps 0x…)`. No behaviour change yet — it
  answers whether a preedit model (current word left uncommitted, so correcting it
  needs no deletion) is viable in the apps you actually use, before anything is
  built on it. Deletion is the broken primitive underneath every correction path.
- **Pointer slowdown is OFF by default** and gated on the GNOME peripherals
  gsettings schemas actually existing. `g_settings_new()` *aborts the process* on
  a missing schema rather than failing, so off GNOME the old default (level 2)
  would kill the app the moment the cursor entered the window; it now reports
  unavailable in ⋯ instead. It also writes a **global** desktop setting on window
  hover, which is not something to opt someone into silently. A crash re-raises
  without running destructors, so `crash_handler` calls
  `og_pointer_emergency_restore()` — async-signal-safe by construction: the argv
  strings are rendered at apply() time and the handler only `fork()`s and
  `execve()`s. `restoreIfInterrupted()` remains the next-launch backstop.
- **Caret avoidance**: the focused app reports its text-cursor rect through the
  IBus `set_cursor_location` vfunc. An IME normally uses that to put a popup
  *next to* the caret; OpenGlide uses it to get *off* the text — if the window
  covers the caret it slides above or below, whichever is nearer, never during a
  glide, always on screen. Toggle in ⋯ (persisted). Many toolkits never report it,
  in which case this does nothing — the diagnostics line shows `caret: x,y ×N` or
  "never reported", so you can see which.
- **Target identity — corrections never edit a document we no longer own.** Every
  correction is "delete N chars, retype" against offsets into `injected`, our
  mirror of the focused field. If focus moves to a *different* field those offsets
  still validate (the mirror didn't change) but now address the wrong document, so
  a stale chip click would silently edit whatever the user switched to;
  `trimHistory()` cannot see this, it only catches drift *within* a focused field.
  Two layers: (1) `targetGeneration()` — IBus `focus_in`/`focus_out`/`disable`,
  exact but only while OpenGlide is the active IME; (2) an age cap, all that's
  available on the uinput fallback (KDE/Fcitx gives no IME focus signal), so it is
  much shorter there — 20 s vs 5 min. Entries are stamped with their generation;
  non-live entries are removed from the chip slots and every mutator
  (`replaceHistory` / `deleteHistory` / `correct` / `undoDelete`) **refuses**
  rather than editing blind. Diagnostics shows `target gen N (focused|no focus)`.
- **Recent-word history (spec §9.3)**: the four chrome slots are contextual —
  candidates right after a glide, the recent words once those go stale. Each
  history word keeps the candidate list its own glide produced, so clicking it
  offers those alternatives + Delete. Correction is no longer limited to the
  newest word. Entries hold absolute offsets into the `injected` mirror; a
  replacement shifts every later entry, and any manual edit drops entries that no
  longer match. Unit-tested in `../qml-logic-test` (85 assertions, no Qt needed) — including
  that each refusal issues *no injector output at all*, not merely unchanged text.
- **Show/hide (spec §13)**: three states — Full, Collapsed (the puck), and Hidden
  (no surface at all). Hidden is reached from ⋯ and returns via `ToggleListener`
  (`src/togglelistener.{h,cpp}`), a worker thread watching raw evdev mouse buttons
  for the LMB+RMB chord — second button within 150 ms, both held 300 ms, either
  order. **Observe-only: never `EVIOCGRAB`** (ADR-0004), so the app underneath
  still sees the clicks; whether that is annoying enough to justify interception
  is the §13.3 question, to be answered by using it. Needs read access to
  `/dev/input/event*` (group `input`); without it the Hide entry is disabled and
  says why, because hiding with no way back is the same trap as a one-click quit.
  Mode (`chord`/`middle`/`mouse4`/`mouse5`) and timings are read from settings.
  A disabled ⋯ entry now carries a **second line naming the fix** ("add yourself
  to the 'input' group") rather than only stating what is unavailable — a greyed
  row that explains nothing is a dead end.
- **Shift + symbols**: shift cycles off/once/lock; `?123` swaps in a symbols layer
  on the same three-row grid (tap-only — gliding is disabled there). Note the
  uinput fallback can only type ASCII it has evdev codes for; the symbols layer
  relies on the IBus path for the rest.
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
