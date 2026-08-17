# OpenGlide — UX & visual review

**Status:** Analysis + brainstorm (not a decision — nothing here is settled;
candidates for ADR-0007 and the Phase 2 "remaining" list are marked ★)
**Implemented so far:** step 1 of §4 — **B1, B2, B3, B9** landed 2026-08-17
(plan.md Phase 2 item 10), closing **F1, F2, F3** and the menu half of **F8**.
Logic-tested (`tools/qml-logic-test`, 85 + 41 assertions), not yet compiled or
rendered — the box it was written on has no Qt.
**Date:** 2026-08-17
**Subject:** `tools/qt-prototype` @ `83d40cd` — `main.qml` (1369 lines) and the
C++ items it drives.
**Reads against:** spec §8/§9/§17/§23/§26/§36, [ADR-0004](decisions/0004-diagnostics-and-operations.md),
[ADR-0005](decisions/0005-keyboard-layout-and-window-ux.md),
[ADR-0006](decisions/0006-decode-accuracy-strategy.md), [RESULTS.md](../tools/RESULTS.md).

---

## 0. Summary

ADR-0005 fixed the **geometry** problem and fixed it well: one unit `u`, the
letter block edge-to-edge, 60% glide surface, no letterbox, `×` no longer quits.
That work is done and should not be re-opened.

What is left is a different class of problem. The keyboard now has the right
*shape* and almost no *voice*:

1. **The system never says what it is doing.** Every state the app computes —
   decoding, too-short, stalled, decoder-dead, warming up — is rendered in
   exactly one place: an opt-in diagnostics line (`main.qml:1023`). By default,
   glide → 480 ms of nothing → a word appears. Or nothing appears, forever, with
   no explanation. This is the single biggest gap and the cheapest to close.
2. **The one interaction rule that measurably determines accuracy is untaught.**
   RESULTS.md ("Start-position hypothesis — CONFIRMED") shows top-1 goes 81% →
   94% when the glide starts on the target's first letter. The UI does nothing to
   make that natural. Nothing.
3. **The visuals are unowned.** One hardcoded light palette, ~10 hex literals
   that bypass it, no dark mode, no font choice, keys separated from their
   background by a 1.2:1 tonal step, hard corners, no shadow, no motion beyond a
   70 ms key-pop.
4. **Gesture depth is invisible.** ⌫ alone has four distinct gestures. None is
   discoverable, and there is no help anywhere in the product.
5. **Real functional gaps for the stated user.** No modifiers (Ctrl/Alt/Tab/Esc)
   — so "operate a computer with the mouse alone" currently excludes a terminal,
   a shortcut, or a dialog you have to Esc out of. No mic slot reserved (Phase 3
   will force a chrome re-layout).

The through-line: OpenGlide's product principle is **minimum effort per
correctly entered word** (spec §36). Effort is not only pointer travel — it is
also *the seconds a user spends not knowing what happened*. Most of what follows
buys back that second kind of effort at very low implementation cost.

---

## 1. What is actually on screen today

```text
┌───────────────────────────────────────────────┐  ← frame: 4–8 px of pal.shell
│ ● [there][three][their][theme]   − + ▾ ⋯      │  chromeH   (candidates OR history OR undo)
├───────────────────────────────────────────────┤
│  Q W E R T Y U I O P                          │  rowH
│   A S D F G H J K L                           │  rowH   ← SwipeSurface (the decoder frame)
│ ⇧  Z X C V B N M   ⌫                          │  rowH
├───────────────────────────────────────────────┤
│ ?123 │ , │      space      │ . │ ⏎            │  actionH
└───────────────────────────────────────────────┘
```

- **Palette** (`main.qml:32-47`): 13 named colors, light only, "FUTO-inspired"
  (reads as Material/Google). Plus `#ffffff`, `#dadce0`, `#c5221f`, `#6b7178`,
  `#9aa0a6` written inline in eight places, outside `pal`.
- **Motion**: key-pop `scale 1.14` over 70 ms; trail opacity fade over 220/420 ms.
  That is the entire motion inventory.
- **Feedback**: nearest-key pop during a glide; live accent-colored trail;
  tap-flash 120 ms; the swipe-delete dot gauge (which is genuinely good — it is
  the one place the app shows the user the consequence of a gesture *before*
  committing to it).
