# ADR-0005: One grid unit drives the whole surface; the window collapses, never quits

- **Status:** Accepted* — steps 0–3 implemented in `tools/qt-prototype` (0–2
  verified on hardware 2026-08-11); the aspect-band gate below is still open
- **Date:** 2026-08-10
- **Supersedes:** none (implements spec §2.1 "resizable keyboard", §7.3/§7.4,
  §13, §23; refines the Phase 2 "resize controls / floating / docking" line in
  [plan.md](../plan.md))

## Context

The Phase 1 prototype (`tools/qt-prototype`) proves the pipeline: glide → decode
→ commit. It does **not** yet behave like a keyboard someone would leave running
all day. Three problems block that, and all three are geometry problems in the
same place:

1. **The window spends most of its pixels on things that are not keys.** At the
   default 900×360 the glide surface is 26.8% of the window; the rest is chrome,
   letterbox, and debug text.
2. **It cannot be resized.** `Qt.FramelessWindowHint` removes the WM border and
   nothing calls `startSystemResize` — so spec §2.1's hard requirement
   ("Resizable keyboard") is currently unmet in the literal sense: there is no
   mouse gesture that changes the window's size.
3. **Its close button quits the process.** `main.qml:160` is `onClicked:
   Qt.quit()`. For a product whose premise is *mouse-only computer use*, the
   only way back is a terminal — which needs a keyboard. The app's own chrome
   contains a trap that breaks its core promise.

This ADR settles the layout/geometry model, the resize/move model, and the
visibility model, so the Phase 2 UI work has one set of rules to build against
instead of accreting more independently-anchored widgets.

## Findings (primary sources)

All measurements are of `tools/qt-prototype/main.qml` at commit `a34e9f7`,
computed from the layout expressions in that file (no rendering required — the
sizes are closed-form).

### F1. A single scalar couples every dimension, and the min() letterboxes

`main.qml:32` —
`keySize = max(30, min((width - 24)/10, (height - 48)/5.8))` — with the letter
block at `width: 10*keySize; height: 3*keySize` (`main.qml:182`), horizontally
centered. The `min()` means the block takes a fixed 10:3 aspect and is
letterboxed in whichever axis is not binding:

| window | keySize | glide surface | side letterbox | debug chrome (status + committed mirror) |
|---|---|---|---|---|
| **900×360 (default)** | 53.8 | **26.8%** of window | 40.2% of width | 22.6% of height |
| 700×300 | 43.4 | 27.0% | 37.9% | 25.0% |
| **1200×400** | 60.7 | **23.0%** | **49.4%** | 21.4% |
| 600×420 | 57.6 | 39.5% | 4.0% | **29.0%** |

Two structural consequences, both visible in the table:

- **Making the window wider makes it worse.** At 1200×400 nearly half the width
  is empty and the glide target has *shrunk* as a fraction of the window.
- **Surplus height is absorbed by the debug mirror.** `committedBar`
  (`main.qml:332-342`) is anchored `top: actionRow.bottom` … `bottom:
  parent.bottom`, so every pixel of height the keys cannot claim goes to a
  read-only text mirror — 29% of the window at 600×420.

There is also no `minimumWidth`/`minimumHeight`. Because `keySize` has a floor
of 30, a window narrower than ~324 px renders a letter block wider than the
window, which clips.

### F2. The action row is geometrically unrelated to the letter grid

The letters are absolutely positioned from normalized coordinates inside `kb`
(`main.qml:184-196`), total width `10*keySize`. The action row is a separate
`Row` (`main.qml:239-303`), separately centered, whose widths sum to
`1.5 + 5.0 + 1.5 + 2.0 = 10.0` keySize **plus** three `keySize*0.12` gaps =
`10.36*keySize`. It is therefore 0.36 keySize wider than the letter block and
centered independently, so no edge and no internal boundary aligns with the grid
above it. That mismatch is exactly the "bottom row isn't connected to the rest of
the keyboard" reading — it is not a spacing tweak, the two rows are built by
different layout systems that share no unit.

