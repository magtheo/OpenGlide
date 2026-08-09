# OpenGlide probe results

Empirical output of the `tools/text-output-probe` and `tools/surface-probe` runs.
These convert ADR-0002's architecture from speculation into measured facts.
Append rows as more compositors / sessions are tested.

## Environment under test
- **Session:** GNOME on Wayland (mutter), with XWayland at `DISPLAY=:0`.
- **Compositors installed here:** `gnome-shell`, `Xorg`, `Xephyr` only — no Sway / Hyprland / Plasma.
- **Active XKB layout:** Norwegian (so `å æ ø` exist as physical keys; `日 本 🫐` do not).
- `/dev/uinput` accessible (group `input`).

## text-output-probe
Build: `cd tools/text-output-probe && make` → `./text-probe`. Dry-run by default; `--fire` emits events.

### `xtest` backend (XWayland) — TESTED ✅
`text-probe --backend xtest --text "blåbær æøå 日本 🫐"` → **injectable=12, uninjectable=3**.

| char | cp | keysym | keycode | result |
|------|----|--------|---------|--------|
| å | U+00E5 | 0xE5 | 34 | injectable (on Norwegian layout) |
| æ | U+00E6 | 0xE6 | 48 | injectable |
| ø | U+00F8 | 0xF8 | 47 | injectable |
| 日 | U+65E5 | 0x10065E5 | — | **NO keycode — uninjectable** |
| 本 | U+672C | 0x100672C | — | **NO keycode — uninjectable** |
| 🫐 | U+1FAD0 | 0x101FAD0 | — | **NO keycode — uninjectable** |

**Finding:** raw-key injection is **layout-bound**. A character is injectable iff it maps to a keycode on the active XKB layout; characters absent from the layout (CJK, emoji, arbitrary Unicode) are impossible. This is more precise than "ASCII-only": it scales with the active layout but never reaches full Unicode. Confirms ADR-0002.

### `uinput` backend — TESTED ✅
ASCII table by design (probe under-reports `å/æ/ø` which the layout *could* map to evdev codes). The underlying limit is identical to XTest: evdev has no Unicode codepoints, so injection is layout-bound.

### `im2` backend (Wayland `zwp_input_method_v2`) — HEADLINE ✅
`text-probe --backend im2` connects and dumps the mutter registry. Key observations:

- mutter advertises `zwp_text_input_manager_v3` (the **application** side) — apps can receive committed text.
- mutter does **NOT** advertise `zwp_input_method_manager_v2` — a generic client **cannot** bind the input-method peer directly.
- mutter does **NOT** advertise `zwlr_layer_shell_v1`.