- **Chrome**: an IME dot, four word slots, and four 0.70u glyph buttons
  (`−` `+` `▾` `⋯`), all unlabeled, all inside one ~26 px band.
- **States**: Full / Collapsed (dark puck) / Hidden (invisible, chord to return).

---

## 2. Findings

Ordered by (user pain × cheapness of the fix).

### F1 — All decode feedback is invisible by default ★

`stateText()` (`main.qml:211-219`) computes five user-facing states. Its only
call site is the diagnostics overlay (`main.qml:1023`), which is off by default
per ADR-0004. So by default:

| what happened | what the user sees |
|---|---|
| decoding (20–60 ms clean, 300–480 ms sloppy, 1.7 s outlier — RESULTS.md KDE) | nothing |
| stroke too short (<4 points) | nothing |
| decode stalled (20 s watchdog) | nothing |
| decoder failed to load (model missing) | a full keyboard that silently ignores every glide |
| decoder still warming up | same |

ADR-0004 is right that *content* is diagnostics. But "decoding…" is not content —
it is **structure**, which ADR-0004 §1 explicitly permits at default levels. The
status machinery was moved behind the toggle as a side effect of removing the
debug text band, and the state indicator went with it. That was over-corrective.

### F2 — Two silent-drop paths make the app look broken ★

```qml
// main.qml:846-852
win.candidates = []; win.pending = true; win.timedOut = false;
watchdog.restart();
if (decoder.ready) decoder.decode(points);
```

- If the decoder is **not ready**, `pending` is set and `decode()` is never
  called → nothing resolves it → 20 s of silence → `timedOut` (invisible, F1).
- If a decode is **already running**, `DecoderBridge::decode` returns at the
  stale-discard guard (`decoderbridge.cpp:163-165`) **without emitting anything**
  → identical 20 s silence. Glide two words quickly and the second vanishes.

Both are correct engineering (ADR-0003 stale-discard is deliberate) with no UI
contract behind them. The user's model is "I glided and it did nothing."

### F3 — The start-position rule is measured, decisive, and untaught ★

RESULTS.md: uncontrolled 81% top-1 → controlled-start 94%, and the failure mode
was catastrophic (five different prompts all decoding to `river`). plan.md
already names it: *"the cursor persists between words (unlike a finger), so each
glide must begin on the target's first letter; the UI must guide this."*

Today nothing guides it. The letter keys have **no hover state** — `SwipeSurface`
only emits `cursorMoved` while `swiping` (`swipesurface.cpp:31-35`), so between
words the user has no idea which key they are parked on. The single cheapest
accuracy intervention available to this project is a hover highlight.

### F4 — Key/background separation is 1.2:1

`pal.key #ffffff` on `pal.bg #e8eaed` is a contrast ratio of **1.20:1**, plus a
`kgap` of `u*0.045` (≈2 px at the default size). Against a bright desktop, through
an always-on-top window with no shadow and no border radius, the keys are defined
almost entirely by 2 px of gap. `pal.accent #1a73e8` against white text lands at
**4.50:1** — exactly the AA threshold, with zero margin. Nothing in the project
states a contrast floor, so nothing protects these numbers from the next tweak.

### F5 — No dark mode, no theme following, no font choice

Always-on-top + always-light = a glare panel at night. `pal` is already a single
`QtObject`, so this is close to free structurally; the blocker is the ~10 inline
hex literals that bypass it. No `font.family` is set anywhere except the
diagnostics monospace, so the keyboard's glyphs are whatever the distro's default
sans happens to be — visual identity varies per machine.

### F6 — The chrome row's mode changes are invisible