### F3. Three grid columns inside the letter block are unused

Row 3 key *centers* run `x ∈ [0.20, 0.80]` (`main.qml:41-42`), so with 1-column
keys the row's ink spans `x ∈ [0.15, 0.85]`. That leaves **1.5 empty columns at
each end** — 3 columns of a 10-column grid, 2.7% of the window at the default
size. On every phone layout these hold shift and backspace, and 1.5u is exactly
the width those keys conventionally take.

Critically: **filling them costs the decoder nothing.** The model consumes key
centers and trajectory in the *same* normalized [0,1]² frame
(`swipe_engine.cpp:50-56`, `run_forward`), so as long as the 26 letter centers
keep their normalized positions, adding non-letter keys in the empty slots does
not change a single model input.

### F4. Key geometry is duplicated, and the spec says it should not be

The same 26 normalized centers exist twice: `main.qml:36-43` (`keys`) and
`swipe_engine.cpp:50-56` (`static const float C[26][2]`). Spec §7.2 requires the
opposite — "The decoder should receive the current key geometry dynamically …
The neural decoder should not assume fixed pixel positions." Today any layout
edit must be mirrored by hand in C++ or the decoder silently scores against a
keyboard that is no longer on screen.

### F5. The tap/swipe threshold is anisotropic by 3.33×

`swipesurface.cpp:48-57` compares squared displacement in **normalized** units
against `0.05²`, using `dx` and `dy` symmetrically. But normalized units are not
isotropic: keys are 0.1 apart in x and 0.333 apart in y. So the same threshold
is **0.50 of a key horizontally and 0.15 of a key vertically** — a tap that
drifts ~15% of a key downward is classified as a glide and decodes to a wrong
word or "too short". `nearestKey` (`main.qml:119-126`) already does the right
thing by converting to pixels; the surface does not.

### F6. Spec §13 (show/hide) is entirely unbuilt

No hide state, no collapsed state, no tray, no global listener, no configurable
toggle. `×` quits (F1 above). §13.1–§13.6 — the chord, the evdev listener, the
alternative activations, the settings — exist only as text.

### F7. Smaller items in the same family

- `wordSwipePx: 60` (`main.qml:28`) is a fixed pixel constant, so the
  swipe-to-delete gesture requires a different fraction of a key at every window
  size.
- The glide path is painted only on `swipeCompleted` (`main.qml:218-225`) and
  never cleared, so what is on screen during a glide is the *previous* word's
  trail. Spec §34 lists live-path display as an open question; it is currently
  answered "stale path, no live path", which is the one option nobody wants.
- Candidate chips are fixed `keySize*2.4` wide in a centered `Row`
  (`main.qml:139-155`) with no eliding: long words overflow their chip, and
  because the row is centered, a given candidate lands at a different x on every
  glide, so no click target is ever in the same place twice.
- `set_cursor_location` is not implemented in `ibus_engine.c` (only
  `enable`/`disable`/`process_key_event`, lines 73-75). The focused application
  reports its caret rectangle through that vfunc — the signal needed to keep the
  keyboard off the text being typed is available on the path already built, and
  unused.

## Decision

### 1. One unit drives the surface

Define exactly two layout units and express **every** element as a multiple of
them:

```text
u = contentWidth / 10          (column width)
v = letterBlockHeight / 3      (row height)
```

The letter block spans all 10 columns, edge to edge — no centering, no
letterbox. The candidate row, the action row, and the chrome bar use the same 10
columns and the same edges. Nothing in the window is positioned by a constant
that is not a multiple of `u` or `v`.

Target composition (5 rows of `v`, 10 columns of `u`):

```text
┌──────────────────────────────────────────────────┐
│ ⠿   there   three   their   theme      🎤    ▾  │  1v  candidates + grip + mic + collapse
├──────────────────────────────────────────────────┤
│  Q   W   E   R   T   Y   U   I   O   P           │  1v ┐
│    A   S   D   F   G   H   J   K   L             │  1v ├ glide surface (decoder frame)
│  ⇧    Z   X   C   V   B   N   M      ⌫          │  1v ┘  wings filled (F3)
├──────────────────────────────────────────────────┤
│ ?123  ,          space          .   ⏎           │  1v  same 10 columns as the grid
└──────────────────────────────────────────────────┘
```

