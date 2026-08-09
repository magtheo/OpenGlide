# text-output-probe

**Goal:** answer *"does arbitrary UTF-8 commit correctly across compositors and
apps?"* before building OpenGlide. Decision under test: ADR-0002.

## Hypothesis
Native IME / text-commit paths (`zwp_input_method_v2` on Wayland; IBus / Fcitx on
X11) deliver arbitrary UTF-8 reliably. Raw-key injection (uinput / XTest) does
not. There is **no single universal Wayland backend** — each desktop likely needs
its own wiring underneath one shared `TextBackend` abstraction.

## What to build
Minimal Qt UI: a text field preloaded with `blåbær æøå 日本 🫐`, plus one
**COMMIT** button per backend. No swipe, no FUTO. Each backend commits the test
string to the currently focused application.

Backends to implement:
- Wayland: `zwp_input_method_v2` `commit_string`
- Fcitx 5: `commit_text`
- IBus: `commit_text`
- uinput (raw keys)
- XTest (raw keys)

## Test matrix
- **Compositors:** KDE Wayland, GNOME Wayland, Sway, Hyprland, X11
- **Apps:** Qt field, GTK field, Firefox, Chromium, VS Code / Electron, LibreOffice, terminal

## What to record (per cell)
- exact / partial / failed / wrong-encoding
- whether keyboard focus stayed on the target app
- backend used and any fallback triggered

## Prerequisites / blockers (this environment)
- **Qt6 dev packages not installed** — only runtime libs are present
  (`libqt6*6t64` 6.4.2, `qt6-wayland`). Need `qt6-base-dev`,
  `qt6-declarative-dev`, `qt6-wayland-dev`, and the relevant QML modules.
- System `wayland-protocols` ships input-method-**v1** only (no v2) → vendor
  these XMLs at build time: `input-method-unstable-v2`, `text-input-unstable-v3`,
  `virtual-keyboard-unstable-v1`.
- `/dev/uinput` write permission (udev rule or `input`/`uinput` group) for the
  uinput backend.
