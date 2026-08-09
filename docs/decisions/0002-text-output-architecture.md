# ADR-0002: Text-output architecture — commit UTF-8 first, raw keys as fallback

- **Status:** Accepted
- **Date:** 2026-08-09
- **Supersedes:** the framing in technical spec §14–§15 ("uinput = universal output")

## Context
The spec treated Linux `uinput` as the universal output backend. `uinput`, X11
`XTest`, and the Wayland `virtual-keyboard` protocol all inject **scancodes /
key events against a keymap** — not text. This is fine for ASCII on a matched
XKB layout and breaks for:

- Norwegian (`æ ø å`),
- emoji,
- anything Whisper transcribes (arbitrary UTF-8 by construction),
- any case where the active keyboard layout lacks the target character.

This is the single largest correctness hole in the original spec.

## Findings (primary sources)
`zwp_input_method_v2` is the Wayland protocol an **input method** uses to talk to
the compositor. (OpenGlide is the IME *peer*; `text-input-v3` is the
*application* peer — the original review conflated the two.) From the protocol:

- `commit_string(string)` — "Send the commit string text for insertion to the
  application. Inserts a string at current cursor position." Strings are
  **UTF-8**; **4000-byte max per message**.
- `set_preedit_string` — composition / live preview.
- `delete_surrounding_text(before, after)` — the correction/backspace path, in bytes.
- `surrounding_text` event — the app sends us text around its cursor → enables
  replace-previous-word without guessing offsets.
- Constraint: **at most one input-method object per seat.**
- Status: experimental; backward-incompatible changes may occur → isolate behind
  a backend and vendor the protocol XML.

On X11, IBus exposes equivalent `commit_text`, surrounding-text, preedit, and
delete facilities; `XTest` is the raw-key fallback there.

## Decision
`TextBackend` remains the single OpenGlide abstraction, but the hierarchy is
**inverted** from the spec:

```
                       TextBackend
                            │
     ┌──────────────────────┼──────────────────────────┐
     ▼                      ▼                          ▼
 WaylandInputMethod    Fcitx / IBus              RawKeyBackend
 Backend               Backend                       │
 (zwp_input_method_v2) (commit UTF-8)          ┌─────┴─────┐
     │                      │                  │           │
     ▼                      ▼                uinput       XTest
 arbitrary UTF-8        arbitrary UTF-8       raw keys    raw keys
 surrounding text       surrounding text
 delete / preedit       delete / preedit
```

1. **Primary path commits arbitrary UTF-8 natively**, wherever an IME/text-input path exists.
2. **Raw-key injection (uinput / XTest) is a fallback for keyboard semantics**
   (shortcuts, Tab/Enter/Backspace where no IME path exists) — **not** the universal text path.
3. Runtime backend selection; one abstraction, possibly several compositor-specific
   implementations underneath. There is likely **no single universal Wayland
   backend** (Sway/Hyprland → input-method-v2; GNOME → flows through IBus; KDE →
   its own IME setup). The `text-output-probe` maps what actually works.

## Consequences
- Correction (spec §9) uses `delete_surrounding_text` + `surrounding_text` on the
  rich path; degrades to `backspace × word_length` on the raw-key fallback — the
  spec §9.2 split, now grounded in a real protocol.
- Long Whisper transcriptions must be **chunked to < 4000 bytes** before `commit_string`.
- Surrounding-text math is in **bytes** (per protocol), so the storage layer treats
  text as bytes, not codepoints — see `docs/data-formats.md`.
- Probe results may force per-compositor backends; acceptable and expected.

## Verification gates
- [x] **GNOME / IBus — validated 2026-08-09:** `tools/ibus-engine-probe` registers a
      throwaway engine with the running ibus-daemon, activates it as the global engine,
      and commits `blåbær æøå 日本 🫐` via `ibus_engine_commit_text`. The string lands
      **exactly** in a focused GTK text field; keyboard focus is retained; the previous
      engine is restored. Automated assertion (a GTK sink self-triggers the probe on focus).
      GNOME/mutter does not expose `zwp_input_method_v2`, so IBus is the GNOME cell.
- [ ] Sway / Hyprland / KDE cells of the compositor × app matrix (input-method-v2 there).

## References
- input-method-v2: https://wayland.app/protocols/input-method-unstable-v2
- virtual-keyboard (raw events, not text): https://github.com/swaywm/wlroots/blob/master/protocol/virtual-keyboard-unstable-v1.xml
- IBusEngine: https://ibus.github.io/docs/ibus-1.5/IBusEngine.html
- XTest: https://www.x.org/releases/X11R7.5/doc/man/man3/XTestFakeKeyEvent.3.html
- Fcitx on Wayland (per-DE differences): https://fcitx-im.org/wiki/Using_Fcitx_5_on_Wayland
