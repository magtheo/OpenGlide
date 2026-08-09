# OpenGlide — Technical Specification and Development Plan

**Status:** Initial architecture specification  
**Platform:** Linux  
**Primary interaction:** Mouse-only glide typing  
**Working project name:** OpenGlide (provisional — naming collision must be resolved before public release)  
**Core objective:** Build a native Linux on-screen keyboard that provides high-accuracy neural glide typing, easy correction, offline speech-to-text, and reliable text entry across Linux distributions, desktop environments, Wayland, and X11.

---

# 1. Project Vision

OpenGlide is a native Linux input system designed primarily for people who want to operate a computer using a mouse without needing a physical keyboard.

The central interaction is glide typing:

1. The user presses and holds the left mouse button on the on-screen keyboard.
2. The user moves the mouse through the approximate letters of a word.
3. The gesture may move outside the visible keyboard/window without being cancelled.
4. Releasing the mouse ends the gesture.
5. A neural swipe decoder predicts the intended word.
6. The most likely word is inserted into the currently focused Linux application.
7. Alternative candidates remain immediately accessible for one-click correction.

The goal is not merely to reproduce a phone keyboard on Linux. The system should be optimized specifically for mouse input, desktop applications, correction speed, and cross-distribution compatibility.

---

# 2. Primary Design Goals

## 2.1 Essential goals

The following are hard requirements:

- Mouse-only operation.
- High-accuracy glide typing.
- A glide must continue even when the pointer leaves the keyboard surface or window after the gesture has begun.
- Ranked alternative word candidates must be displayed after every glide.
- Wrong predictions must be correctable with a single mouse action where possible.
- Dictionary-constrained word decoding.
- Personal/custom dictionary support.
- Learning from user word frequency and corrections.
- Speech-to-text button directly accessible from the keyboard.
- Fully functional on Wayland.
- Fully functional on X11.
- Usable across major Linux distributions.
- Usable across major desktop environments and compositors.
- Works with ordinary Linux applications, not only specially integrated applications.
- Offline operation for glide recognition and speech recognition.
- Resizable keyboard.
- Floating and docked modes.
- The keyboard must not steal typing focus from the target application.
- The keyboard must be extremely easy to show and hide using the mouse alone.
- A global mouse-only show/hide mechanism must work even when the keyboard is completely hidden.
- The default proposed global mouse gesture is an LMB+RMB chord held together for approximately 300 ms.
- A configurable global keyboard shortcut must also be available as a secondary toggle.
- Mouse4/Mouse5 or another mouse button should be configurable as alternative toggle inputs.

## 2.2 Strongly desirable goals

- Context-aware prediction.
- Recent-word history for easier correction.
- English and Norwegian support.
- User vocabulary learning.
- Local-only personalization data.
- Native UTF-8 text insertion where possible.
- Graceful fallback where advanced input-method integration is unavailable.
- Low latency.
- Low idle resource consumption.
- No Android or Waydroid dependency.
- No mandatory cloud service.
- No mandatory Fcitx or IBus dependency.

---

# 3. Major Architectural Decision

## Decision

OpenGlide will be a **native Linux application**, rather than FUTO Keyboard running inside Waydroid.

The system will use FUTO's swipe recognition technology as a decoding component, but the surrounding keyboard, gesture handling, correction UI, speech recognition, dictionary system, and Linux integration will be purpose-built for desktop Linux.

### Chosen architecture

```text
Mouse / keyboard UI / microphone
              │
              ▼
        OpenGlide Core
              │
     ┌────────┼─────────┐
     │        │         │
     ▼        ▼         ▼
 FUTO Swipe  Dictionary  Speech
             system      recognition
     │        │         │
     └────────┼─────────┘
              ▼
      Candidate / text engine
              │
              ▼
        Output dispatcher
              │
       ┌──────┴─────────┐
       ▼                ▼
   Rich IME          uinput
   backend           fallback
       │                │
       └────────┬───────┘
                ▼
        Any Linux application
```

---

# 4. Chosen Technology Stack

## 4.1 Programming language

**C++20**

Reasons:

- FUTO Swipe is already implemented as a native C++ library.
- Qt is C++.
- Fcitx is C++.
- whisper.cpp is C/C++.
- Hunspell is C++.
- SQLite has a native C API.
- Linux `uinput` is directly accessible from C/C++.
- Avoids unnecessary FFI layers.
- Good performance and predictable latency.
- Suitable for a long-running desktop input application.

