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

### Phase 2–5 — per spec §28
Daily keyboard (~~personalization~~ ✅, dictionary, SQLite, floating/docked) → speech (whisper.cpp) → rich IME (Fcitx 5) → languages (Norwegian).

## Open prerequisites (need user authorization)
- ~~Qt6 dev + QML modules~~ ✅ installed.
- ~~`libibus-1.0-dev`~~ ✅ installed (IBus backend now buildable).
- CMake ≥3.29 (bump or patch) + ExecuTorch build — native C++ decoder (4-core box, slow).
- Sway / Hyprland / KDE sessions — remaining matrix rows (input-method-v2 + layer-shell).

## Pointers
- Vision: [openglide_technical_spec.md](openglide_technical_spec.md)
- Decisions: [decisions/](decisions/) (ADR-0001..0004)
- Contracts: [data-formats.md](data-formats.md)
- Findings: [../tools/RESULTS.md](../tools/RESULTS.md)
- Tools: `tools/qt-prototype` (Phase 1 app), `tools/ibus-engine-probe` (GNOME UTF-8 commit), `tools/futo-spike`, `tools/swipe-capture`, `tools/text-output-probe`, `tools/surface-probe`
