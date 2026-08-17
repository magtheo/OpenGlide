# OpenGlide — Plan & Status

**Living document.** The [technical spec](openglide_technical_spec.md) is the
*vision*; [decisions/](decisions/) are settled *choices*; [../tools/RESULTS.md](../tools/RESULTS.md)
is *empirical findings*. This file reconciles all three into: **what's done,
what the plan is, and what's next.** When reality changes, update this file
(the spec is intentionally left as the original vision — its supersessions are
recorded here and in the ADRs).

> Note: the spec is now **partially superseded** — ADR-0002 inverts the output
> architecture the spec describes in §14–§15; findings refine §6.3. Read this
> file + the ADRs for current truth, not the spec alone.

## Status at a glance

Core technical assumptions are now **measured, not guessed**:

| Area | Status | Evidence |
|---|---|---|
| FUTO on mouse trajectories | ✅ **Risk 3 closed** — 100% dict top-1 on clean glides, ~5 ms | RESULTS.md |
| Text-output architecture | ✅ mapped — GNOME needs IBus (im-v2 not exposed); raw-key backends layout-bound | ADR-0002, RESULTS.md |
| Focus preservation | ✅ `WindowDoesNotAcceptFocus` keeps keyboard focus on XWayland; override-redirect is the fallback | RESULTS.md |
| Gesture capture (§4.2/6.2/6.3) | ✅ spec-aligned `SwipeSurface` (pointer grab, unclamped) — 9/9 | tools/swipe-capture |
| **Phase 1 prototype** | ✅ **end-to-end working** — glide → decode → injected into a real app (XWayland) | tools/qt-prototype, RESULTS.md |
| Native C++ decoder | ✅ built + live (**trie CTC**) — ExecuTorch C++ (XNNPACK); 0.1 ms load, 7 ms forward, ~79 ms decode avg / ~350 ms worst-case (was ~8 s / ~1350 ms); 94% corpus = Python parity | tools/native-decode-spike, RESULTS.md |
| UTF-8 via IBus (GNOME) | ✅ **wired into prototype** — hosts a pass-through engine, self-activates; `og_ibus_commit` lands exact UTF-8 (verified `æøå 🫐`); uinput fallback | tools/qt-prototype/src/ibus_engine.c, RESULTS.md |
| Licensing | ✅ preliminary GO (GPL-3.0-only lib + commercial-permissive weights) | ADR-0001 |
| Decisions / contracts | ✅ ADR-0001..0004, versioned data-formats | decisions/, data-formats.md |
| **Window / layout UX** | ✅ **compiled, rendered, hand-driven** (2026-08-14, Plasma 6 / XWayland, uinput path): glide → decode → old-word correction → ⌫-hold exercised live — no freeze, no reordering (`help te` sink ground truth). IBus path not connected on that box (session has no IM configured; see RESULTS.md) | ADR-0005 |

## How reality refined the spec's §28 phases

- **"FUTO integration"** is no longer a risk to prove — it's done (see RESULTS.md). Phase 1 may assume FUTO works.
- **"uinput = universal output"** is **wrong** — ADR-0002 inverts it: commit UTF-8 via `input-method-v2`/IBus first; `uinput`/`XTest` is a layout-bound fallback. The Phase 1 milestone "word enters Firefox" still stands, but the path is the ADR-0002 hierarchy.
- **New refinements** (mouse-specific, not in the spec):
  - **Start-position UX** — cursor persists between words (unlike a finger), so each glide must begin on the target's first letter; the UI must guide this.
  - **Overshoot adapter** (§6.3) — leading overshoot is tolerated, but trailing/mid overshoot corrupts decode; the collector stays unclamped, the **decoder adapter** must clamp/drop trailing+mid overshoot.

## Roadmap

### Phase 0 — Validation & decisions ✅ DONE
Probes, FUTO validation, ADRs, data contracts. (`tools/` + `docs/decisions/` + `data-formats.md`.)

### Phase 1 — Working prototype ✅ MILESTONE (prototype); hardening remains
The spec's Phase 1 flow works end-to-end: hold LMB → glide → FUTO decodes → top word injected into a focused app (`tools/qt-prototype`). Verified on XWayland — words reach Firefox/editor/terminal; `WindowDoesNotAcceptFocus` preserves keyboard focus (§17).