Rust was considered, but rejected for the initial implementation because it would introduce additional C/C++ interop around most of the project's important dependencies.

---

## 4.2 User interface

**Qt 6 Quick + QML**

Responsibilities:

- keyboard rendering;
- key layouts;
- candidate bar;
- history bar;
- speech button;
- floating/docked modes;
- resizing;
- settings UI;
- visual swipe path;
- animations;
- mouse interactions.

Performance-sensitive or input-sensitive components should be implemented in C++ and exposed to QML.

### Gesture surface

A dedicated C++ `QQuickItem`, tentatively named:

```text
SwipeSurface
```

will handle:

- mouse press;
- mouse movement;
- mouse release;
- coordinate collection;
- timestamps;
- pointer grabbing;
- swipe state;
- interaction outside the visible keyboard bounds.

---

## 4.3 Build system

**CMake + Ninja**

Reasons:

- strong Qt support;
- natural fit for C++ dependencies;
- supported by FUTO's C++ components;
- widely available on Linux;
- easy integration with CI.

---

# 5. Glide Recognition

## 5.1 Decoder

**FUTO Swipe**

The FUTO neural swipe stack is the selected recognition engine.

The decoder should receive a full trajectory rather than a simple list of crossed keys.

Example:

```text
(x1, y1, t1)
(x2, y2, t2)
(x3, y3, t3)
...
(xN, yN, tN)
```

This lets the model reason about:

- trajectory shape;
- turns;
- overshoots;
- near misses;
- relative movement;
- timing;
- intended key locations.

This is fundamentally preferable to simpler systems that only collect the sequence of keys crossed by the pointer.

---

## 5.2 Decoder abstraction

OpenGlide should not expose FUTO-specific internals throughout the application.

Use an abstraction such as:

```cpp
class SwipeDecoder {
public:
    virtual std::vector<Candidate> decode(
        const SwipePath& path,
        const KeyboardLayout& layout,
        const TextContext& context
    ) = 0;
};
```

Initial implementation:

```text
SwipeDecoder
└── FutoDecoder
```

This allows alternative or experimental decoders to be added later.

---

# 6. Pointer and Glide Behaviour

## 6.1 Gesture start

A glide begins when:

- the left mouse button is pressed;
- the pointer is inside the active letter area.

At this moment OpenGlide enters an explicit swipe state.

---

## 6.2 Leaving the window

**The glide must not stop when the mouse leaves the keyboard or application window.**

Desired state machine:

```text
LMB press inside keyboard
          │
          ▼
      BEGIN SWIPE
          │
          ▼
collect x/y/time continuously
          │
          ├── pointer remains inside
          │
          └── pointer moves outside
                    │
                    ▼
              KEEP RECORDING
                    │
                    ▼
             LMB released
                    │
                    ▼
                END SWIPE
```

Qt pointer grabbing will be used to preserve ownership of the interaction through release.

---

## 6.3 Out-of-bounds coordinates

Coordinates outside the keyboard should not automatically terminate or necessarily be clamped to the keyboard boundary.

Example:

```text
0.82
0.94
1.04
1.11
1.02
0.91
```

The overshoot can contain useful information for the neural model.

Any normalization or clipping required by the FUTO model should occur in the decoder adapter rather than in the raw gesture collector.

---

# 7. Keyboard Layout

## 7.1 Initial layout

Initial target:

**English QWERTY**

Example:

```text
Q W E R T Y U I O P
 A S D F G H J K L
  Z X C V B N M
```

---

## 7.2 Keyboard geometry

The decoder should receive the current key geometry dynamically.

This allows the keyboard to support:

- resizing;
- different screen resolutions;
- floating mode;
- docked mode;
- alternative layouts;
- future language layouts.

The neural decoder should not assume fixed pixel positions.

---

## 7.3 Floating mode

A compact floating keyboard should be supported.

Benefits:

- shorter mouse travel;
- easier use from bed or a couch;
- less screen obstruction;
- movable to the user's preferred region.

---

## 7.4 Docked mode

The keyboard can alternatively dock to the bottom or another screen edge.

Docking should ideally reserve workspace when supported by the desktop environment, but must have a fallback overlay mode.

---

# 8. Candidate System

The candidate interface is a core feature, not an optional enhancement.

After each glide, the decoder should return several ranked words:

```text
there
three
their
theme
```

The highest-ranked candidate is inserted automatically by default.

