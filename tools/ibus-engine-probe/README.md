# ibus-engine-probe — validate ADR-0002's primary text-output path on GNOME

The `text-output-probe` closed every cell **except** GNOME UTF-8 commit:
GNOME/mutter does not advertise `zwp_input_method_manager_v2`, so the IME
commit path on GNOME runs through **IBus**. This probe proves that an OpenGlide
IBus engine can commit **arbitrary UTF-8** (`blåbær æøå 日本 🫐`) to the focused
field via `ibus_engine_commit_text`, and that focus is retained — the literal
ADR-0002 verification gate.

## What it does
Registers a throwaway engine `openglide-probe` with the running `ibus-daemon`,
activates itself as the global engine, commits the test string on `enable`,
restores the previous global engine, and exits. **No installed files, no
permanent session changes.**

## Build / run
```
make
# focus a text field first (e.g. open gnome-text-editor and click in it):
./ibus-probe                        # commits the default test string
./ibus-probe "any utf-8 like 日本語 ☺"
```
Exit 0 = committed; 2 = activated but `enable` never fired (no focused IM
context); 3 = could not connect to ibus-daemon.

## What "passes"
The test string appears **exactly** in the focused field and the field keeps
keyboard focus. Backends `xtest`/`uinput` (in `../text-output-probe`) cannot do
this for non-ASCII on a non-matching layout — that asymmetry *is* the point of
ADR-0002's inverted hierarchy.

## Forward path (not in this probe)
The real engine ships as a component (an installed `.engine`/XML + the binary
on `command_line`) so ibus-daemon can D-Bus-activate it; the Qt keyboard app
talks to it (D-Bus method) to commit decoded words, with `uinput` as the
layout-bound fallback.