The glide surface becomes 3/5 of the window height at full width: **60% of the
window, up from 26.8%.** Equivalently, at identical key size the window shrinks
from 900×360 to 538×269 — **55% less screen** — or, at the same window area,
keys grow 1.5× (53.8 → 80.5 px) and the glide target becomes **2.24× larger**.
A comfortable compact default is `u = v = 44 px` → a **440×220** window, 30% of
the current footprint.

The `status` line and the `committedBar` mirror are removed from the default
surface. Per ADR-0004 they are diagnostics: they return only under the
content-logging opt-in. The reclaimed space is where the spec §9.3 history bar
goes, which is interactive.

### 2. The letter block owns the decoder frame; chrome may not enter it

`SwipeSurface` normalizes to the letter block's rectangle and nothing else. Keys
added in the row-3 wings (shift, backspace) sit **inside** that rectangle but are
not letters: they are tap-only targets, and a press that begins on one does not
start a glide. The 26 letter centers keep the normalized positions in F3
verbatim, so this whole redesign is a **zero-diff change to the model's inputs**.

### 3. Aspect ratio is free within a measured band, clamped outside it

With independent `u` and `v` the block fills any window shape, which is what
makes "resizable" mean something. To first order this is safe — the user aims at
key centers, which are at fixed normalized positions. It is not free to second
order: overshoot and curvature are produced by physical mouse momentum, so a
very wide/short block amplifies vertical overshoot in normalized space, and
RESULTS.md already shows the encoder's overshoot tolerance is asymmetric
(leading tolerated; trailing/mid corrupts decode).

Therefore: the allowed aspect band for the letter block is set **by measurement,
not by assumption** (see verification gates). Outside the band the surplus goes
to chrome (taller candidate/history rows) rather than distorting the grid.

### 4. Resizing must not require precise pointing

A frameless window resized by dragging a few-pixel edge is exactly the
fine-motor task this product exists to remove. Resize is therefore offered three
ways, in this order of prominence:

- **Size presets + a `−`/`+` scale stepper** in the chrome bar — the primary
  path, one click, no dragging.
- **A grip corner of at least `0.4u` (≥16 px)** at bottom-right, calling
  `Window.startSystemResize`.
- **Edge/corner zones** (~6 px, with cursor shapes) for users who expect them.

`minimumWidth = 10 * u_min` with `u_min ≈ 30 px`; the window geometry, scale,
and screen position persist across restarts.

Moving: any chrome that is not a key is a drag handle (already true of the top
bar, `main.qml:133`), plus snap-to-screen-edge, which is also the entry point to
the spec §7.4 docked mode.

### 5. Three visibility states, and `×` never quits

```text
Full ──collapse(×)──> Collapsed ──click puck──> Full
  │                        │
  └──────hide─────────> Hidden ──chord / hotkey / tray──> Full
```

- **Full** — the keyboard.
- **Collapsed** — a single draggable puck of about `1.5u × 1u`, always-on-top,
  at the last edge position. One click restores. This is the default action of
  `×`. It needs no permissions and is reachable with the mouse alone, so it is
  the *guaranteed* way back and the answer to F6 for the common case.
- **Hidden** — no surface at all (spec §13.5). Restored by the §13.1 LMB+RMB
  evdev chord, a configurable global hotkey, or a tray action.

Quit moves out of the chrome's primary row into a menu on the grip. **No
single click may terminate the process**, because in a mouse-only session
termination is unrecoverable.

Non-obstruction, in the same pass: idle opacity (full opacity on hover), and —
once `set_cursor_location` is implemented (F7) — nudging the window off the
reported caret rectangle instead of covering the text being typed.

## Consequences