The remaining alternatives stay visible.

Example UI:

```text
┌──────────────────────────────────────────────────────┐
│ THERE       THREE       THEIR       THEME       🎤  │
├──────────────────────────────────────────────────────┤
│ Q W E R T Y U I O P                                  │
│  A S D F G H J K L                                   │
│   Z X C V B N M                                      │
└──────────────────────────────────────────────────────┘
```

---

# 9. Correction System

Correction speed is considered almost as important as initial recognition accuracy.

## 9.1 Immediate correction

Example:

The decoder inserts:

```text
there
```

but the user intended:

```text
three
```

The candidate bar still displays:

```text
THREE | THEIR | THEME
```

Clicking `THREE` should replace the previously committed word with one mouse click.

---

## 9.2 Replacement behaviour

The application must remember enough state to replace the previous committed token.

With a basic virtual-keyboard backend this may require:

```text
backspace × word_length
insert replacement
restore trailing separator
```

With a rich IME backend, direct replacement or composition management can be used.

---

## 9.3 Recent word history

OpenGlide should maintain a visible short history of recently entered words.

Example:

```text
I | think | this | should | work
```

Clicking a previous word should provide correction actions such as:

```text
Alternatives
Delete
Replace
Add to dictionary
```

This is specifically optimized for mouse-only use.

---

# 10. Dictionary Architecture

OpenGlide will use multiple dictionary layers because glide decoding and spell correction solve different problems.

## 10.1 Swipe lexicon

Used inside FUTO's dictionary-constrained decoding.

Responsibilities:

- determine valid word candidates;
- assign frequency information;
- constrain neural beam search;
- produce ranked words.

---

## 10.2 Personal dictionary

Stored locally.

Examples:

```text
Kodeverket
Kaplay
PostHog
Tripletex
NTNU
FUTO
```

Personal words must be made available to the swipe decoder, not merely to the spell checker.

---

## 10.3 Spell checker

**Hunspell**

Responsibilities:

- validate manually typed words;
- suggest corrections;
- support language dictionaries;
- provide conventional spelling alternatives.

Hunspell is supplementary to FUTO's swipe lexicon rather than a replacement for it.

---

# 11. Personalization and Local Learning

## Storage

**SQLite**

Proposed database:

```text
openglide.db
```

Possible tables:

```text
personal_words
word_frequency
correction_history
recent_context
language_settings
keyboard_settings
model_settings
```

---

## 11.1 Word frequency

OpenGlide should learn which words the user frequently selects.

Example:

```text
word          usage_count
PostHog       82
Kodeverket    57
Kaplay        44
```

This information can influence candidate ranking.

---

## 11.2 Correction learning

If OpenGlide repeatedly predicts:

```text
there
```

and the user repeatedly replaces it with:

```text
three
```

for similar gestures or context, the system should eventually increase the ranking of `three`.

The exact adaptation algorithm is not yet locked down, but the architecture must make this possible.

---

## 11.3 Privacy

Personalization data should remain local by default.

No cloud account or telemetry service should be necessary for basic operation.

---

# 12. Speech-to-Text

## Engine

**whisper.cpp**

Reasons:

- native C/C++;
- offline;
- Linux support;
- CPU support;
- optional hardware acceleration;
- multilingual models;
- relatively easy embedding.

---

## 12.1 Audio capture

**Qt Multimedia / QAudioSource**

Pipeline:

```text
click microphone
       │
       ▼
 QAudioSource
       │
       ▼
16 kHz mono PCM
       │
       ▼
 whisper.cpp
       │
       ▼
 recognized text
       │
       ▼
 OutputDispatcher
```

---

## 12.2 UI

The microphone button should always be easy to reach.

Example:

```text
THERE | THREE | THEIR | THEME | 🎤
```

Possible interaction:

- click once to start;
- click again to stop;
- optional silence-based automatic stopping later.

A clear listening state should be displayed.

---

## 12.3 Models

Initial model choices may include:

```text
Fast      → tiny multilingual
Balanced  → base multilingual
Accurate  → small multilingual
```

Exact defaults will be benchmarked later.

---

# 13. Show/Hide and Global Activation

Fast visibility control is a hard requirement because OpenGlide is intended to support complete mouse-only computer use.

The user must be able to make the keyboard appear and disappear without reaching for a physical keyboard.

## 13.1 Default mouse-only toggle

The proposed default is:

```text
LMB + RMB
```

