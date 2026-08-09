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
| Native C++ decoder | ✅ built + live in prototype — ExecuTorch C++ (XNNPACK); 0.1 ms load, 7 ms forward, 162 ms decode (was ~8 s / ~1350 ms); 94% corpus = Python parity | tools/native-decode-spike, RESULTS.md |
| UTF-8 via IBus (GNOME) | ✅ validated — `ibus_engine_commit_text` lands exact UTF-8, focus retained | tools/ibus-engine-probe, ADR-0002 |
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
- ~~Native C++ `SwipeEngine`~~ ✅ done + live in the Qt prototype (`tools/native-decode-spike`): ExecuTorch C++ runtime (XNNPACK — the `.pte` is delegated) runs the FUTO encoder natively; full decode ported (resample → forward → greedy + dict CTC + overshoot adapter). **0.1 ms** model load (was ~8 s warmup), **7 ms** forward, **162 ms** avg decode (was ~1350 ms); corpus top-1 94% = Python parity. Variable cost is dict CTC scoring (future: trie decoder).
- UTF-8 injection via IBus — primary path **validated** (`tools/ibus-engine-probe`; ADR-0002 gate met). Remaining: wire it into the Qt prototype (engine service + D-Bus commit, `uinput` fallback) and the `input-method-v2` path for wlroots compositors.
- ~~One-click candidate correction; decoder overshoot adapter (§6.3); watchdog for the `pending` state~~ ✅ done (`tools/qt-prototype`): candidate click → backspace + recommit; overshoot adapter (now in the native `SwipeEngine`) drops trailing OOB + clamps to `[0,1]`; a 20 s stuck-backstop + `decoderDied` (fires if the engine fails to load) keep the status state machine from sticking.
- Out-of-window capture under Qt's mouse-grab on each platform.

### Phase 2–5 — per spec §28
Daily keyboard (dictionary, SQLite, personalization, floating/docked) → speech (whisper.cpp) → rich IME (Fcitx 5) → languages (Norwegian).

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