- **Forces** a single source of truth for key geometry (F4) *before* any layout
  edit lands, since the wings/action-row work is precisely the change that would
  desync `main.qml` from `swipe_engine.cpp`. A `layout.json` per spec §22, loaded
  by both QML and a new `SwipeEngine::set_layout()`, replaces `C[26][2]`.
- **Forbids** any new element positioned by a raw pixel constant. `wordSwipePx`
  (F7) becomes a multiple of `u`.
- **Accepts** that the glide surface's aspect ratio is no longer fixed at 10:3,
  and that this is a decode-accuracy risk until the band in §3 is measured. The
  fallback if the band turns out to be narrow is to letterbox again — but only
  outside the measured band, and into *useful* chrome rather than blank margin.
- **Accepts** non-square keys within the band. Key hit-testing (`nearestKey`)
  already works in pixels, so it is unaffected; the tap threshold (F5) must be
  converted to pixels for the same reason.
- **Changes the meaning of `×`**, which existing muscle memory reads as "quit".
  Mitigated by the puck being visible and by quit remaining available one level
  in.
- The Collapsed state means the process stays resident. It must therefore hold
  its IBus engine registration and release nothing on collapse; ADR-0004's
  crash-safety rule (no stuck grabs) extends to the collapsed state.

## Amendment (2026-08-17): machine state returns to the default surface

§1 removed two things from the default surface in one sentence — "The `status`
line and the `committedBar` mirror are removed … they return only under the
content-logging opt-in" — and that sentence conflated two different kinds of
thing under one word, *diagnostics*.

- The **committed mirror** is user content: every word you have typed, on screen,
  for as long as the window is open. It is exactly what ADR-0004 §1 exists to
  keep off the default surface. That half of §1 was right and stands.