held together for approximately:

```text
300 ms
```

to toggle OpenGlide.

Conceptually:

```text
OpenGlide hidden
      │
      │ LMB + RMB chord
      ▼
OpenGlide visible
      │
      │ LMB + RMB chord
      ▼
OpenGlide hidden
```

The exact timing should be configurable after usability testing.

A useful initial detection rule is:

```text
first mouse button down
        │
second button down within ~150 ms
        │
both remain down for ~300 ms
        │
        ▼
TOGGLE OPENGLIDE
```

The order should not matter:

```text
LMB → RMB
```

and:

```text
RMB → LMB
```

should both work.

The hold threshold exists to reduce accidental activation during normal clicking.

---

## 13.2 Hidden-state detection

When OpenGlide is visible, its own Qt input system can recognize the toggle gesture.

When OpenGlide is completely hidden, however, its window cannot be relied upon to receive pointer events.

Therefore the architecture requires a small global input listener for the hidden state.

Conceptually:

```text
Physical mouse
      │
      ▼
 Linux evdev
      │
      ▼
OpenGlide toggle listener
      │
      ├── BTN_LEFT state
      └── BTN_RIGHT state
              │
              ▼
      chord recognized
              │
              ▼
         show OpenGlide
```

The listener should be deliberately small and limited in scope.

---

## 13.3 Event suppression

A first implementation may observe the LMB/RMB chord without suppressing the original button events.

This means the application underneath may also temporarily receive:

```text
left click
right click
```

which could, for example, open a context menu.

The first prototype should measure whether this is sufficiently annoying to justify interception.

If suppression is required, a later input mediation layer can consume the toggle chord while passing ordinary mouse events through unchanged, potentially re-emitting them via `uinput`.

Full mouse interception should not be part of the earliest prototype unless testing shows it is necessary, because it increases input-system complexity and failure risk.

---

## 13.4 Alternative activation methods

OpenGlide should support several configurable activation methods:

```text
Default:
LMB + RMB held together

Optional:
Mouse4
Mouse5
middle mouse button
custom mouse chord
global keyboard shortcut
tray/menu action
```

A side mouse button may ultimately provide the fastest and least intrusive toggle for users whose mice expose one.

The keyboard shortcut is a secondary mechanism, not a requirement for normal operation.

Mouse-only operation must remain fully supported.

---

## 13.5 Complete hiding

OpenGlide must support being genuinely hidden rather than merely minimized or collapsed.

When hidden:

- no keyboard surface should obstruct applications;
- the low-level toggle listener remains active;
- an activation gesture must restore the keyboard immediately;
- speech and swipe workers should not consume unnecessary resources while idle;
- the focused application should remain unaffected.

---

## 13.6 Configuration

Proposed settings:

```text
Show / hide OpenGlide

● Left + Right mouse buttons
○ Mouse4
○ Mouse5
○ Middle mouse button
○ Custom mouse button/chord

Chord timing
Second-button window: 150 ms
Hold duration:        300 ms

Keyboard shortcut
[ configurable ]
```

The LMB+RMB values are initial defaults and should be validated through real use.

---

# 14. Text Output Architecture

Text injection is one of the most important portability challenges.

OpenGlide will therefore use an output abstraction.

```cpp
class TextBackend {
public:
    virtual void commitText(std::string_view text) = 0;
    virtual void backspace(int count) = 0;
    virtual void key(Key key) = 0;
};
```

Possible implementations:

```text
TextBackend
├── UInputBackend
├── FcitxBackend
├── X11Backend        [optional]
└── future backends
```

---

# 15. Universal Fallback: Linux uinput

**Linux `uinput` is the baseline universal output backend.**

OpenGlide creates a virtual keyboard through the Linux input subsystem.

Conceptually:

```text
OpenGlide
    │
    ▼
 /dev/uinput
    │
    ▼
OpenGlide Virtual Keyboard
    │
    ▼
Linux desktop/application
```

Advantages:

- works below X11 and Wayland;
- desktop-environment independent;
- does not require application-specific integration;
- closely resembles ordinary keyboard input from the application's perspective.

---

## 14.1 Permissions

OpenGlide must not run as root.

Installation should configure appropriate permission rules for `/dev/uinput`.

A one-time installation helper may be provided.

Example conceptual flow:

```text
openglide --install-input-permissions
```

The exact mechanism will be determined during implementation.

---

# 16. Rich Input Backend