**Finding:** on GNOME Wayland, OpenGlide cannot use `input-method-v2` directly; the seat's input-method slot is mediated by GNOME's IBus integration. **OpenGlide on GNOME must commit UTF-8 via the IBus path** (ADR-0002's "GNOME → flows through IBus" branch), now empirically grounded. (On Sway / Hyprland / wlroots, `input-method-v2` is expected to be advertised to clients — run this same probe there to confirm.)

> Caveat: the IBus `commit_text` backend is **not yet implemented** (needs `libibus-1.0-dev`, which requires `sudo` to install). The GNOME UTF-8 commit is therefore *architecturally determined*, not yet *empirically witnessed landing in an app*.

## surface-probe (X11/XWayland) — TESTED ✅
Build: `cd tools/surface-probe && make` → `./surface-probe [seconds]`.

Maps an override-redirect window, then is clicked twice via `xdotool` while keyboard focus is monitored.

- Probe window `0x1200001` mapped (`override_redirect=True`) on `:0`.
- Initial keyboard focus = `0x600003`.
- Two `ButtonPress` events received (at 130,80). After each, **focus stayed `0x600003`**.
- `focus_is_probe = no` for the entire 6 s run.

**Finding:** an override-redirect X11 window receives mouse input **without stealing keyboard focus** from the target app. The X11 / XWayland focus-preservation mechanism (spec §17) works as designed.

> GNOME-native Wayland surface: no `wlr-layer-shell` (registry-confirmed) → GNOME does not support layer-shell; its OSK/IME popup uses a GNOME-private path. The layer-shell surface implementation is therefore **pending a wlroots compositor** (Sway/Hyprland) to build and test against.

## Matrix status

| Compositor       | text-commit path                  | surface/focus path        | status |
|------------------|-----------------------------------|---------------------------|--------|
| GNOME Wayland    | IBus (im-v2 **not** exposed)      | no layer-shell            | ✅ tested |
| X11 / XWayland   | xtest/uinput (layout-bound)       | override-redirect ✅      | ✅ tested |
| Sway             | input-method-v2 (expected)        | wlr-layer-shell           | pending |
| Hyprland         | input-method-v2 (expected)        | wlr-layer-shell           | pending |
| KDE Plasma       | Fcitx5 / own IME                  | LayerShellQt              | pending |

## FUTO decoder spike (spec §35 / Risk 3)

Script: `tools/futo-spike/validate.py` (Python; venv at `tools/futo-spike/.venv`).
Setup: `python3 -m venv tools/futo-spike/.venv && .venv/bin/pip install --index-url https://download.pytorch.org/whl/cpu torch && .venv/bin/pip install executorch huggingface_hub numpy`.
Run: `HF_HOME=$PWD/tools/futo-spike/models .venv/bin/python tools/futo-spike/validate.py`.

### Baseline — PASS
Dictionary-free greedy decode (encoder-only) of the model-card sample trajectory yields **"computer"**. The FUTO stack runs operationally — ADR-0001's GO is now empirically grounded, not just legally permitted.

### Robustness (greedy decode under perturbation)

| perturbation | result | margin |
|---|---|---|
| spatial Gaussian σ=0.005 / 0.01 / 0.02 | `computer` | tolerates ~2% noise |
| spatial Gaussian σ=0.04 / 0.08 | `vompuer` / `vpmpirtre` | breaks ~0.03 |
| subsample to 40 / 27 / 18 / 13 pts | `computer`×3 / `comper` | tolerates ~18 raw pts |
| timing jitter 20 / 50 / 100 / 200 ms | `computer`×2 / `compouer` / `compter` | tolerates ~50 ms |
| speed warp 0.5× / 2× / 4× | `computer`×3 | fully tolerant |

**Reading:** the encoder is shape-driven and time-normalized — robust to sparse sampling and speed variation (both relevant to mice), moderately robust to spatial noise and timing jitter. Encouraging for the mouse case, but the real mouse-vs-finger answer needs actual glides.

### Real mouse glides (the actual Risk-3 experiment) — PASS
Recorder `recorder.py` (tkinter QWERTY grid) + offline `dictionary_decode.py` (CTC word scoring over `/usr/share/dict/american-english`, 71k words). 26 glides captured.

| metric | result |
|---|---|
| greedy exact-match (dictionary-free) | 16/26 = 62% |
| **dictionary top-1** | **21/26 = 81% overall; 21/21 = 100% on clean glides** |
| dictionary top-3 / top-5 | 81% / 81% |
| decode latency (per glide) | ~5 ms (well under the 100 ms p95 budget, ADR-0003) |

The dictionary recovered every greedy near-miss: `geen→green`, `tavble→table`, `helo→hello`, `riber→river`, `compuer→computer`.

**The 5 misses (glides 2–6) are a capture anomaly, not a model failure:** their recorded paths all physically cross the R-I-V-E-R key cluster (`rtuijbvrer`, `rtuibvfer`, …) despite different prompts, so FUTO correctly decoded the captured stroke as "river." Glides 1 and 7–26 capture target-matching strokes and decode perfectly. Root cause of the early-session quirk is unconfirmed (planned: live in-glide path display + X pointer grab for out-of-window capture in recorder v2).

**Conclusion:** FUTO decodes real mouse trajectories with dictionary at **100% top-1 on clean captures** at ~5 ms. Risk 3 is answered affirmatively — the project's core assumption holds on mouse input. Corpus saved at `tools/futo-spike/corpus.jsonl` (data-formats.md JSONL schema) for regression use.

### Start-position hypothesis — CONFIRMED (controlled re-collect)
Re-ran with the first letter of each prompt highlighted and the instruction "start the glide on the highlighted key" (`corpus-controlled.jsonl`, 18 glides). Every glide started on the correct first key, the "everything → river" anomaly vanished, and path-traces matched targets (window→`wetyui`, stone→`sdtyonbre`, place→`pljfacfe`, …). Result: **dict top-1 = 17/18 (94%)**; the single miss was a too-short stroke, not a start-position failure → 17/17 on full strokes.

**Product insight (mouse-specific):** because the cursor persists between words (a finger lifts; a mouse cursor does not), reliable decode requires beginning each glide on the target's first letter. The product UX must make this natural, or the decoder must tolerate mid-keyboard starts. (Phone swipe gets this for free; mouse glide typing does not.)

### Spec-aligned capture surface (§4.2 / §6.2 / §6.3) + overshoot finding
`tools/swipe-capture/` — a minimal C/`Xlib` `SwipeSurface`: on LMB press it `XGrabPointer`s the pointer (capture continues outside the window, §6.2) and records **unclamped** normalized coords (overshoot preserved, §6.3). Validated end-to-end: dictionary top-1 = **9/9 (100%)** on captured glides (`capture.jsonl`) — the spec-aligned capture path produces trajectories the decoder handles perfectly.

**Overshoot probe (`overshoot_probe.py`) refines §6.3:** the encoder tolerates *leading* overshoot (start before the first key, x→−0.3 → still `computer`) but **trailing and mid-trajectory overshoot corrupt decode** (tail to x=1.3 → `computep`; mid spike x>1.1 → `comppter`). This is mouse-specific — phone-swipe overshoot is usually at the start (tolerated), but mouse momentum produces trailing overshoot. **Conclusion: keep the collector unclamped (§6.3 stands for capture), but the decoder adapter must handle trailing/mid overshoot** (clamp to `[0,1]`, or drop trailing out-of-bounds points, before inference). Heads-up for the real `SwipeDecoder` adapter.

## How to run on another session
```
cd tools/text-output-probe && make
# focus a text field in some app first; then:
./text-probe --backend im2   --text "blåbær æøå 日本 🫐"   # Wayland primary path
./text-probe --backend xtest --text "blåbær æøå 日本 🫐"   # add --fire to actually type
cd ../surface-probe && make
./surface-probe 6            # click the white window; watch focus_is_probe stays "no"
```
Record per cell: did the string land **exactly**? did focus stay on the target?

## Build prerequisites (fresh machine)
- `gcc`, `wayland-client` dev, `wayland-scanner`, `libx11-dev`, `libxtst-dev`.
- `/dev/uinput` write access (group `input`, or a udev rule).
- IBus backend (GNOME UTF-8 path): `libibus-1.0-dev` — **needs `sudo`**, not yet implemented.
- Qt6 dev packages only needed for the eventual Qt UI wrapper, not for these protocol probes.

## Blocking follow-ups
1. **IBus backend** (`libibus-dev`, sudo) to actually witness UTF-8 commit land in GNOME apps — the one unverified cell on this machine.
2. **Sway / Hyprland / KDE sessions** to fill the remaining matrix rows (input-method-v2 + layer-shell).
3. Qt6 dev install for the Qt UI wrapper (optional, later).
