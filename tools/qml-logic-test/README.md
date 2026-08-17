# qml-logic-test — unit tests for main.qml's text model and visible state

Runs the keyboard's logic with no Qt, no display, no decoder, and no IBus:
`extract.py` pulls the named functions **verbatim** out of
`../qt-prototype/main.qml`, and two suites execute them against stubs.

```
./run.sh          # needs python3 + node
```

| suite | asserts | what it protects |
|---|---|---|
| `history_test.js` | 85 | the text model — the mirror never drifts from the target buffer |
| `state_test.js`   | 41 | the visible state — every glide outcome resolves; note precedence |

## Why this exists

The one invariant that must never break:

> `injected` (the mirror QML keeps of the focused field) must always equal what
> the injector calls actually produced in that field.

Every correction is computed from the mirror — "delete `n` characters, retype
this" — so the moment the two drift, corrections start eating the wrong text.
That failure has already happened once in this project (`26e5c68`, delete/undo
corruption) and it is invisible until you are three words downstream. So the test
rebuilds the target buffer purely from `injector.*` calls and asserts it matches
the mirror after every operation, alongside checking that each history entry
still points at its own word.

Recent-word correction (spec §9.3) made this sharper: history entries hold
absolute offsets into the mirror, so replacing a word has to shift every later
entry by the length delta, and any manual edit has to invalidate whatever no
longer lines up.

## Why the state suite exists

A second invariant, added when the state pill made these states visible
(ADR-0005 amendment, 2026-08-17):

> Every exit from a finished glide must leave the UI in a state that **resolves**.

A glide the decoder refuses emits no `candidatesReady` — not when the engine is
still loading, and not when a decode is already running (`DecoderBridge::decode`
returns `false`). Anything that sets `pending` on those paths hangs the keyboard
until the 20 s watchdog, and the watchdog's own message used to render only in
the opt-in diagnostics overlay. That was the shipped behaviour until this suite
existed; `state_test.js` was **mutation-checked against it** and fails 10
assertions there, including `pending` left set on both refusal paths.

The second half is the note precedence order — a dead decoder outranks a stalled
decode outranks a short stroke — plus the ADR-0004 §1 rule that no note may
interpolate user content.

## Covered — text model (`history_test.js`)

- three glides build three history entries with correct offsets
- correcting the newest word (the candidate-slot path)
- **correcting an older word** — was impossible before; retypes the suffix
- replacements that grow and shrink the word, shifting later offsets
- deleting a middle word, swallowing one adjacent space
- punctuation collapsing a space, then correcting the word before it
- manual backspacing invalidating a damaged entry instead of mis-targeting it
- the word-delete gesture
- capitalization surviving a correction
- the `histMax` cap

## Covered — visible state (`state_test.js`)

- a full stroke hands off, sets `pending`, arms the watchdog
- a glide refused by the busy guard: `pending` NOT set, watchdog NOT armed
- a glide before the decoder is ready: same, and the decoder is never called
- a stroke under 4 points reports "too short" without calling the decoder
- every outcome leaves a note the user can read (the resolve invariant)
- a full stroke clears a previous "too short"; the expiry timer is armed
- note precedence across all six states, and progress vs. problem
- no note interpolates a decoded word, a candidate, or typed text

## Note

`extract.py` reads the real source rather than keeping a copy, on purpose — a
duplicated copy drifts and then the test passes while the app is broken. It
matches `^    function <name>(` at the QML root indent and is brace-balanced, so
if those functions are re-indented or moved into a sub-object it will fail loudly
(`not found in …`) rather than silently testing nothing.