`chromeSlots` (`main.qml:161-179`) silently switches the same four slots between
three meanings: candidates → recent words → undo offer. The only differences are
fill (accent / white / transparent) and border color. Nothing labels the mode, so
"why did my alternatives turn into different words" has no on-screen answer. The
undo chip solved its own version of this problem by *renaming itself* ("↶ undo
word" after "↶ word" read as a strange suggestion — see the prototype README) —
the same lesson has not been applied to the history/candidate switch.

### F7 — Gesture depth with zero affordance

⌫: tap = delete word · hold = char + repeat · hold+swipe-left = multi-word (with
gauge) · swipe-right = undo. Shift: three states. `?123`: a whole layer. The
window: drag anywhere on chrome, resize by 6 px edges or corners, `−`/`+`, S/M/L.
There is **no help, no tooltip, no first-run coach** anywhere in the product.
A user who does not read the README will find perhaps a third of the app.

### F8 — Failure messages are terse and offer no route forward

- `"Hide — needs /dev/input access"` (`main.qml:1144`) — no mention of the
  `input` group, no copyable command.
- `"Pointer slow — needs GNOME settings"` — accurate, unhelpful.
- `"decoder stopped — restart the app"` — invisible anyway (F1).
- The IME dot is the only backend signal, and it is **actively misleading on
  KDE**: the verified-working uinput path shows the dot muted (`ibusActive`
  false) while text lands perfectly. The user reads "broken" from a working
  system.

### F9 — Window can be dragged mostly off-screen

`moveTo` (`main.qml:69-72`) clamps `x,y ≥ 0` and nothing else. There is no
right/bottom bound, no multi-monitor awareness (caret avoidance reads
`Screen.desktopAvailableHeight` only), and no edge snap or dock (§7.4, still
open). The keyboard can be parked where only its corner is reachable — for a
mouse-only user that is close to unrecoverable without the chord.

### F10 — Four polling timers run continuously at idle

`targetGen` 400 ms, `ibusActive` 800 ms, `preedit`/`caps` 700 ms, `caret` 500 ms
— ≈7 wakeups/second while visible and idle, each crossing into C++, against spec
§26's "low idle resource consumption". They exist because IBus state has no
change signal exposed to QML; one consolidated 250–500 ms tick that fetches all
four (or a C++-side notifier) removes three-quarters of the wakeups.

### F11 — The trail is a flat polyline repainted whole, every sample

`pathCanvas` (`main.qml:804-820`) pushes to `pts` and calls `requestPaint()` per
`cursorMoved`, re-stroking the entire path each time — O(n²) work over a glide,
and `Canvas` is the slowest QtQuick drawing primitive. It also cannot express
velocity, taper, or a gradient, all of which are cheap on a `Shape`/`ShapePath`
or a scene-graph node.

### F12 — No modifiers: "mouse-only computer use" does not yet reach a terminal

No Ctrl, Alt, Super, Tab, Esc, or arrows. Ctrl+C in a shell, Alt+Tab, Esc out of
a dialog, Tab between form fields — all impossible from the keyboard. This is a
**spec-level gap** (§1's premise), not polish, and it is not on the Phase 2 list.

### F13 — The mic slot is not reserved

Every spec mockup (§8, §12.2, §23) puts 🎤 in the candidate row. Phase 3 will
need it. Adding it later re-flows chrome that users will have built muscle memory
against — the fixed-slot property that makes chips clickable-by-position
(ADR-0005) is exactly what a late insertion breaks.

---

## 3. Brainstorm

### Tier 1 — high value, low cost (days, mostly QML)

**B1. A state pill in the chrome. ★**
Reuse `stateText()`, minus content. When the app has something to say, slot 0
becomes a pill: `decoding…` / `too short — glide across more keys` /
`decoder loading… 3 s` / `decoder stopped`. Otherwise chrome is unchanged.
Closes F1 with existing strings; satisfies ADR-0004 (structure, not payload).

**B2. Resolve the silent-drop paths. ★**
- `!decoder.ready` → don't set `pending`; show `decoder loading…`.
- Stale-discard → have `DecoderBridge::decode` return a bool, or emit
  `decodeDropped()`, and flash `busy — glide again`. Closes F2.

**B3. Hover highlight on the key under the cursor. ★**
`hoverEnabled` on the surface, emit `cursorMoved` when not swiping, reuse the
existing `activeKey` pop at lower intensity (say a ring, not a scale). Directly
attacks the measured 81→94% start-position gap (F3) — plausibly the highest
accuracy-per-line-of-code change available anywhere in the project.

**B4. Greedy-first: show the raw letters in ~7 ms. ★**
`SwipeEngine` computes the encoder forward in **7 ms** and then spends 79 ms
(worst case 350–1700 ms) on the dictionary trie. Emit `greedyReady(text)` from
the worker right after `forward`, and render it as a ghost chip that resolves
into the decoded word. The data already exists and is already logged
(`decoderbridge.cpp:183`) — it is thrown away for UI purposes. Perceived latency
drops from "up to 1.7 s of nothing" to "instant, then it sharpens." Best
effort-to-delight ratio in this document.

**B5. Keep the trail alive while decoding.**
Today the trail fades 420 ms after release regardless. Instead: hold it, run a
subtle shimmer/pulse along it while `pending`, dissolve it when the word commits.
The wait becomes attached to the gesture that caused it instead of floating free.

**B6. Label the chrome mode.** A 0.3u leading marker or micro-label —
`alt` vs `recent` — plus distinct chip shapes (pill vs tag). Closes F6 for
roughly nothing.

**B7. Countdown on the undo chip.** A 1 px progress line draining over the 8 s
`undoTimer`. Users currently cannot tell that the offer is expiring.

**B8. Tooltips + a cheatsheet.** Tooltips on `− + ▾ ⋯`, and a `⋯ → Gestures`
panel listing every gesture in the product. One static panel; closes most of F7.

**B9. Actionable failure text.** `Hide — needs read access to /dev/input`, with
`sudo usermod -aG input $USER` shown and a "why" line. Same treatment for the
pointer-speed and decoder-dead cases. (F8)

**B10. Clamp to the screen + snap to edges.** Bound `moveTo` by
`Screen.desktopAvailableWidth/Height`, snap within ~12 px of an edge. Cheap, and
it is the entry point ADR-0005 §4 already names for docked mode (§7.4). (F9)

**B11. Consolidate the pollers.** One 500 ms tick calling all four getters. (F10)

### Tier 2 — the visual direction (a week or two)

**B12. A real palette, tokenized and dual-mode. ★**
Move every inline hex into `pal`; split into `light`/`dark` records selected by a
`dark` bool; read the system preference from the XDG settings portal
(`org.freedesktop.appearance` / `color-scheme`), which works on both GNOME and
KDE, with a manual override in `⋯`. Set a contrast floor as a stated rule —
suggest ≥3:1 for key-vs-background (fixing F4's 1.2:1) and ≥4.5:1 for all text —
and note it wherever the palette is defined, so the next tweak has something to
violate.

**B13. Give the window a body.** Corner radius ~`0.25u`, a soft drop shadow, and
a 1 px outline. Today it is a hard rectangle with a gray `frame` ring that reads
as a 2005 bevel. The frame *is* the resize ring, so this is a re-skin, not a
re-layout: draw the ring as the shadow/outline rather than as flat fill.

**B14. Idle transparency.** ADR-0005 §5 promised it and it was never built: fade
to ~60% opacity after ~4 s without pointer or activity, snap to 100% on enter.
Directly serves "the keyboard must not be obstructive," complements the caret
avoidance that already ships.

**B15. Commit motion tied to the caret.** The app already knows the caret rect
(`injector.caretRect()`, used to *avoid* it). Use it a second way: on commit,
fly a 180 ms ghost of the word from its chip toward the caret. This answers the
most common real confusion — *did my text land, and where?* — with motion instead
of prose, and it is the kind of detail that makes the product feel authored.

**B16. Type: pick a stack and ship it.** Choose an explicit `font.family` list
(e.g. Inter / Cantarell / Noto Sans / DejaVu Sans) so the keyboard looks the same
on Fedora and Ubuntu. Letters slightly heavier and more open than the default UI
face; tabular figures for the symbols layer.

**B17. Trail 2.0 on `Shape`.** Replace the `Canvas` (F11) with a `ShapePath`:
width tapered by velocity, older segments fading along the path, color shifting
subtly on `pending` (B5). Cheaper *and* better-looking.

**B18. Reserve the mic slot now.** Draw it disabled with a "coming in Phase 3"
tooltip, or leave a deliberate `1u` gap at the right of the chrome. Either way,
fix the geometry before muscle memory forms. (F13)

### Tier 3 — structural, needs a decision (ADR-sized)

**B19. Sticky-modifier row / a third layer. ★ (F12)**
`Ctrl Alt Super Tab Esc ← ↑ ↓ →` as sticky modifiers (click Ctrl, then a letter →
Ctrl+C). Two candidate homes: a fourth layer behind `?123`, or a slide-out strip
from the action row. This is what turns "an on-screen keyboard" into "you can run
this computer without a keyboard," which is the actual promise in spec §1.
Note the constraint: it must **not** make the letter block taller — RESULTS.md
puts a top-1 cliff at 83% at 10:5, so extra rows live in the chrome/action bands,
never inside the decoder frame.

**B20. Preedit-first correction (kills the deletion wall).**
RESULTS.md is unambiguous: `delete_surrounding_text` **SIGABRTs gnome-shell**,
forwarded BackSpace is ignored, uinput is target-mismatched. Every correction
path today rests on a primitive that is broken on the project's primary desktop.
The preedit probe already reports per-client `IBUS_CAP_PREEDIT_TEXT`. The UX
this unlocks is better *and* safer: the current word stays uncommitted and
visibly "wet" (underlined in the target app), chips edit it with no deletion at
all, and it hardens on space/punctuation. Spec §34's open question — "whether the
best candidate should always auto-commit" — is really this question. Gate it on
the probe: preedit where declared, current behavior where not.

**B21. A health panel, replacing the diagnostics one-liner.**
Today: one elided monospace line with ten fields. Instead, a small grid — decoder,
text backend (and its cost: *uinput → ASCII only*), target/focus, toggle listener,
caret reports, pointer slowdown — each row a green/amber/red state and a one-line
"what to do." This is where F8's messages belong, and it converts the project's
hardest support questions ("nothing lands", "why is text ASCII") into something a
user can read off the screen. Diagnostics stays opt-in; *health* is not
diagnostics.

**B22. Local, content-free effort metrics.**
Count glides, corrections, re-glides, and words-per-minute locally (structure
only — ADR-0004 §1 permits exactly this). Two payoffs: the user sees their own
accuracy improve, and ADR-0006 gets the *"standing real-glide number"* RESULTS.md
says is missing, from real use instead of a 24-glide capture session. Spec §25
already lists correction rate and mouse-action cost as the metrics that matter;
nothing currently measures them.

**B23. First-run coach.** Three cards over the keyboard, dismissible, once:
(1) hold and drag through the letters — **start on the first letter**; (2) wrong
word? click an alternative; (3) `×` hides, it never quits. Card 1 alone is the
single highest-leverage sentence in the product (F3).

**B24. Auto-commit as a setting.** Spec §34 lists it as open. With B4's greedy
ghost and B20's preedit, "propose, don't commit" becomes viable for users who
prefer confirming — and it sidesteps deletion entirely.

### Tier 4 — speculative, worth arguing about

- **Explain the decode.** After a miss, briefly overlay the key-centre polyline
  of the word it *chose* against the user's actual path. The machinery exists
  (`reaspect.h`'s word model). Teaches the user what a clean stroke looks like,
  and doubles as a debugging tool for the project.
- **Confidence in the chip.** Modulate the top-1 chip's fill by the score margin
  over candidate 2 — a low-confidence word arrives visibly tentative, which is
  when the user should be looking at the alternatives anyway.
- **Next-word prediction in idle slots.** When candidates go stale, the slots
  show history (good). A context model (ADR-0006's n-gram rescoring, once built)
  could instead offer likely next words — one click, no glide. Directly serves
  §36's "minimum effort per correctly entered word."
- **Radial correction menu** at the pointer instead of a chrome popup: zero
  travel, which is the currency spec §36 counts.
- **Layout personalization from the corpus.** The app records where the user
  actually clicks/crosses per letter. Nudging key centres toward the user's own
  aim (within the decoder's tolerance) is a personalization axis nobody else has
  — the geometry is already dynamic (`set_layout`), so the plumbing exists.
- **Identity work is blocked.** Spec §32's name collision with the 3dfx
  OpenGLide wrapper is unresolved, so a logo/wordmark/tray icon would be built on
  a name that may change. Palette, type, motion, and shape (B12–B17) are all
  name-independent and should go first.

---

## 4. Suggested order

| # | Items | Why here |
|---|---|---|
| 1 | B1, B2, B3, B9 | The app stops lying about its own state, and the measured accuracy gap gets its first intervention. All QML, all small. |
| 2 | B4, B5, B7, B6 | Perceived latency and legibility of the chrome. B4 needs one C++ signal. |
| 3 | B12, B13, B14, B16 | The visual pass proper — theme tokens, dark mode, body, type. Needs a design decision, not much code. |
| 4 | B8, B23, B10, B11 | Discoverability, screen bounds, idle cost. |
| 5 | B21, B19 | Health panel; then modifiers, which is spec-level scope and deserves an ADR. |
| 6 | B20, B24, B22 | Preedit + auto-commit + metrics — the correction model rewrite, gated on the probe data. |

Tiers 1–2 are roughly a fortnight of QML with one small C++ signal, and they
address every finding except F12 and the deletion wall.