- The **status line's states** are not content. "decoding…", "too short",
  "decoder stopped" carry no typed text, no candidate, no geometry. ADR-0004 §1
  permits precisely this at default levels ("stage timings … backend selection,
  errors, focus/visibility transitions — never payloads").

Removing both together left the application with **no channel at all** for saying
what it was doing. Measured on `main.qml` @ `83d40cd`: six computed states —
decoding, warming up, too short, stalled, decoder-dead — and one call site, the
opt-in overlay. By default a 480 ms decode, a glide the decoder refused, and a
decoder that never loaded were indistinguishable from a working keyboard doing
nothing. (See [ux-and-visual-review.md](../ux-and-visual-review.md) F1/F2.)

**Amended decision:** structure returns to the default surface; content does not.

- A **state pill** in the chrome band is the sanctioned home for machine state.
  Any new state indicator goes there — not into the diagnostics line.
- It may carry **no payload**. A decoded word, a candidate, the greedy string,
  the injected mirror, caret coordinates and queue depth stay behind
  ⋯ → Diagnostics. This is the test for anything added later: if it has to
  interpolate something the user typed, it is not eligible.
- **It costs no layout.** The pill borrows the two leftmost chip slots rather
  than claiming a row, so §1's headline number — the glide surface at 60% of the
  window — is unchanged. Precedence in those slots: state pill > recent-word
  chips (the slots fill oldest-first, so the two it covers are the two least
  useful) > empty. Chips never *shift*; they yield. A chip that moves is a chip
  you have to re-find, which is the §1 rule this preserves.
- Transient states expire (2.6 s). A note that outlives its cause becomes
  furniture and stops being read.

This does not reopen §1's geometry, only its treatment of the status text.

## Verification gates

- [x] **Zero-diff geometry data.** `languages/en/layout.json` compared
      field-by-field against the built-in `C[26][2]` table: 26/26 keys identical,
      so installing the file changes no model input. (The *runtime* half — that
      the loaded layout reaches the engine on a real run — is part of the
      on-hardware gate below.)
- [x] **`set_layout` cannot corrupt the decoder.** Verified against the extracted
      implementation: 26 unique letters accepted (upper- or lowercase), 25 keys /
      duplicate letter / non-letter label all rejected, and the previously
      installed geometry survives every rejection.
- [~] **Aspect band measured, not assumed.** **Pre-check run 2026-08-11: no
      cliff** — `corpus_test --sweep` held top-1 at the 94% corpus baseline across
      the whole band, 10:5 (tall) to 10:1.8 (wide), latency flat (RESULTS.md).
      **Decision: resizing stays unclamped** (§3). The gate stays open rather than
      closed, because the transform under-tests the dangerous case: it scales the
      residual from a *locally straight* path, and trailing overshoot — the
      mechanism RESULTS.md identifies as decode-corrupting — is a smooth
      excursion, so a local line fit treats it as intended and never amplifies it.
      A flat result there is weak evidence about the one failure mode that
      matters — and is the likeliest reason every row came out identical.
      **The key-centre-polyline model is now built and is the default**
      (`--model word`, `reaspect.h`): overshoot lands past the end of the
      polyline, so it registers as deviation and scales (verified 1.56× at
      k=1.67, vs 1.00× for the old model, `aspect_model_test.cpp`).
      **Re-run `--sweep` with it.** If it stays flat, the unclamped decision is
      real evidence; if not, the band needs clamping. Real glides captured at the
      edge aspects remain what finally closes this.
- [ ] **Zero-diff model inputs.** After the wings/action-row change, assert the
      26 normalized letter centers are byte-identical to F3, and re-run
      `corpus_test` for an unchanged 17/18.
- [ ] **Focus preservation survives the new window states.** Re-run the
      `surface-probe` focus check (RESULTS.md §surface-probe) against Full,
      Collapsed, and post-restore-from-Hidden — `WindowDoesNotAcceptFocus` is
      verified on XWayland today but not for a window that hides and re-shows.
- [x] **Steps 0–2 build and render.** Verified on hardware 2026-08-11. One defect
      surfaced and was fixed there: `og_ibus_backspace` blocked the caller up to
      500 ms per delete, which under hold-⌫ repeat (70 ms) piled up and froze the
      session; it is now fire-and-forget, ordered by D-Bus.
- [ ] **Step 3 builds and renders.** `ToggleListener` and the Hidden state were
      written on a machine with no Qt Quick: qmllint-clean, `-fsyntax-only` clean
      against real Qt 6 headers, and the chord state machine passes 13 standalone
      cases (both orders fire; ordinary clicks, two separate clicks, a late second
      button, an early release, and a long LMB glide followed by RMB all do not;
      one chord fires exactly once and re-arms only after a full release). Not yet
      compiled or run against a real `/dev/input`.
- [ ] **The mouse-only loop closes.** With no physical keyboard touched:
      collapse, restore, resize by preset, resize by grip, move, dock, hide, and
      restore from hidden. Any step that requires a keyboard is a failure of
      spec §2.1's "extremely easy to show and hide using the mouse alone".
- [ ] **`set_cursor_location` actually reports** on GNOME/IBus for a real GTK
      and a real Electron client before caret-avoidance is designed against it
      (`tools/ibus-engine-probe/` is the right place to answer this).

## References

- `tools/qt-prototype/main.qml` @ `a34e9f7` — lines cited throughout.
- `tools/qt-prototype/src/swipesurface.cpp:48-57` — tap threshold (F5).
- `tools/native-decode-spike/swipe_engine.cpp:50-56` — hard-coded key centers (F4),
  and `decode()`:245-258 — the overshoot adapter whose asymmetry motivates §3.
- `tools/qt-prototype/src/ibus_engine.c:73-75` — engine vfuncs, no
  `set_cursor_location` (F7).
- [openglide_technical_spec.md](../openglide_technical_spec.md) §2.1, §7.2,
  §7.3, §7.4, §9.3, §13, §23, §34, §36.
- [ADR-0004](0004-diagnostics-and-operations.md) — why the status line and
  committed mirror are opt-in diagnostics, and the observe-only rule for the
  §13.2 global listener.
- [../../tools/RESULTS.md](../../tools/RESULTS.md) — overshoot asymmetry;
  corpus baseline 94%; focus-preservation probe.
