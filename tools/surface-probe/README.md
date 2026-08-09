# surface-probe

**Goal:** answer *"can the keyboard stay visible, accept mouse input, and never
steal keyboard focus from the target app?"* Decision context: spec §17, ADR-0002.

## Hypothesis
`Qt::WindowStaysOnTopHint` is insufficient on Wayland. The correct mechanisms are
surface-role protocols: `wlr-layer-shell` (wlroots / Hyprland / Sway), KDE's
`LayerShellQt`, GNOME's IME/popup path, and ordinary focus-disabled windows on
X11. Precedent: Squeekboard (layer-shell + virtual-keyboard + input-method-v2).

## What to build
Minimal always-on-top Qt window that accepts mouse clicks, tried under each
mechanism per compositor. Open a second app (e.g. a Firefox text field), click
the probe, and verify Firefox retains keyboard focus.

## Test matrix
KDE, GNOME, Sway, Hyprland, X11.

## What to record (per compositor)
- visible and clickable?
- keyboard focus retained by the target app?
- mechanism used (layer-shell / LayerShellQt / input-popup-surface / X11 no-focus window)
- pointer-grab behavior when the cursor leaves the surface

## Prerequisites / blockers (this environment)
- Qt6 dev packages (see `../text-output-probe/README.md`).
- `LayerShellQt` (KDE) availability; `qt6-wayland-dev`.
- Vendor `wlr-layer-shell-unstable-v1.xml`.
