# qml-logic-test — unit tests for main.qml's text model

Runs the keyboard's text-editing logic with no Qt, no display, no decoder, and no
IBus: `extract.py` pulls the named functions **verbatim** out of
`../qt-prototype/main.qml` and `history_test.js` executes them against a
simulated target application.

```
./run.sh          # needs python3 + node
```

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

## Covered

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

## Note

`extract.py` reads the real source rather than keeping a copy, on purpose — a
duplicated copy drifts and then the test passes while the app is broken. It
matches `^    function <name>(` at the QML root indent and is brace-balanced, so
if those functions are re-indented or moved into a sub-object it will fail loudly
(`not found in …`) rather than silently testing nothing.