## Preferred enhanced backend

**Fcitx 5**

Fcitx should be supported as an optional rich IME integration.

Potential benefits:

- direct text commits;
- Unicode handling;
- surrounding-text awareness;
- composition support;
- improved correction;
- cursor and selection awareness;
- candidate integration.

---

## 15.1 Fcitx is not mandatory

Fcitx behaviour and integration vary across:

- KDE;
- GNOME;
- Sway;
- Hyprland;
- Qt apps;
- GTK apps;
- Chromium;
- Electron;
- XWayland.

Therefore:

```text
Fcitx available and working
        │
        ▼
use rich IME backend

otherwise
        │
        ▼
use uinput
```

This prevents OpenGlide from depending on one desktop environment or IME framework.

---

# 17. Focus Behaviour

A central requirement is:

> Clicking OpenGlide must not cause the target application's text field to lose keyboard focus.

Example:

```text
Firefox search box has focus
          │
          ▼
user clicks OpenGlide
          │
          ▼
Firefox remains text target
```

The keyboard window should:

- receive mouse input;
- appear above ordinary windows when desired;
- avoid taking keyboard focus;
- maintain knowledge of the active target application when possible.

X11 and Wayland may require different window-handling strategies.

---

# 18. Wayland and X11

OpenGlide must support both.

## Wayland

Primary targets:

- KDE Plasma;
- GNOME;
- Sway;
- Hyprland;
- other wlroots-based compositors.

Primary fallback:

```text
uinput
```

Enhanced integration:

```text
Fcitx / supported text-input mechanisms
```

---

## X11

Primary fallback:

```text
uinput
```

An optional X11-specific backend may later improve integration if useful.

The project should avoid requiring X11-specific technologies for core operation.

---

# 19. Distribution Independence

The objective is to support common Linux distributions including:

- Ubuntu;
- Debian;
- Fedora;
- Arch Linux;
- Linux Mint;
- openSUSE;
- NixOS;
- Gentoo;
- related distributions.

Absolute zero-dependency universality is not realistic, but the application should avoid binding itself to one distro or desktop stack.

---

# 20. Packaging Strategy

## Initial release

**AppImage**

Initial builds:

```text
OpenGlide-x86_64.AppImage
OpenGlide-aarch64.AppImage
```

AppImage is preferred initially because it allows most non-system dependencies to travel with the application.

---

## Later packages

Potential future distribution formats:

```text
.deb
.rpm
AUR
Nix package
Flatpak
```

Flatpak is not the initial priority because OpenGlide needs low-level input integration and access to the Linux input subsystem.

---

# 21. Language Support

## 20.1 English

English QWERTY should be the first fully supported language.

Expected stack:

```text
FUTO universal encoder
        +
English QWERTY decoder
        +
English swipe lexicon
        +
context model
        +
Hunspell
```

This should provide the best initial recognition quality.

---

## 20.2 Norwegian

Norwegian is a planned requirement.

Initial Norwegian support may use:

```text
FUTO universal encoder
        +
Norwegian key geometry
        +
Norwegian dictionary/trie
        +
Norwegian Hunspell
```

A language-specific contextual model can be added later.

A Norwegian-specific swipe decoder may require additional model work.

Therefore Norwegian support should be architected from the beginning, even if English reaches maximum accuracy first.

---

# 22. Language Pack Architecture

Possible structure:

```text
languages/
├── en/
│   ├── layout.json
│   ├── swipe_dictionary
│   ├── hunspell
│   ├── context_model
│   └── metadata.json
│
└── nb/
    ├── layout.json
    ├── swipe_dictionary
    ├── hunspell
    ├── context_model
    └── metadata.json
```

The system should not hard-code English assumptions into the core.

---

# 23. UI Concept

A possible main interface:

```text
┌─────────────────────────────────────────────────────────────┐
│ I   think   this   should   work                           │
├─────────────────────────────────────────────────────────────┤
│ THERE        THREE        THEIR         THEME        🎤     │
├─────────────────────────────────────────────────────────────┤
│    Q    W    E    R    T    Y    U    I    O    P          │
│      A    S    D    F    G    H    J    K    L             │
│        Z    X    C    V    B    N    M                      │
│                                                             │
│ 123    ↶             SPACE             ⌫     .     ENTER   │
└─────────────────────────────────────────────────────────────┘
```

Important characteristics:

- large glide target;
- short mouse travel;
- clear alternatives;
- visible microphone;
- simple correction;
- optional history;
- minimal requirement for precise clicking.

---

# 24. Suggested Repository Structure

```text
openglide/
│
├── CMakeLists.txt
│
├── app/
│   └── main.cpp
│
├── core/
│   ├── SwipePath.hpp
│   ├── Candidate.hpp
│   ├── TextContext.hpp
│   ├── KeyboardLayout.hpp
│   └── Settings.hpp
│
├── swipe/
│   ├── SwipeDecoder.hpp
│   ├── FutoDecoder.cpp
│   ├── FutoDecoder.hpp
│   └── Dictionary.cpp
│
├── language/
│   ├── SpellChecker.cpp
│   ├── PersonalDictionary.cpp
│   ├── ContextEngine.cpp
│   └── LanguagePack.cpp
│
├── speech/
│   ├── AudioCapture.cpp
│   └── WhisperEngine.cpp
│
├── input/
│   ├── TextBackend.hpp
│   ├── UInputBackend.cpp
│   ├── FcitxBackend.cpp
│   └── X11Backend.cpp
│
├── ui/
│   ├── Main.qml
│   ├── Keyboard.qml
│   ├── Key.qml
│   ├── CandidateBar.qml
│   ├── HistoryBar.qml
│   ├── VoiceButton.qml
│   └── SwipeSurface.cpp
│
├── storage/
│   └── Database.cpp
│
├── languages/
│   ├── en/
│   └── nb/
│
├── models/
│   └── README.md
│
└── tests/
    ├── swipe/
    ├── dictionary/
    ├── correction/
    ├── speech/
    └── input/
```

---

# 25. Testing Strategy

Recognition quality is a primary product requirement, so testing should go beyond unit tests.

## 24.1 Unit tests

Test:

- gesture normalization;
- dictionary lookup;
- candidate replacement;
- Unicode handling;
- personal dictionary updates;
- database operations;
- output backend behaviour.

---

## 24.2 Recorded swipe corpus

Create a collection of real mouse trajectories.

Each record should contain:

```text
target_word
keyboard_geometry
pointer_samples
timestamp_samples
decoder_candidates
selected_candidate
```

This makes recognition regressions measurable.

---

## 24.3 Metrics

Important metrics:

### Top-1 accuracy

Was the correct word the first prediction?

### Top-3 accuracy

Was the correct word among the first three predictions?

### Top-5 accuracy

Was the correct word among the first five predictions?

### Correction rate

How often does the user manually change the selected word?

### Keystroke/mouse-action cost

How many extra mouse actions are required per corrected word?

### Latency

Time between mouse release and visible prediction.

---

# 26. Performance Targets

Initial targets:

- candidate output should feel effectively immediate after mouse release;
- no noticeable UI lag during the swipe;
- microphone activation should feel responsive;
- idle resource usage should remain low;
- neural models should not block the UI thread.

Inference should run on worker threads.

The UI/event thread must never be blocked by:

- FUTO model execution;
- Whisper transcription;
- SQLite writes;
- large dictionary loading.

---

# 27. Threading Model

Potential structure:

```text
UI thread
├── Qt/QML
├── pointer events
└── rendering

Swipe worker
└── FUTO inference

Speech worker
└── whisper.cpp inference

Storage worker
└── SQLite / personalization
```

Candidate results return asynchronously to the UI.

---

# 28. Development Phases

## Phase 1 — Proof of concept

This phase should deliberately remain small.

Implement:

1. Qt/QML keyboard.
2. C++ SwipeSurface.
3. Mouse glide capture.
4. Pointer grab.
5. Gesture continues outside the keyboard.
6. FUTO Swipe integration.
7. Ranked candidate display.
8. uinput virtual keyboard.
9. Insert selected words into arbitrary Linux applications.
10. One-click replacement with alternative candidate.

### Phase 1 success criteria

The prototype must prove:

```text
hold LMB
    ↓
glide across keyboard
    ↓
leave keyboard if desired
    ↓
release
    ↓
FUTO predicts word
    ↓
alternatives appear
    ↓
word enters Firefox / editor / terminal
```

If these work reliably, the core technical risk of the project is largely resolved.

---

## Phase 2 — Usable daily keyboard

Add:

- personal dictionary;
- SQLite;
- learned word frequency;
- correction history;
- recent-word history bar;
- resize controls;
- floating mode;
- docking;
- punctuation;
- numbers;
- keyboard settings;
- visual polish.

