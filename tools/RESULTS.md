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