Remaining to harden Phase 1 toward product quality:
- ~~Native C++ `SwipeEngine`~~ ✅ done + live in the Qt prototype (`tools/native-decode-spike`): ExecuTorch C++ runtime (XNNPACK — the `.pte` is delegated) runs the FUTO encoder natively; full decode ported (resample → forward → greedy + dict CTC + overshoot adapter). **0.1 ms** model load (was ~8 s warmup), **7 ms** forward, **~79 ms** avg decode (was ~1350 ms); corpus top-1 94% = Python parity. A prefix-shared **trie CTC** forward (one DFS over the dictionary trie, exact — identical ranking to per-word scoring) cut decode ~1.8× avg and the outlier 840→350 ms; remaining cost is the `alph`+length candidate set.
- ~~UTF-8 injection via IBus~~ ✅ **wired into the Qt prototype** (`tools/qt-prototype/src/ibus_engine.{c,h}`): the app hosts a pass-through IBus engine (`openglide`), self-activates it as the global engine on startup (in-process `set_global_engine_async` — external `SetGlobalEngine` can't resolve a runtime engine), forwards physical keys so typing still works, and commits via `og_ibus_commit` → `ibus_engine_commit_text` (UTF-8, layout-independent) with `uinput` fallback. Verified: `æøå 🫐 openglide` lands exactly in a focused GTK sink; previous engine restored on shutdown. Remaining: the `input-method-v2` path for wlroots compositors.
- ~~One-click candidate correction; decoder overshoot adapter (§6.3); watchdog for the `pending` state~~ ✅ done (`tools/qt-prototype`): candidate click → backspace + recommit; overshoot adapter (now in the native `SwipeEngine`) drops trailing OOB + clamps to `[0,1]`; a 20 s stuck-backstop + `decoderDied` (fires if the engine fails to load) keep the status state machine from sticking.
- ~~Tap-to-type + double-letter recovery~~ ✅ done: a tap (press, <5% movement) types the nearest key via IBus — precise entry alongside glide (`src/swipesurface`). And a **double-letter bonus** recovers doubled letters the glide collapses (good←god, hello←helo): candidates equal to the greedy with one letter doubled get +4.5 nats. (A word-frequency prior was tried first but **rejected** — logged scores showed "help" beats "hello" on both CTC and frequency, so no λ can pick hello; the doubled letter is the real signal.) Corpus still 94%.
- ~~Personalization~~ ✅ done (first Phase 2 work): per-user word counts persist at `~/.local/share/openglide/user_freq.tsv`; decode adds `user_lambda·log(count+1)` (λ=2.0) so your own vocabulary wins ties over time. Bumped on every glide commit + candidate correction, saved immediately (survives crash/kill). The right "learn common words" — learns YOUR words, not generic frequency (which broke hello→help).
- Out-of-window capture under Qt's mouse-grab on each platform.

### Phase 2 — Daily keyboard (in progress)

The decode pipeline is good enough to use; **the window is not.** Measured on
`main.qml` @ `a34e9f7`: the glide surface is **26.8%** of the default 900×360
window (23.0% at 1200×400 — it gets *worse* as you widen it), the action row is
built by a different layout system than the letter grid so its edges don't line
up, three grid columns inside the letter block are empty (1.5 at each end of
row 3 — exactly where shift and ⌫ belong), ~23% of the window is
non-interactive debug text, the window **cannot be resized by any mouse
gesture**, and `×` calls `Qt.quit()` — which in a mouse-only session is
unrecoverable, since restarting needs a terminal. [ADR-0005](decisions/0005-keyboard-layout-and-window-ux.md)
settles the layout, resize, and visibility model. Ordered work:

0. ~~**Single source of truth for key geometry**~~ ✅ (spec §7.2). The 26 centers
   now live once, in `languages/en/layout.json` (the schema `data-formats.md`
   already specified). `DecoderBridge` parses it, installs it via the new
   `SwipeEngine::set_layout()`, then **reads the geometry back out of the engine**
   and hands that to QML as `decoder.keys` — so the keys on screen and the keys
   the decoder scores against are the same objects, not two copies that agree by
   luck. A malformed file is rejected wholesale and the built-in QWERTY stands.
   Verified: layout.json is field-identical to the old `C[26][2]`; `set_layout`
   rejects short/duplicate/non-letter sets and keeps the prior geometry on reject.
1. ~~**Grid unification**~~ ✅ — `u = contentWidth/10` and `rowH` drive every
   element; letter block edge to edge; shift + ⌫ fill the row-3 wings; action row
   on the same ten columns and the same outer edges; status line and committed
   mirror moved behind a diagnostics toggle (ADR-0004); candidate chips in fixed
   `u`-slots with eliding. **Glide surface 26.8% → 60%**; default window is now
   560×280 instead of 900×360. Also fixed: tap-threshold anisotropy (now measured
   in key widths, not raw normalized units), `wordSwipePx` → one column, live
   glide trail with fade instead of the previous word's stale path. Added: shift
   (off/once/lock) and a `?123` symbols layer on the same grid — there were no
   capitals and no digits before.
2. ~~**Resize / move / persist**~~ ✅ — `−`/`+` stepper and S/M/L presets as the
   primary path, corner grabs ≥16 px + top/bottom edges via `startSystemResize`,
   `minimumWidth/Height`, any non-control chrome drags the window, geometry
   persisted to `~/.config/openglide/` via `AppSettings`. The left/right resize
   strips deliberately stop at the chrome and action bands so they never overlap
   the letter block's outer edge — grabbing a resize handle instead of starting a
   glide on Q or P would be worse than the bug it fixes.
   Still open here: snap-to-edge / docked mode.
3. ~~**Visibility model**~~ ✅ — Full / Collapsed / Hidden. `×` collapses to a
   draggable puck; quit moved into the ⋯ menu, so no single click can terminate a
   mouse-only session. **Hidden** (spec §13.5, no surface at all) is reached from
   the ⋯ menu and returns via `ToggleListener`: a worker thread watching raw
   evdev mouse buttons for the §13.1 LMB+RMB chord (second button within 150 ms,
   both held 300 ms), **observe-only — never `EVIOCGRAB`** (ADR-0004). Mode
   (`chord`/`middle`/`mouse4`/`mouse5`) and timings come from settings (§13.6).
   Hide is offered **only when the listener is actually running** — if
   `/dev/input` isn't readable the menu entry is disabled and says so, because
   hiding with no way back is the `×`-quits trap again, just slower to find.
   Still to do here: tray entry, a settings UI for §13.6, and the §13.3 question
   (the chord isn't suppressed, so the app underneath still sees left+right —
   live with it before building an interception layer).
4. **Aspect-ratio band** — re-run 2026-08-11 with the fixed `--model word` (the
   first "no cliff" result used the line model, which is blind to trailing
   overshoot). There **is** a cliff, at the tall end: top-1 drops to **83% at
   10:5 (k=0.60)** and holds 94% from 10:4 through 10:1.8 (wide). **Clamp the
   tall end** so the board can't get taller than ~10:4 (aspect ≥ ~2.5); the wide
   direction stays free. (ADR-0006 §F4; RESULTS.md.)
5. ~~**Recent-word history** (§9.3)~~ ✅ — and it cost no layout. The four chrome
   slots are contextual: right after a glide they show that word's candidates
   (as before), and once those go stale they become the **recent words**, each
   still carrying the candidate list its own glide produced. Clicking one opens
   its alternatives + Delete. So correction is no longer limited to the newest
   word — previously an older mistake could only be fixed by backspacing
   everything after it, which is the expensive kind of error for a mouse-only
   user (spec §36 counts pointer travel). The glide surface stays at 60%.
   History entries carry absolute offsets into the mirror, so a replacement
   shifts every later entry and any manual edit invalidates whatever no longer
   matches. `tools/qml-logic-test` unit-tests all of it against a simulated
   target buffer (41 assertions, no Qt/display/decoder needed).
   Not included: "Add to dictionary" (§9.3) — it needs the personal dictionary
   (§10.2) to reach the decode lexicon; bumping a word the trie doesn't contain
   would do nothing, so offering it would be a lie.
6. ~~**Caret avoidance**~~ ✅ — `set_cursor_location` is implemented in
   `ibus_engine.c`: the focused app reports its caret rect, and where an IME
   normally uses that to place a popup *next to* the caret, we use it to get
   *off* the text. The window slides above or below — whichever is nearer — only
   when it genuinely covers the caret, never mid-glide or mid-menu, always on
   screen. Toggle in ⋯, persisted. Many toolkits never report; then it is a
   no-op, which is why it ships on. The diagnostics overlay shows the live rect
   and a report count, so the app is its own probe for the ADR-0005 gate.
7. **Remaining**: snap-to-edge / docked mode (§7.4); tray entry + a settings UI for §13.6; the §13.3 question — the chord isn't suppressed, so the app underneath still sees left+right (live with it before building interception); and a **hardware pass on chip safety** (6425926: target-generation stamping + age caps + stale-edit refusals — logic suite is 85 assertions but needs `node`, absent on the KDE box).

8. **Decode accuracy — the make-or-break** (ADR-0006, current focus). Real glides
   underperform the controlled corpus (94%): a 24-glide live capture missed
   `coffee→code`, `because→bose`, `mouse→mousse`. The miss taxonomy now has a
   **measuring tool**, not just anecdotes: `corpus_test --diagnose` classifies each
   miss as rerank-able / length-pruned / alph-pruned / not-in-dict (each points at
   a *different* fix). ADR-0006 F2 was corrected by reading the code — `coffee` was
   never a beam problem, it hit a **one-doubling cap** in the bonus (needs `ff`
   *and* `ee`); `because` is suspected length-pruning. Depth before re-ranking,
   both as probes: **(a) decode depth** — lever 1a done: `count_doublings()`
   handles N pairs (tunable `--doublings`, 1 = old / 2 = default), guarded by
   `doubling_test` (16/16, incl. the `help`/`helo` no-bonus guard that blocks the
   frequency-prior regression); **(b) n-gram context rescoring** — the whole-text
   correction idea (n-gram, not neural; conservative). **Open gate:** A/B
   `--doublings 1` vs `2` + `--diagnose` on a fresh ≥30-real-glide corpus — zero
   regressions, and the tally settles depth-vs-rescoring priority.

9. **Output ownership + preedit probe** (3c6b226; ADR-0003 single-owner + the
   deletion wall). Two bugs, one root cause — output had no single owner: a
   **freeze** (uinput backspace is ~17 ms of real sleeps/char and ran on the UI
   thread; recent-word correction retypes 70+ chars → >1 s freeze) and an
   **ordering inversion** (uinput lands immediately, IBus commit queues on GLib →
   "commit then backspace" could delete before the word arrived). Fix: all four ops
   (commit / commitExact / typeChar / backspace) queue onto **one worker** in
   submission order; the UI thread only enqueues; `og_ibus_commit_sync` blocks on
   the worker (buys ordering back) without freezing. Fire-and-forget was "the
   right fix at the wrong layer." Also landed: `set_capabilities` reports
   `IBUS_CAP_PREEDIT_TEXT` per focused client in the diagnostics overlay — the
   caret-avoidance pattern again (measure what a GTK field / terminal / Electron
   actually declare before designing a preedit correction model). Preedit is the
   durable path out of the deletion wall (current word stays uncommitted →

   **Verified on hardware (2026-08-14, KDE/XWayland, uinput path):** the smoke
   test is closed — old-history-word correction (delete word + suffix, retype)
   and a 2 s ⌫-hold produced **no freeze and no ordering inversion**; exact
   landed text captured through a `cat > file` sink (`help te`). See RESULTS.md
   (KDE section).

> ⚠️ Steps 0–2 are **built and tested on hardware** (magtheo, 2026-08-11) — plus a
> follow-up fix there: `og_ibus_backspace` was blocking the UI thread up to 500 ms
> per delete, which piled up under hold-⌫ repeat and froze the session. First made
> fire-and-forget, then (2026-08-11) replaced by one serialized output queue on a
> dedicated worker (item 9) — blocking moved off the UI thread, not removed.
> Steps 3–5 and the step-4 harness were written on a box with **no Qt Quick and no
> ExecuTorch**: qmllint-clean, `-fsyntax-only` clean against real Qt 6 headers,
> and every piece of non-trivial logic is unit-tested standalone — the chord state
> machine (13 cases incl. "ordinary clicking never fires" and "a long glide then a
> right-click never fires"), the aspect re-projection (5 cases), and the history
> text model (41 assertions, `tools/qml-logic-test`). But **nothing new had been
> compiled or rendered.** *Update 2026-08-14:* steps 3–5 are now **compiled,
> rendered and exercised on hardware** (Plasma 6 / XWayland box): step 5's
> recent-word chips and old-entry correction in particular (see item 9
> verification + RESULTS.md). Still not exercised there: the chord/Hidden path
> (step 3) and the aspect harness (step 4).

Also Phase 2 per spec §28: ~~personalization~~ ✅, personal dictionary, SQLite,
punctuation, settings UI.

### Phase 3–5 — per spec §28
Speech (whisper.cpp) → rich IME (Fcitx 5) → languages (Norwegian).

## Open prerequisites (need user authorization)
- ~~Qt6 dev + QML modules~~ ✅ installed.
- ~~`libibus-1.0-dev`~~ ✅ installed (IBus backend now buildable).
- ~~CMake ≥3.29 (bump or patch) + ExecuTorch build~~ ✅ built from source (executorch v1.4.0 + XNNPACK submodules, 16-core box; app links in ~73 s incremental after the first ET build).
- Sway / Hyprland sessions — **KDE row closed for shipping** (2026-08-14..17, Plasma 6 / Fedora 44): the keyboard now runs there by default as an **override-redirect xcb window** — free positioning AND no focus stealing (kwin steals focus from managed xcb windows when the target is a Wayland window; layer-shell fixes focus but kwin freezes margins at creation, so no dragging). IBus connects (address-file resolver) but kwin's bridge drops engine commits → text stays on uinput, IBus is signals-only (`OPENGLIDE_TEXT_BACKEND` overrides). See the KDE section in RESULTS.md. Sway/Hyprland (layer-shell) remain the open rows.

## Pointers
- Vision: [openglide_technical_spec.md](openglide_technical_spec.md)
- Decisions: [decisions/](decisions/) (ADR-0001..0004)
- Contracts: [data-formats.md](data-formats.md)
- Findings: [../tools/RESULTS.md](../tools/RESULTS.md)
- Tools: `tools/qt-prototype` (Phase 1 app), `tools/ibus-engine-probe` (GNOME UTF-8 commit), `tools/futo-spike`, `tools/swipe-capture`, `tools/text-output-probe`, `tools/surface-probe`