---

## Phase 3 — Speech

Add:

- Qt audio capture;
- whisper.cpp;
- microphone button;
- recording state;
- multilingual models;
- transcription insertion;
- model selection.

---

## Phase 4 — Rich IME integration

Add:

- Fcitx 5 backend;
- direct text commit;
- surrounding-text awareness;
- richer replacement;
- cursor-aware corrections;
- graceful automatic fallback to uinput.

---

## Phase 5 — Languages and personalization

Add:

- Norwegian layout;
- Norwegian swipe dictionary;
- Norwegian spelling;
- language switching;
- contextual language models;
- improved adaptive ranking;
- optional language-specific decoder work.

---

# 29. What We Are Explicitly Not Doing

For the initial architecture:

- no Waydroid dependency;
- no Android runtime;
- no Electron UI;
- no browser-based keyboard;
- no mandatory cloud inference;
- no mandatory network connection;
- no mandatory Fcitx dependency;
- no mandatory IBus dependency;
- no root-running application;
- no KDE-only architecture;
- no GNOME-only architecture;
- no X11-only architecture;
- no Wayland-only architecture.

---

# 30. Licensing Considerations

Licensing must be checked before public distribution.

The project depends on components with their own licenses, including:

- FUTO Swipe library;
- FUTO model files;
- Qt;
- whisper.cpp;
- Hunspell;
- SQLite;
- Fcitx if used.

The repository structure and release process should preserve all required licenses and notices.

This should be resolved early rather than immediately before release.

---

# 31. Main Technical Risks

## Risk 1 — Text injection compatibility

Some applications, compositors, and sandboxing systems may interact differently with synthetic input.

Mitigation:

- uinput universal fallback;
- rich IME backend;
- automated compatibility tests.

---

## Risk 2 — Focus management

The keyboard must receive mouse input without disrupting the destination text field.

Mitigation:

- dedicated Qt window flags and platform-specific handling;
- Wayland and X11 test matrix.

---

## Risk 3 — Mouse trajectories differ from finger trajectories

FUTO is trained primarily for swipe-style input, but mouse movement may have different characteristics.

Mitigation:

- build a mouse swipe corpus early;
- benchmark top-1/top-3 performance;
- adjust preprocessing;
- later use correction history for personalization.

This should be measured during Phase 1 rather than assumed.

---

## Risk 4 — Norwegian recognition quality

English has the strongest ready-made FUTO components.

Mitigation:

- language abstraction from day one;
- Norwegian trie/dictionary;
- ContextLM later;
- collect Norwegian mouse trajectories if needed.

---

## Risk 5 — uinput permissions

Direct access generally requires appropriate system permissions.

Mitigation:

- installer/configuration helper;
- udev/device-access rule;
- never run the entire app as root.

---

# 32. Project Name

**OpenGlide** is the current preferred working name.

The name communicates the project well:

- **Open** signals an open/native Linux-oriented project;
- **Glide** directly describes the primary input method;
- it is broader than a name centered specifically on a keyboard UI, which fits the long-term vision of an input system containing glide, speech, correction, personalization, and multiple Linux input backends.

However, the name is currently **provisional rather than locked**.

There is an existing unrelated software project named **OpenGLide**, historically used for a 3dfx Glide-to-OpenGL compatibility wrapper. Because the spelling is effectively identical in package names, repositories, search engines, and case-insensitive contexts, this creates discoverability and naming-collision risk.

Before the first public release, the project should perform:

- repository-name availability checks;
- package-name checks across major Linux distributions;
- domain availability checks if a website is desired;
- basic trademark/name-conflict checks;
- search-engine discoverability evaluation.

Until that review is complete, documentation and development may use **OpenGlide** as the working name.

---

# 33. Current Locked Decisions

These decisions are currently considered agreed:

1. Build a native Linux application.
2. Use **OpenGlide** as the provisional working name; final public naming remains open because of an existing unrelated OpenGLide project.
3. Optimize primarily for mouse-only use.
4. Use neural glide recognition rather than simple key-sequence matching.
5. Use FUTO Swipe as the initial glide decoder.
6. Use **C++20** as the primary language.
7. Use **Qt 6 Quick/QML** for the UI.
8. Use **CMake + Ninja**.
9. Use a C++ `SwipeSurface` for gesture collection.
10. Preserve an active swipe when the pointer leaves the window.
11. Capture full pointer trajectory with timing.
12. Show ranked alternative candidates.
13. Make one-click correction a core interaction.
14. Maintain a dictionary-constrained glide lexicon.
15. Support a local personal dictionary.
16. Use **Hunspell** for conventional spell checking.
17. Use **SQLite** for local personalization/history/settings.
18. Use **whisper.cpp** for offline speech-to-text.
19. Use **Qt Multimedia/QAudioSource** for microphone capture.
20. Use **Linux uinput** as the universal text-output fallback.
21. Add **Fcitx 5** later as an optional richer backend.
22. Do not make Fcitx mandatory.
23. Support both Wayland and X11.
24. Aim for desktop-environment independence.
25. Start with English QWERTY.
26. Architect Norwegian support from the beginning.
27. Support floating and docked keyboard modes.
28. The keyboard must not take keyboard focus from the target app.
29. Personalization should be local by default.
30. AppImage is the preferred first portable release format.
31. OpenGlide must be easy to show and hide without a physical keyboard.
32. The default proposed mouse-only global toggle is **LMB + RMB held together for ~300 ms**.
33. A configurable keyboard shortcut must be available as a secondary toggle.
34. Mouse4/Mouse5 and other suitable mouse buttons should be configurable as alternative toggles.
35. The application must support being completely hidden while retaining a minimal global activation listener.
36. Build a minimal proof of concept before implementing the full product.

---

# 34. Open Decisions

The following still need investigation or benchmarking:

- final project name;
- exact FUTO model/version to ship;
- exact FUTO licensing/distribution requirements;
- whether FUTO's existing ContextLM should be enabled in V1;
- optimal number of visible candidate words;
- whether the best candidate should always auto-commit;
- precise correction-history ranking algorithm;
- exact personal-dictionary weighting;
- optimal keyboard size for mouse gliding;
- whether the keyboard should show the live swipe path;
- whether pointer coordinates outside the keyboard should be clipped before model inference;
- preferred default Whisper model;
- automatic speech end detection;
- docking implementation across Wayland compositors;
- best Fcitx integration design;
- exact uinput permission setup;
- packaging of model files;
- whether AppImage alone is sufficient for the first public release;
- Norwegian contextual prediction strategy;
- whether user-trained adaptation should use gesture geometry, text history, or both.

---

# 35. Immediate Next Engineering Task

Build the smallest possible experimental prototype that answers one question:

> **How accurately does FUTO Swipe decode real mouse-generated trajectories on a resizable Qt keyboard?**

The prototype only needs:

```text
Qt window
   +
QWERTY keys
   +
SwipeSurface
   +
raw trajectory logging
   +
FUTO Swipe
   +
candidate bar
```

Text injection can be added immediately afterward.

This test should produce actual measurements for:

- Top-1 accuracy;
- Top-3 accuracy;
- latency;
- robustness to leaving the keyboard bounds;
- robustness to overshooting keys;
- performance at different keyboard sizes;
- differences between slow and fast mouse glides.

This measurement should determine the next implementation decisions instead of optimizing blindly.

---

# 36. Product Principle

The project should optimize for:

> **Minimum effort per correctly entered word.**

That means raw top-1 recognition accuracy is not the only important metric.

The overall experience is determined by:

```text
recognition accuracy
        +
candidate quality
        +
correction speed
        +
pointer travel
        +
speech fallback
        +
application compatibility
```

A system that predicts 94% of words correctly and lets the remaining 6% be corrected with one click may be substantially more usable than a nominally more accurate system with poor correction interaction.

OpenGlide should therefore treat **recognition and correction as one unified input workflow**.

---

# 37. Summary

The current design is:

```text
C++20
  │
  ├── Qt 6 / QML
  │     └── mouse-first on-screen keyboard
  │
  ├── FUTO Swipe
  │     └── neural glide recognition
  │
  ├── dictionary layer
  │     ├── FUTO swipe trie
  │     ├── personal dictionary
  │     └── Hunspell
  │
  ├── SQLite
  │     └── personalization and history
  │
  ├── whisper.cpp
  │     └── offline speech-to-text
  │
  └── output system
        ├── Fcitx rich integration
        └── Linux uinput universal fallback
```

The first milestone is not a polished keyboard. It is a measurable prototype proving that neural FUTO glide recognition works accurately and reliably with real mouse trajectories while retaining cross-Linux text entry.

Once that succeeds, the rest of the system can be built incrementally around it.
