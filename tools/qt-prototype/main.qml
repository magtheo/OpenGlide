import QtQuick
import QtQuick.Window
import OpenGlide 1.0

// OpenGlide keyboard surface — geometry per ADR-0005.
//
// ONE unit drives everything: u = contentWidth/10 (a column), rowH = a letter row.
// Every element is a multiple of u horizontally and shares the letter block's
// outer edges, so the action row is part of the same grid rather than a second
// widget floating under it. The letter block spans the full content width — no
// centering, no letterbox — and the 1.5u wings at each end of row 3 hold shift
// and backspace instead of being empty.
//
// The 26 letter CENTRES keep their normalized positions exactly (they come from
// languages/en/layout.json via decoder.keys, the same geometry installed into
// SwipeEngine::set_layout), so this layout is a zero-diff change to the model's
// inputs: the SwipeSurface still normalizes against the letter block alone.
Window {
    id: win
    width: 440; height: 220
    // Shown from main.cpp AFTER the layer-shell surface is configured: the
    // surface type (layer vs xdg-toplevel) is fixed at first commit, and QML
    // visible:true would commit a plain toplevel before WindowCtl::attach ran
    // ("Cannot set shell integration while there's already a shell surface").
    visible: false
    flags: Qt.WindowStaysOnTopHint | Qt.WindowDoesNotAcceptFocus | Qt.FramelessWindowHint
    color: "transparent"
    minimumWidth:  collapsed ? 1 : 320
    minimumHeight: collapsed ? 1 : 170

    // ---- quiet, semantic palette ------------------------------------------------
    // Follow the desktop unless the user explicitly chooses Light or Dark. Every
    // visible colour lives here so both modes stay coherent; components describe
    // their role rather than carrying light-only literals of their own.
    property string themeMode: "system"       // "system" | "light" | "dark"
    readonly property bool darkTheme: themeMode === "dark"
                                           || (themeMode === "system"
                                               && Qt.styleHints.colorScheme === Qt.ColorScheme.Dark)
    property string colorway: "neutral"
    property int customRed: 94
    property int customGreen: 129
    property int customBlue: 172
    property bool appearanceOpen: false
    property bool customColorOpen: false
    readonly property string uiFont: "Inter"
    function colorFromHex(value) {
        const s = String(value);
        if (/^#[0-9a-fA-F]{6}$/.test(s))
            return Qt.rgba(parseInt(s.substring(1, 3), 16) / 255,
                           parseInt(s.substring(3, 5), 16) / 255,
                           parseInt(s.substring(5, 7), 16) / 255, 1);
        return value;
    }
    function mixColor(a, b, amount) {
        const ca = colorFromHex(a), cb = colorFromHex(b);
        return Qt.rgba(ca.r + (cb.r - ca.r) * amount,
                       ca.g + (cb.g - ca.g) * amount,
                       ca.b + (cb.b - ca.b) * amount, 1);
    }
    function colorwaySeed(name) {
        if (name === "ocean")  return colorFromHex("#3f7fc4");
        if (name === "forest") return colorFromHex("#438567");
        if (name === "plum")   return colorFromHex("#815fa8");
        if (name === "rose")   return colorFromHex("#ad5f76");
        if (name === "amber")  return colorFromHex("#a8732f");
        if (name === "custom") return Qt.rgba(customRed / 255, customGreen / 255,
                                                customBlue / 255, 1);
        return colorFromHex(darkTheme ? "#7c8794" : "#68737f");
    }
    readonly property color seedColor: colorwaySeed(colorway)
    function accentTextFor(c) {
        // Relative perceived brightness is sufficient here because accent is
        // already pulled away from the mode's extreme before this is called.
        return c.r * 0.299 + c.g * 0.587 + c.b * 0.114 > 0.58 ? "#15181c" : "#ffffff";
    }
    function twoDigitHex(n) {
        const s = Math.max(0, Math.min(255, Math.round(n))).toString(16).toUpperCase();
        return s.length < 2 ? "0" + s : s;
    }
    readonly property string customHex: "#" + twoDigitHex(customRed)
                                               + twoDigitHex(customGreen)
                                               + twoDigitHex(customBlue)
    readonly property QtObject pal: QtObject {
        readonly property color body: win.mixColor(win.darkTheme ? "#17191d" : "#d9dce1",
                                                    win.seedColor, win.colorway === "neutral" ? 0.02 : 0.10)
        readonly property color bodyOutline:       win.darkTheme ? "#676e78" : "#727983"
        readonly property color keyboardBackground: win.mixColor(win.darkTheme ? "#202329" : "#eceef1",
                                                                  win.seedColor, win.colorway === "neutral" ? 0.015 : 0.08)
        readonly property color keyBackground: win.mixColor(win.darkTheme ? "#292d34" : "#f8f8f7",
                                                             win.seedColor, win.colorway === "neutral" ? 0.01 : 0.055)
        readonly property color keyHover: win.mixColor(keyBackground, accent, win.darkTheme ? 0.28 : 0.18)
        readonly property color keyOutline:        win.darkTheme ? "#555d68" : "#aeb4bc"
        readonly property color keyText:           win.darkTheme ? "#f0f2f5" : "#26292d"
        readonly property color actionBackground: win.mixColor(win.darkTheme ? "#24282e" : "#dde0e4",
                                                                win.seedColor, win.colorway === "neutral" ? 0.015 : 0.07)
        readonly property color actionPressed:     win.darkTheme ? "#3b414a" : "#c3c8cf"
        readonly property color actionText:        win.darkTheme ? "#d9dde3" : "#3d4248"
        readonly property color accent: win.colorway === "neutral"
                                         ? (win.darkTheme ? "#78aef5" : "#2f6fbd")
                                         : win.mixColor(win.seedColor,
                                                        win.darkTheme ? "#ffffff" : "#000000",
                                                        win.darkTheme ? 0.25 : 0.12)
        readonly property color accentText:        win.accentTextFor(accent)
        readonly property color notice:            win.darkTheme ? "#e7a45d" : "#99520f"
        readonly property color chromeBackground: win.mixColor(win.darkTheme ? "#1b1e23" : "#e4e6e9",
                                                                win.seedColor, win.colorway === "neutral" ? 0.02 : 0.11)
        readonly property color mutedText:         win.darkTheme ? "#a8afb9" : "#5d646d"
        readonly property color panelBackground: win.mixColor(win.darkTheme ? "#25292f" : "#ffffff",
                                                               win.seedColor, win.colorway === "neutral" ? 0.01 : 0.045)
        readonly property color panelHover: win.mixColor(panelBackground, accent,
                                                          win.darkTheme ? 0.15 : 0.08)
        readonly property color destructive:       win.darkTheme ? "#ff938a" : "#b3261e"
        readonly property color disabledText:      win.darkTheme ? "#747b85" : "#90969e"
        readonly property color gaugeInactive:     win.darkTheme ? "#69717d" : "#737a83"
        readonly property color diagnosticsBackground: win.darkTheme ? "#111317" : "#202124"
        readonly property color diagnosticsText:   win.darkTheme ? "#e4e7eb" : "#e8eaed"
        readonly property color accentTint: win.mixColor(panelBackground, accent,
                                                          win.darkTheme ? 0.20 : 0.12)
        readonly property color noticeTint:        win.darkTheme ? "#3c2d20" : "#f3e5d8"
    }

    // ---- geometry: two units, everything else derived (ADR-0005 §1) ----
    // The frame is the resize hit band and the inset that exposes the body edge.
    readonly property real frame:    Math.max(4, Math.min(8, width * 0.008))
    readonly property real contentW: width  - 2 * frame
    readonly property real contentH: height - 2 * frame
    readonly property real u:        contentW / 10                                  // column
    readonly property real chromeH:  Math.max(16, Math.min(u * 0.95, contentH * 0.24))
    readonly property real actionH:  chromeH
    readonly property real rowH:     Math.max(10, (contentH - chromeH - actionH) / 3)
    readonly property real kgap:     Math.max(1, u * 0.045)                         // key inset
    readonly property real krad:     Math.max(2, u * 0.16)
    // Letter-block aspect. The free-resize band is a verification gate in ADR-0005
    // (it has to be measured against the corpus, not guessed), so nothing is
    // clamped to it yet — this is surfaced in diagnostics so the measurement has
    // something to read.
    readonly property real letterAspect: rowH > 0 ? contentW / (3 * rowH) : 0

    // ---- manual move/resize (works without a WM role: layer-shell margins or
    // plain x/y on xcb — startSystemMove/Resize needs a WM and is a silent no-op
    // on layer-shell) ----
    function moveTo(x, y) {
        win.x = Math.max(0, x); win.y = Math.max(0, y);
        windowCtl.move(win.x, win.y);
    }
    function sizeTo(w, h) {
        win.width = Math.max(win.minimumWidth, w);
        win.height = Math.max(win.minimumHeight, h);
    }

    // ---- window state ----
    property bool collapsed: false
    property int  expandedW: 440
    property int  expandedH: 220
    readonly property int puckW: Math.max(96, Math.round(expandedW * 0.22))
    readonly property int puckH: Math.max(30, Math.round(expandedW * 0.075))
    property bool menuOpen: false
    property bool showDiagnostics: false
    // "Hidden" keeps the surface MAPPED: GNOME Wayland won't let a client place
    // its own toplevel, so unmapping (visible=false) makes Mutter re-place it on
    // re-show and the keyboard loses its spot. Instead we render the mapped
    // window invisible (opacity 0) and click-through (empty input region); Mutter
    // never re-places it and the keyboard reappears exactly where it was.
    property bool hidden: false
    readonly property int transparentForInput: 0x00080000  // Qt::WindowTransparentForInput

    // ---- input state ----
    property string layer: "abc"        // "abc" | "sym"
    property int    shiftState: 0       // 0 off · 1 once · 2 lock
    property var    candidates: []
    property string greedyText: ""
    property real   decMs: 0.0
    property real   lastGid: 0     // id of the newest glide's corpus record (0 = none)
    property bool   pending: false
    property bool   lastShort: false
    property bool   timedOut: false
    property bool   dropped: false      // glide refused because a decode was already running
    // Recent-word history (spec §9.3). Each entry remembers where the word sits
    // in `injected` and the candidate list its glide produced, so ANY of the last
    // few words can still be corrected in one click — not just the newest one,
    // which was the previous limit and the expensive kind of mistake for a
    // mouse-only user (the alternative is backspacing everything after it).
    property var    history: []      // [{text, cands, start, len}]
    property int    histOpen: -1     // index into history whose popup is open
    property real   histOpenX: 0
    readonly property int histMax: 12
    property string pendingUndo: ""      // word removed by a ⌫ tap, restorable
    property var    pendingUndoEntry: null
    property int    undoGen: -1          // target the staged undo belongs to

    // ---- target identity: never edit a document we no longer own ------------
    // Corrections are "delete N chars, retype" against offsets into `injected`,
    // our mirror of the focused field. If focus moves to a DIFFERENT field the
    // offsets still validate — the mirror didn't change — but they now address
    // the wrong document, so clicking a stale chip would silently edit whatever
    // the user switched to. trimHistory() cannot catch this: it only detects
    // drift WITHIN a still-focused field.
    //
    // Two layers, because the precise signal is not always available:
    //  1. targetGen — IBus focus_in/focus_out/disable. Exact, but only while
    //     OpenGlide is the active IME (i.e. GNOME/IBus).
    //  2. staleMs — an age cap, which is all we have on the uinput fallback
    //     (KDE/Fcitx, no IME focus signal at all). Deliberately much shorter
    //     there: with no way to know the target changed, old is the only proxy.
    property int  targetGen: 0
    // The 5-min cap trusts per-focus generation events; on kwin the bridge
    // enables once and may never re-fire, so the short cap stays unless the
    // bridge is GNOME-grade (text-capable is the proxy for that).
    readonly property int staleMs: (ibusActive && ibusTextCapable) ? 300000 : 20000
    function entryLive(e) {
        return e !== null && e !== undefined
            && e.gen === targetGen
            && (Date.now() - e.t) < staleMs;
    }
    Timer {
        interval: 400; repeat: true
        running: win.visible && !win.collapsed && !win.hidden
        onTriggered: {
            const g = injector.targetGeneration();
            if (g !== win.targetGen) {
                win.targetGen = g;      // focus moved: every prior entry is now stale
                win.candidates = [];    // and so are the candidates for the last glide
                win.histOpen = -1;
                win.clearUndo();
            }
        }
    }

    // What the chrome slots show. Right after a glide those slots ARE the
    // alternatives for the word just committed, so candidates take priority;
    // once the candidates are stale (you typed, spaced, punctuated) the same
    // slots become the recent words, each still carrying its own alternatives.
    // Contextual, so history costs no permanent rows and the glide surface stays
    // at 60% (ADR-0005 §1).
    readonly property bool showingHistory: candidates.length === 0 && chromeSlots.length > 0
    readonly property var chromeSlots: {
        var a = [];
        if (pendingUndo.length)      // an undo offer outranks both
            a.push({text: "↶ undo " + pendingUndo.replace(/\s+$/, ""), hist: false, undo: true, idx: -1});
        // While a choice is pending (E4) only the CONTENDERS show — two equal
        // offers, not four — and the state pill takes the freed right half to
        // ask its question next to them (see statePill x).
        const room = (win.ambiguous ? 2 : 4) - a.length;
        if (candidates.length > 0) {
            for (var i = 0; i < Math.min(room, candidates.length); i++)
                a.push({text: candidates[i].text, hist: false, undo: false, idx: i});
            return a;
        }
        // Only entries we can still prove belong to the current target are
        // offered — a chip you cannot safely act on must not be clickable.
        var live = [];
        for (var j = 0; j < history.length; j++)
            if (entryLive(history[j])) live.push(j);
        for (var k = Math.max(0, live.length - room); k < live.length; k++)
            a.push({text: history[live[k]].text, hist: true, undo: false, idx: live[k]});
        return a;
    }
    property bool   decoderDead: false
    property string injected: ""
    property string activeKey: ""       // key under the cursor DURING a glide
    // Key the cursor is resting on between glides. A finger lifts; a mouse
    // cursor stays wherever the last word left it, so without this the user has
    // no way to see where the next glide would begin — and RESULTS.md measures
    // that as the difference between 81% and 94% dict top-1.
    property string parkedKey: ""
    property int    warmupSecs: 0
    property bool   ibusActive: false
    property bool   ibusTextCapable: false
    readonly property int availWords: injected.length ? injected.replace(/^\s+|\s+$/g, "").split(/\s+/).filter(function (w) { return w.length; }).length : 0
    // Leftward travel per deleted word — one column, so the gesture feels the
    // same at every window size (it used to be a fixed 60 px).
    readonly property real wordSwipePx: u

    // Symbols layer. Reuses the letter block's three-row grid exactly, so the
    // keys land on the same centres — only the glyphs differ. Tap-only: gliding
    // is disabled while this layer is up.
    readonly property var symKeys: [
        {l: "1", c: "1", x: 0.05, y: 0.167}, {l: "2", c: "2", x: 0.15, y: 0.167},
        {l: "3", c: "3", x: 0.25, y: 0.167}, {l: "4", c: "4", x: 0.35, y: 0.167},
        {l: "5", c: "5", x: 0.45, y: 0.167}, {l: "6", c: "6", x: 0.55, y: 0.167},
        {l: "7", c: "7", x: 0.65, y: 0.167}, {l: "8", c: "8", x: 0.75, y: 0.167},
        {l: "9", c: "9", x: 0.85, y: 0.167}, {l: "0", c: "0", x: 0.95, y: 0.167},
        {l: "@", c: "@", x: 0.10, y: 0.500}, {l: "#", c: "#", x: 0.20, y: 0.500},
        {l: "$", c: "$", x: 0.30, y: 0.500}, {l: "%", c: "%", x: 0.40, y: 0.500},
        {l: "&", c: "&", x: 0.50, y: 0.500}, {l: "-", c: "-", x: 0.60, y: 0.500},
        {l: "+", c: "+", x: 0.70, y: 0.500}, {l: "(", c: "(", x: 0.80, y: 0.500},
        {l: ")", c: ")", x: 0.90, y: 0.500},
        {l: "*", c: "*", x: 0.20, y: 0.833}, {l: "\"", c: "\"", x: 0.30, y: 0.833},
        {l: "'", c: "'", x: 0.40, y: 0.833}, {l: ":", c: ":", x: 0.50, y: 0.833},
        {l: ";", c: ";", x: 0.60, y: 0.833}, {l: "!", c: "!", x: 0.70, y: 0.833},
        {l: "?", c: "?", x: 0.80, y: 0.833}
    ]

    // ================= what the machine is doing =================
    // Until now every one of these states rendered in exactly ONE place: the
    // opt-in diagnostics line. So by default a 480 ms decode, a glide dropped by
    // the stale-discard guard, and a decoder that never loaded all looked
    // identical — nothing happening — and the only state that ever reached the
    // user was the word itself, or its absence.
    //
    // These strings are STRUCTURE, not content: no typed text, no candidate, no
    // geometry. That is the line ADR-0004 §1 draws, and it explicitly permits
    // this side of it ("stage timings, backend selection, errors, focus/
    // visibility transitions" — never payloads). Keep it that way: never put a
    // decoded word in here.
    //
    // Pure function of the state, with the state passed IN: the precedence order
    // is the whole content of this decision (a dead decoder outranks a stalled
    // decode outranks a short stroke), so it is worth testing on its own, and a
    // pure function is testable on a box with no Qt (tools/qml-logic-test).
    // Explicit arguments also make the binding's dependencies visible.
    // `ambig` (E4, ADR-0006): top-1 and top-2 finished within ambigMargin nats —
    // the ranking is a coin flip, so the user is being asked to pick. Outranks
    // nothing urgent (it expires the moment they act); outranks `stall`, which
    // is idle noise by comparison.
    function noteState(dead, ready, drop, pend, short, ambig, stall, secs) {
        if (dead)   return {text: "decoder stopped — restart",   problem: true};
        if (!ready) return {text: "loading decoder… " + secs + " s", problem: false};
        if (drop)   return {text: "busy — glide again",           problem: true};
        if (pend)   return {text: "decoding…",                    problem: false};
        if (short)  return {text: "too short — glide further",    problem: true};
        if (ambig)  return {text: "which one?",                   problem: true};
        if (stall)  return {text: "decode stalled — glide again", problem: true};
        return {text: "", problem: false};
    }
    readonly property var note: noteState(decoderDead, decoder.ready, dropped,
                                          pending, lastShort, ambiguous, timedOut, warmupSecs)
    readonly property string stateNote: note.text
    // Waiting vs. something-went-wrong. Only the colour and the icon differ, but
    // the distinction is the whole point: "decoding…" asks for patience, the
    // rest ask for another glide.
    readonly property bool noteIsProblem: note.problem

    // Transient notes must expire or they become furniture. `lastShort` in
    // particular used to be cleared only by the next full-length glide — which
    // was harmless while it was invisible and would have made it permanent now.
    Timer {
        id: noteTimer
        interval: 2600
        onTriggered: { win.lastShort = false; win.timedOut = false; win.dropped = false }
    }
    function noteShort() {              // a stroke too short to decode
        lastShort = true; pending = false; candidates = [];
        noteTimer.restart();
    }

    // What to do with a finished glide. Every exit must leave the UI in a state
    // that RESOLVES: this used to set `pending` first and only then check whether
    // the decoder would take the glide, so both refusal paths — engine not ready,
    // and a decode already running (stale-discard, ADR-0003) — left `pending`
    // true with nothing to clear it. That is 20 s of dead keyboard ending in a
    // message that rendered only in diagnostics. Returns the outcome so the rule
    // can be asserted without Qt.
    function glideFinished(points) {
        flushAmbiguous();                  // an unanswered choice defaults to top-1
        if (points.length < 4) { noteShort(); return "short"; }
        lastShort = false;
        candidates = [];
        // The pill already says "loading decoder…" — don't also claim to be
        // decoding something.
        if (!decoder.ready) return "notready";
        timedOut = false; dropped = false;
        // Last committed word(s), newest last — flushAmbiguous() above already
        // settled any pending choice, so `history` reflects true prior context
        // at glide start. Logging-only (ADR-0006 layer 2 needs real sequence
        // data; today's corpus has none); does not affect this decode.
        var ctx = [];
        for (var ci = Math.max(0, history.length - 2); ci < history.length; ci++)
            ctx.push(history[ci].text);
        if (!decoder.decode(points, ctx)) {
            pending = false; dropped = true;
            noteTimer.restart();
            return "dropped";
        }
        pending = true;
        watchdog.restart();
        return "decoding";
    }

    // ======================= E4: score margin → behaviour =======================
    // The diagnose tally left exactly one miss at bonus-off (river/riber losing
    // to rober by 0.09 nats) and the review's E4 said what to do with it: a
    // margin that small should change BEHAVIOUR, not just ranking. When top-1
    // and top-2 finish within ambigMargin nats the decoder is saying "coin
    // flip" — auto-committing the winner bets the user's text on that coin.
    // Instead: commit nothing, show the chips, let one click pick. The next
    // input event (glide, tap, space, backspace…) flushes top-1 first, so
    // ignoring the question costs nothing vs today — but answering it turns a
    // wrong commit into a right one. Default 1.0 nats, tuned on the live corpus
    // (RESULTS.md "margin sweep"): 0.6 missed the tink->think class (0.89), and
    // above 1.0 no additional save was chip-reachable — the next corrected glide
    // (wl->well, gap 1.70) had its truth ranked 3rd, so no pair of chips could
    // offer it. At 1.0: 3 saves / 11 holds on 41 recorded glides.
    readonly property real ambigMargin: 1.0
    property bool ambiguous: false       // a choice is pending (see candsAmbiguous)
    // Pure, so the threshold rule is testable without Qt. Scores come from the
    // bridge (candidates[i].score, nats); missing scores (hand-built tests) =
    // not ambiguous.
    function candsAmbiguous(cands, margin) {
        if (cands.length < 2) return false;
        if (typeof cands[0].score !== "number" || typeof cands[1].score !== "number") return false;
        return (cands[0].score - cands[1].score) < margin;
    }
    // A chip was clicked while a choice was pending: commit THAT word. Picking
    // a non-top-1 word is also the corpus truth for the glide — amend the
    // record so the label is what the user meant, not the coin flip.
    function commitChoice(i) {
        if (!ambiguous || i < 0 || i >= candidates.length) return;
        const word = candidates[i].text, top1 = candidates[0].text;
        ambiguous = false;
        commitDecoded(word);
        if (word !== top1 && lastGid) decoder.amendRecord(lastGid, word.toLowerCase());
    }
    // The user moved on without picking: behave exactly as before E4 — commit
    // top-1 — then whatever they did proceeds. Every entry point that would
    // overwrite the pending question calls this first, so no word is ever lost
    // and no question outlives its moment.
    function flushAmbiguous() {
        if (!ambiguous) return;
        ambiguous = false;
        if (candidates.length > 0) commitDecoded(candidates[0].text);
    }
    // Reaching for backspace while a choice is pending means neither offered
    // word was right (user report, 2026-08-19: "still" offered as
    // stool/sol, neither correct) — flushAmbiguous()'s default-to-top-1
    // would commit the wrong word just so the very next action could delete
    // it again. Backspace is an "undo/reject" gesture everywhere else in
    // this UI (E4's whole premise is asking rather than guessing), so it
    // drops the question with NOTHING committed instead of passing through:
    // no injected text to remove, no corpus label for a glide whose true
    // word is unknown (dropRecord, same as a chip-deleted word). Returns
    // true when it consumed the backspace this way, so callers stop there
    // rather than also deleting whatever came before the pending glide.
    function rejectAmbiguous() {
        if (!ambiguous) return false;
        ambiguous = false;
        if (lastGid) decoder.dropRecord(lastGid, true);
        candidates = [];
        return true;
    }

    function stateText() {
        if (stateNote.length) return stateNote;
        if (candidates.length === 0) return "focus a text editor, then glide a word here";
        return `top-1 “${candidates[0].text}” (greedy “${greedyText}”, ${decMs.toFixed(0)} ms)`;
    }
    Timer { interval: 1000; repeat: true; running: !decoder.ready && win.visible; onTriggered: win.warmupSecs += 1 }
    Timer { interval: 800; repeat: true; running: win.visible; onTriggered: { win.ibusActive = injector.ibusActive(); win.ibusTextCapable = injector.ibusTextCapable() } }

    // ================= caret avoidance =================
    // The focused app reports where its text cursor is (IBus set_cursor_location).
    // An IME normally uses that to put a candidate popup NEXT to the caret; we
    // want the opposite — to get out from on top of the text being typed. Spec §17
    // covers focus; this covers occlusion, which is the other half of "the
    // keyboard must not be obstructive".
    //
    // Deliberately timid: it only ever moves the window when it is genuinely
    // covering the caret, it moves the minimum distance, it never moves during a
    // glide or a drag, and it stays inside the screen. Many toolkits never report
    // at all — then caretReports() stays 0 and this does nothing, which is why it
    // is safe to leave on.
    // Preedit probe (no behaviour change yet). Deletion is broken on GNOME — the
    // IBus delete API aborts the shell, forwarded BackSpace is ignored, and uinput
    // targets the focused surface rather than the commit context — so every
    // correction path rests on a shaky primitive. Preedit is the way out: leave the
    // current word UNCOMMITTED and editing it needs no deletion at all. But that
    // only works in clients that can display preedit, and terminals/Electron often
    // cannot. So: report what the focused app declares, and decide from data.
    property bool preeditSupported: false
    property int  clientCaps: -1
    Timer {
        interval: 700; repeat: true
        running: win.visible && !win.collapsed
        onTriggered: {
            win.preeditSupported = injector.preeditSupported();
            win.clientCaps = injector.capabilities();
        }
    }

    property bool avoidCaret: true
    property rect caretRect: Qt.rect(0, 0, 0, 0)
    property int  caretReports: 0

    function checkCaret() {
        if (hidden || collapsed) return;
        caretReports = injector.caretReports();
        if (caretReports <= 0) return;
        const c = injector.caretRect();
        caretRect = c;
        if (!avoidCaret || surface.swiping || menuOpen) return;
        if (c.width <= 0 && c.height <= 0 && c.x === 0 && c.y === 0) return;

        // Treat a zero-size report as a thin caret so the margin still applies.
        const ch = c.height > 0 ? c.height : Math.round(u * 0.4);
        const pad = Math.round(u * 0.35);
        const top = y, bottom = y + height;
        const cTop = c.y - pad, cBottom = c.y + ch + pad;
        if (cBottom <= top || cTop >= bottom) return;            // already clear of it

        const scr = Screen.desktopAvailableHeight;
        const below = cBottom;                    // window top if we sit under it
        const above = cTop - height;              // window top if we sit over it
        const canBelow = below + height <= scr;
        const canAbove = above >= 0;
        var ny = y;
        if (canBelow && canAbove)      ny = (Math.abs(below - y) <= Math.abs(above - y)) ? below : above;
        else if (canBelow)             ny = below;
        else if (canAbove)             ny = above;
        else                           return;    // nowhere to go; leave it alone
        if (ny !== y) y = ny;
    }
    Timer {
        interval: 500; repeat: true
        running: win.visible && !win.collapsed
        onTriggered: win.checkCaret()
    }

    // Human-readable name for the configured global gesture (spec §13.6).
    function toggleGesture() {
        const m = toggleListener.mode;
        if (m === "middle") return "middle click";
        if (m === "mouse4") return "Mouse4";
        if (m === "mouse5") return "Mouse5";
        return "hold L+R";
    }

    // ================= shift =================
    function shifted(c) { return shiftState > 0 ? c.toUpperCase() : c; }
    function consumeShift() { if (shiftState === 1) shiftState = 0; }
    function cycleShift() { shiftState = (shiftState + 1) % 3; }

    // ================= injection ops (keep `injected` in sync with the target) ==
    function commitDecoded(word) {
        clearUndo();
        const w = shiftState > 0 ? word.charAt(0).toUpperCase() + word.substring(1) : word;
        const start = injected.length;
        injector.commit(w);
        injected += w + " ";
        decoder.bumpWord(word);     // personalization keys on the dictionary form
        consumeShift();
        pushHistory(w, candidates, start, lastGid);
    }

    // ================= recent-word history (spec §9.3) =================
    function pushHistory(text, cands, start, gid) {
        var h = history.slice();
        h.push({text: text, cands: cands, start: start, len: text.length,
                gen: targetGen, t: Date.now(), gid: gid || 0});   // which target this belongs to
        while (h.length > histMax) h.shift();
        history = h;
    }
    // Entries are only trustworthy while they still match the mirror. Manual
    // editing (backspace, word-delete, punctuation collapsing a space) moves the
    // text under them, so re-check and drop whatever no longer lines up rather
    // than correcting the wrong span later.
    function trimHistory() {
        var h = [];
        for (var i = 0; i < history.length; i++) {
            const e = history[i];
            if (injected.substr(e.start, e.len) === e.text) h.push(e);
        }
        if (h.length !== history.length) history = h;
        if (histOpen >= history.length) histOpen = -1;
    }
    // Replace history entry `hi`, retyping whatever follows it (spec §9.2 — with
    // a rich IME this would be a direct replacement; here it is delete + recommit).
    function replaceHistory(hi, newText) {
        if (hi < 0 || hi >= history.length) return;
        const e = history[hi];
        // Refuse rather than edit blind: this entry's offsets were computed against
        // a target we may no longer be typing into.
        if (!entryLive(e)) { histOpen = -1; return; }
        if (injected.substr(e.start, e.len) !== e.text) { trimHistory(); return; }
        if (newText === e.text) return;
        const suffix = injected.substring(e.start + e.len);
        injector.backspace(e.len + suffix.length);
        injector.commitExact(newText + suffix);
        injected = injected.substring(0, e.start) + newText + suffix;
        const delta = newText.length - e.len;
        var h = history.slice();
        h[hi] = {text: newText, cands: e.cands, start: e.start, len: newText.length,
                 gen: e.gen, t: e.t, gid: e.gid};
        for (var j = hi + 1; j < h.length; j++)
            h[j] = {text: h[j].text, cands: h[j].cands, start: h[j].start + delta,
                    len: h[j].len, gen: h[j].gen, t: h[j].t, gid: h[j].gid};
        history = h;
        histOpen = -1;
        decoder.bumpWord(newText.toLowerCase());   // the user chose this — boost it
        // ...and the corpus gets the truth: this glide's top-1 was wrong and the
        // user said so. This is the label that makes recorded data trustworthy.
        if (e.gid) decoder.amendRecord(e.gid, newText.toLowerCase());
    }
    function deleteHistory(hi) {
        if (hi < 0 || hi >= history.length) return;
        const e = history[hi];
        if (!entryLive(e)) { histOpen = -1; return; }
        if (injected.substr(e.start, e.len) !== e.text) { trimHistory(); return; }
        const extra = injected[e.start + e.len] === " " ? 1 : 0;   // swallow one space
        const suffix = injected.substring(e.start + e.len + extra);
        injector.backspace(e.len + extra + suffix.length);
        if (suffix.length) injector.commitExact(suffix);
        injected = injected.substring(0, e.start) + suffix;
        const delta = -(e.len + extra);
        var h = history.slice();
        h.splice(hi, 1);
        for (var j = hi; j < h.length; j++)
            h[j] = {text: h[j].text, cands: h[j].cands, start: h[j].start + delta,
                    len: h[j].len, gen: h[j].gen, t: h[j].t, gid: h[j].gid};
        history = h;
        histOpen = -1;
        candidates = [];
        // The corpus must not keep a top-1 label for a word the user threw away.
        if (e.gid) decoder.dropRecord(e.gid);
    }
    function typeKey(c) {
        flushAmbiguous();               // typing = moving on: default the choice
        clearUndo();
        const ch = shifted(c);
        injector.typeChar(ch);
        injected += ch;
        consumeShift();
    }
    function tapSpace() { flushAmbiguous(); clearUndo(); injector.typeChar(" "); injected += " "; }
    function tapEnter() { injector.typeChar("\n"); injected += "\n"; }
    function tapPunct(p) {                       // collapse a preceding space, then "p "
        flushAmbiguous();
        clearUndo();
        if (injected.length && injected[injected.length - 1] === " ") {
            injector.backspace(1);
            injected = injected.substring(0, injected.length - 1);
        }
        injector.typeChar(p); injector.typeChar(" ");
        injected += p + " ";
        trimHistory();          // the collapsed space moved everything after it
    }
    function deleteChar() {
        if (rejectAmbiguous()) return;  // neither offered word was right
        flushAmbiguous();               // backspace = moving on too
        if (!injected.length) return;
        clearUndo();
        candidates = [];                  // editing invalidates the last glide's suggestions
        injector.backspace(1);
        injected = injected.substring(0, injected.length - 1);
        trimHistory();
    }
    function deleteWord() {                      // backspace-swipe: trailing spaces + one word
        if (rejectAmbiguous()) return "";         // neither offered word was right
        if (!injected.length) return "";
        flushAmbiguous();
        clearUndo();          // tapDeleteWord re-stages after this returns
        candidates = [];
        var i = injected.length, n = 0;
        while (i > 0 && injected[i - 1] === " ") { i--; n++; }
        while (i > 0 && injected[i - 1] !== " ") { i--; n++; }
        const deleted = injected.substring(i);
        injector.backspace(n);
        injected = injected.substring(0, i);
        trimHistory();
        return deleted;
    }
    function undoWord(s) {
        candidates = [];
        injector.commitExact(s);
        injected += s;
    }
    // Tapping ⌫ removes a whole word, so a mis-tap costs a word rather than a
    // character — and the swipe-right undo only covers the swipe gesture. Stage
    // the deletion (with its history entry, so the alternatives come back too)
    // and offer it as a chip until it times out.
    function tapDeleteWord() {
        // deleteWord() below handles the ambiguous case itself (reject, not
        // flush) — flushing here first would commit top-1 before it gets
        // the chance, reintroducing the exact commit-then-delete friction
        // this is meant to remove.
        const hBefore = history;
        const s = deleteWord();
        if (!s.length) return;
        pendingUndoEntry = hBefore.length > history.length ? hBefore[hBefore.length - 1] : null;
        undoGen = targetGen;
        pendingUndo = s;
        undoTimer.restart();
    }
    function undoDelete() {
        if (!pendingUndo.length) return;
        if (undoGen !== targetGen) { clearUndo(); return; }   // different target now
        const start = injected.length;          // deleteWord() returns "word" + spaces
        injector.commitExact(pendingUndo);
        injected += pendingUndo;
        if (pendingUndoEntry)
            pushHistory(pendingUndoEntry.text, pendingUndoEntry.cands, start, pendingUndoEntry.gid || 0);
        clearUndo();
    }
    function clearUndo() {
        pendingUndo = ""; pendingUndoEntry = null; undoGen = -1;
        undoTimer.stop();
    }
    Timer { id: undoTimer; interval: 8000; onTriggered: win.clearUndo() }
    // Candidate click. The newest word is just the last history entry, so this is
    // the same operation the history popup performs — one code path, not two.
    function correct(i) {
        if (i < 0 || i >= candidates.length || !history.length) return;
        const hi = history.length - 1;
        const old = history[hi].text;
        var nw = candidates[i].text;
        if (old.length && old[0] !== old[0].toLowerCase())   // keep the capitalization
            nw = nw.charAt(0).toUpperCase() + nw.substring(1);
        replaceHistory(hi, nw);
    }
    // Closest key to the cursor, in PIXELS (the normalized frame is anisotropic).
    function nearestKey(nx, ny) {
        var best = null, bestD = 1e9;
        const W = kb.width, H = kb.height, ks = win.layer === "sym" ? win.symKeys : decoder.keys;
        for (var i = 0; i < ks.length; i++) {
            const k = ks[i], dx = (k.x - nx) * W, dy = (k.y - ny) * H, d = dx * dx + dy * dy;
            if (d < bestD) { bestD = d; best = k; }
        }
        return best;
    }

    // ================= window ops =================
    function collapse() {
        if (collapsed) return;
        expandedW = width; expandedH = height;
        collapsed = true;
        menuOpen = false;
        width = puckW; height = puckH;
    }
    function expand() {
        if (!collapsed) return;
        collapsed = false;
        width = expandedW; height = expandedH;
    }
    function rescale(f) {
        if (collapsed) return;
        width  = Math.max(320, Math.round(width  * f));
        height = Math.max(170, Math.round(height * f));
    }
    function preset(w, h) {
        if (collapsed) return;
        width = w; height = h; menuOpen = false;
    }
    function setTheme(mode) {
        themeMode = mode;
        settings.setValue("appearance/theme", themeMode);
        settings.sync();
    }
    function setColorway(name) {
        colorway = name;
        settings.setValue("appearance/colorway", colorway);
        settings.sync();
    }
    function setCustomChannel(channel, value) {
        const v = Math.max(0, Math.min(255, Math.round(value)));
        if (channel === "r") customRed = v;
        else if (channel === "g") customGreen = v;
        else customBlue = v;
        colorway = "custom";
    }
    function saveCustomColor() {
        settings.setValue("appearance/colorway", "custom");
        settings.setValue("appearance/customColor", customHex);
        settings.sync();
    }
    function toggleMenu() {
        menuOpen = !menuOpen;
        if (menuOpen) {
            appearanceOpen = false;
            customColorOpen = false;
        }
    }
    function themeLabel() {
        if (themeMode === "system") return "System (" + (darkTheme ? "dark" : "light") + ")";
        return themeMode.charAt(0).toUpperCase() + themeMode.substring(1);
    }
    function colorwayLabel() {
        return colorway.charAt(0).toUpperCase() + colorway.substring(1);
    }
    // Hidden (spec §13.5): the keyboard is gone from view but the Wayland surface
    // stays MAPPED — invisible (opacity 0) and click-through (empty input region).
    // Unmapping (visible=false) would make Mutter re-place it on re-show (see the
    // `hidden` property above), losing the remembered spot. Only offered when the
    // global evdev listener runs: hiding with no way back would be the same trap
    // as a one-click quit, just slower to discover.
    function hideCompletely() {
        if (!toggleListener.available) return;
        menuOpen = false;
        persistGeometry();
        hidden = true;
        flags = flags | transparentForInput;   // empty input region → click-through
        contentItem.visible = false;          // no content painted → transparent buffer (QWindow::opacity is a no-op on Qt Wayland)
        pointerSpeed.leave();                   // lift slowdown over the ghost window
    }
    function unhide() {
        if (hidden) {
            hidden = false;
            contentItem.visible = true;
            flags = flags & ~transparentForInput;   // input region restored
        }
        raise();
    }
    function toggleVisibility() {
        if (hidden) unhide();
        else if (toggleListener.available) hideCompletely();
        else collapse();     // no listener: collapse is the reversible option
    }

    Connections {
        target: toggleListener
        function onToggleRequested() { win.toggleVisibility() }
    }

    function persistGeometry() {
        if (!collapsed) {
            settings.setValue("window/width", width);
            settings.setValue("window/height", height);
        }
        settings.setValue("window/x", windowCtl.posX());
        settings.setValue("window/y", windowCtl.posY());
        settings.sync();
    }
    Timer { id: saveGeom; interval: 600; onTriggered: win.persistGeometry() }
    onWidthChanged:  if (!collapsed) saveGeom.restart()
    onHeightChanged: if (!collapsed) saveGeom.restart()
    onXChanged: saveGeom.restart()
    onYChanged: saveGeom.restart()

    Component.onCompleted: {
        const w = parseInt(settings.value("window/width", 440));
        const h = parseInt(settings.value("window/height", 220));
        if (w > 0 && h > 0) { win.width = Math.max(320, w); win.height = Math.max(170, h); }
        const sx = parseInt(settings.value("window/x", -1));
        const sy = parseInt(settings.value("window/y", -1));
        if (sx >= 0 && sy >= 0) { win.x = sx; win.y = sy; windowCtl.move(sx, sy); }
        win.expandedW = win.width; win.expandedH = win.height;
        win.avoidCaret = settings.value("window/avoidCaret", true) !== false
                      && String(settings.value("window/avoidCaret", true)) !== "false";
        const savedTheme = String(settings.value("appearance/theme", "system"));
        win.themeMode = savedTheme === "light" || savedTheme === "dark" ? savedTheme : "system";
        const savedColorway = String(settings.value("appearance/colorway", "neutral"));
        const validColorways = ["neutral", "ocean", "forest", "plum", "rose", "amber", "custom"];
        win.colorway = validColorways.indexOf(savedColorway) >= 0 ? savedColorway : "neutral";
        const savedCustom = String(settings.value("appearance/customColor", "#5E81AC"));
        if (/^#[0-9a-fA-F]{6}$/.test(savedCustom)) {
            win.customRed = parseInt(savedCustom.substring(1, 3), 16);
            win.customGreen = parseInt(savedCustom.substring(3, 5), 16);
            win.customBlue = parseInt(savedCustom.substring(5, 7), 16);
        }
    }

    Timer {
        id: watchdog
        interval: 20000; repeat: false
        onTriggered: if (win.pending) {
            win.pending = false; win.timedOut = true; win.candidates = [];
            noteTimer.restart();
        }
    }

    // The visible edge is a quiet outline; `frame` remains the larger invisible
    // resize target. Keeping those separate removes the old flat gray resize ring
    // without making the window harder to grab.
    Rectangle {
        visible: !win.hidden
        anchors.fill: parent
        radius: Math.max(5, win.u * 0.22)
        color: pal.body
        border.color: pal.bodyOutline
        border.width: 1
        antialiasing: true
    }

    // ======================= COLLAPSED: the puck =======================
    // `×` collapses to this instead of quitting. It is draggable, always on top,
    // needs no permissions, and one click brings the keyboard back — the
    // guaranteed mouse-only way home (ADR-0005 §5). Quit lives in the ⋯ menu.
    Rectangle {
        visible: win.collapsed
        anchors.fill: parent
        color: pal.diagnosticsBackground
        radius: Math.max(3, height * 0.18)
        Text {
            anchors.centerIn: parent
            text: "⌨  OpenGlide"
            color: pal.diagnosticsText
            font.family: win.uiFont
            font.pixelSize: Math.max(9, parent.height * 0.36)
        }
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            cursorShape: Qt.PointingHandCursor
            property real px: 0
            property real py: 0
            property bool moved: false
            property real bx: 0
            property real by: 0
            property real s0x: 0
            property real s0y: 0
            onPressed: function (mouse) {
                px = mouse.x; py = mouse.y; moved = false;
                bx = windowCtl.posX(); by = windowCtl.posY();
                s0x = windowCtl.posX() + mouse.x; s0y = windowCtl.posY() + mouse.y;
            }
            onPositionChanged: function (mouse) {
                if (!moved && Math.abs(mouse.x - px) + Math.abs(mouse.y - py) > 6) {
                    moved = true;
                }
                if (moved && pressed) {
                    const mp = mapToItem(win.contentItem, mouse.x, mouse.y);
                    const gx = windowCtl.posX() + mp.x, gy = windowCtl.posY() + mp.y;
                    win.moveTo(bx + gx - s0x, by + gy - s0y);
                }
            }
            onClicked: function (mouse) { if (!moved && mouse.button === Qt.LeftButton) win.expand() }
        }
    }

    // ======================= EXPANDED: the keyboard =======================
    Item {
        id: content
        visible: !win.collapsed
        x: win.frame; y: win.frame
        width: win.contentW; height: win.contentH

        Rectangle { anchors.fill: parent; color: pal.keyboardBackground }

        // ---------------- chrome bar: drag + candidates + controls ----------------
        Rectangle {
            id: topbar
            x: 0; y: 0; width: parent.width; height: win.chromeH
            color: pal.chromeBackground

            // Any chrome that is not a control is a drag handle. Deltas come
            // from the seat-global cursor (windowCtl), not item-relative mouse
            // coords — the window moves under the cursor while dragging, which
            // makes item-relative deltas self-referential (drag cancels itself).
            MouseArea {
                anchors.fill: parent
                // Screen-space cursor = our logical window position + the
                // event's surface-relative offset. QCursor::pos() is NOT usable
                // here: on Qt Wayland it returns surface-relative coordinates,
                // and mixing spaces clamps every drag to (0,0).
                property real bx: 0
                property real by: 0
                property real s0x: 0
                property real s0y: 0
                onPressed: function (mouse) {
                    const mp = mapToItem(win.contentItem, mouse.x, mouse.y);
                    bx = windowCtl.posX(); by = windowCtl.posY();
                    s0x = windowCtl.posX() + mp.x; s0y = windowCtl.posY() + mp.y;
                }
                onPositionChanged: function (mouse) {
                    const mp = mapToItem(win.contentItem, mouse.x, mouse.y);
                    const gx = windowCtl.posX() + mp.x, gy = windowCtl.posY() + mp.y;
                    win.moveTo(bx + gx - s0x, by + gy - s0y);
                }
                onReleased: win.persistGeometry()
            }

            // ---- state pill: what the machine is doing, in the user's line of sight ----
            // It takes the two LEFTMOST chip slots, which is the cheapest pair to
            // borrow: candidates are always empty while a note is showing, and the
            // slots fill oldest-first, so the two it covers are the two least
            // useful recent words. The slots do not shift — a chip that moves is
            // a chip you have to re-find (ADR-0005 §1) — they just yield.
            Rectangle {
                id: statePill
                visible: win.stateNote.length > 0
                // Normally the pill borrows the two LEFTmost slots (candidates
                // are always empty while a note shows). While a choice is
                // pending the chips ARE the question, so only the contenders
                // show (chromeSlots room=2) and the pill takes the freed right
                // half — "which one?" sits beside its answers.
                //
                // A 1px border on transparent, sitting on the light candBar
                // fill, read as "barely visible" (user report, 2026-08-19,
                // screenshot of the wl/well ambiguous state) — a plain
                // outline at this size loses to the two solid-white chips
                // next to it. Filled with a light tint of the same colour so
                // it holds its own without going as loud as a solid fill.
                x: win.ambiguous ? win.u * 3.62 : win.u * 0.36
                y: parent.height * 0.14
                width: win.u * 3.20; height: parent.height * 0.72
                radius: height / 2
                color: win.noteIsProblem ? pal.noticeTint : pal.accentTint
                border.color: win.noteIsProblem ? pal.notice : pal.accent
                border.width: 1
                // While a choice is pending the two chips ARE the icon — a
                // third bordered shape (dot or "!") next to two answer chips
                // was exactly the clutter being reported, so it drops out
                // here and the text gets the room instead.
                Item {
                    id: noteIcon
                    visible: !win.ambiguous
                    x: win.u * 0.22; width: win.u * 0.20; height: width
                    anchors.verticalCenter: parent.verticalCenter
                    Rectangle {          // waiting — pulses
                        anchors.centerIn: parent
                        width: win.u * 0.16; height: width; radius: width / 2
                        visible: !win.noteIsProblem
                        color: pal.accent
                        // `from` is set on both legs so a restart always begins
                        // at full opacity — an "animation on property" keeps its
                        // last value when it stops, and a dot stuck at 0.25
                        // would read as a third, meaningless state.
                        SequentialAnimation on opacity {
                            running: statePill.visible && !win.noteIsProblem
                            loops: Animation.Infinite
                            NumberAnimation { from: 1.0; to: 0.25; duration: 520; easing.type: Easing.InOutSine }
                            NumberAnimation { from: 0.25; to: 1.0; duration: 520; easing.type: Easing.InOutSine }
                        }
                    }
                    Text {               // problem — steady
                        anchors.centerIn: parent
                        visible: win.noteIsProblem
                        text: "!"; color: pal.notice; font.bold: true
                        font.pixelSize: Math.max(8, win.u * 0.26)
                    }
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    x: win.ambiguous ? win.u * 0.26 : win.u * 0.54
                    width: parent.width - x - win.u * 0.18
                    horizontalAlignment: win.ambiguous ? Text.AlignHCenter : Text.AlignLeft
                    elide: Text.ElideRight
                    text: win.stateNote
                    font.bold: win.ambiguous
                    font.family: win.uiFont
                    color: win.noteIsProblem ? pal.notice : pal.accent
                    font.pixelSize: Math.max(8, win.u * 0.23)
                }
            }

            // FIXED slots — same word, same place, every glide. Candidates when
            // they are fresh, recent words otherwise (see chromeSlots).
            Repeater {
                model: 4
                Rectangle {
                    readonly property var slot: index < win.chromeSlots.length ? win.chromeSlots[index] : null
                    readonly property bool isHist: slot ? slot.hist : false
                    readonly property bool isUndo: slot ? slot.undo === true : false
                    // While a choice is pending (E4) top-1 is NOT the answer — it
                    // is one of four equal offers, so it loses the filled style.
                    readonly property bool isTop: slot && !slot.hist && !slot.undo && slot.idx === 0
                                                  && !win.ambiguous
                    x: win.u * (0.36 + index * 1.63); y: parent.height * 0.14
                    width: win.u * 1.57; height: parent.height * 0.72
                    radius: height / 2
                    // The two left slots yield to the state pill while it shows —
                    // EXCEPT while a choice is pending, when they hold the answers
                    // and the pill has moved right instead (see statePill x).
                    // An undo offer lands in slot 0, so a note briefly hides it —
                    // it comes back on its own when the note expires, since both
                    // are bindings and the 8 s undo window outlives the 2.6 s note.
                    visible: slot !== null && !(win.stateNote.length > 0 && index < 2 && !win.ambiguous)
                    color: isTop ? pal.accent : (isHist || isUndo ? "transparent" : pal.panelBackground)
                    border.color: isUndo ? pal.accent : (isHist ? pal.mutedText : pal.keyOutline)
                    border.width: isTop ? 0 : (isUndo ? 2 : 1)
                    Text {
                        anchors.fill: parent; anchors.margins: parent.height * 0.18
                        text: parent.slot ? parent.slot.text : ""
                        horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                        font.bold: parent.isTop || parent.isUndo
                        font.family: win.uiFont
                        font.pixelSize: Math.max(8, win.u * 0.26)
                        color: parent.isTop ? pal.accentText
                               : parent.isUndo ? pal.accent
                               : (parent.isHist ? pal.mutedText : pal.keyText)
                    }
                    MouseArea {
                        anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            if (!parent.slot) return;
                            if (parent.slot.undo) {
                                win.undoDelete();
                            } else if (parent.slot.hist) {
                                win.histOpenX = parent.x;
                                win.histOpen = win.histOpen === parent.slot.idx ? -1 : parent.slot.idx;
                            } else if (win.ambiguous) {
                                win.commitChoice(parent.slot.idx);
                            } else {
                                win.correct(parent.slot.idx);
                            }
                        }
                    }
                }
            }

            // Scale / collapse / menu. Resizing a frameless window by dragging a
            // few-pixel edge is exactly the fine-motor task this product exists to
            // remove, so the stepper is the primary path (ADR-0005 §4).
            Repeater {
                model: [
                    {g: "−",  slot: 7.00, act: "smaller"},
                    {g: "+",  slot: 7.75, act: "bigger"},
                    {g: "▾",  slot: 8.50, act: "collapse"},
                    {g: "⋯",  slot: 9.25, act: "menu"}
                ]
                Rectangle {
                    x: win.u * modelData.slot; y: parent.height * 0.14
                    width: win.u * 0.70; height: parent.height * 0.72
                    radius: Math.max(2, win.u * 0.10)
                    color: ma.containsMouse ? pal.panelHover : "transparent"
                    Text {
                        anchors.centerIn: parent; text: modelData.g
                        font.pixelSize: Math.max(9, win.u * 0.30); color: pal.mutedText
                        font.family: win.uiFont
                    }
                    MouseArea {
                        id: ma
                        anchors.fill: parent; hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            if (modelData.act === "smaller")       win.rescale(1 / 1.12);
                            else if (modelData.act === "bigger")   win.rescale(1.12);
                            else if (modelData.act === "collapse") win.collapse();
                            else                                   win.toggleMenu();
                        }
                    }
                }
            }
        }

        // ---------------- letter block = the glide surface ----------------
        Item {
            id: kb
            x: 0; y: win.chromeH
            width: parent.width; height: 3 * win.rowH

            // Letters. Geometry comes from decoder.keys — the same layout.json the
            // decoder scores against, so the two cannot drift (spec §7.2).
            Repeater {
                model: win.layer === "abc" ? decoder.keys : []
                Rectangle {
                    // Parked = the cursor is resting here, so this is where the
                    // next glide would START. Deliberately a RING, not the fill
                    // and scale the glide uses: "you are here" and "the glide is
                    // crossing here" are different facts and must not look alike.
                    readonly property bool parked: modelData.l === win.parkedKey && !surface.swiping
                    x: modelData.x * kb.width - win.u / 2 + win.kgap
                    y: modelData.y * kb.height - win.rowH / 2 + win.kgap
                    width:  win.u - 2 * win.kgap
                    height: win.rowH - 2 * win.kgap
                    radius: win.krad
                    color: modelData.l === win.activeKey ? pal.keyHover : pal.keyBackground
                    border.color: parked ? pal.accent : pal.keyOutline
                    border.width: parked ? Math.max(2, win.u * 0.045) : 1
                    z: modelData.l === win.activeKey ? 1 : 0
                    transformOrigin: Item.Center
                    Behavior on scale { NumberAnimation { duration: 70; easing.type: Easing.OutCubic } }
                    scale: modelData.l === win.activeKey ? 1.14 : 1.0
                    Text {
                        anchors.centerIn: parent
                        text: win.shiftState > 0 ? modelData.l : modelData.c
                        font.weight: Font.Medium
                        font.family: win.uiFont
                        font.pixelSize: Math.max(9, Math.min(win.u, win.rowH) * 0.46)
                        color: pal.keyText
                    }
                }
            }

            // Symbols layer — same grid, tap-only.
            Repeater {
                model: win.layer === "sym" ? win.symKeys : []
                Rectangle {
                    x: modelData.x * kb.width - win.u / 2 + win.kgap
                    y: modelData.y * kb.height - win.rowH / 2 + win.kgap
                    width:  win.u - 2 * win.kgap
                    height: win.rowH - 2 * win.kgap
                    radius: win.krad
                    color: sym.pressed ? pal.keyHover : pal.keyBackground
                    // Same "you are here" ring as the letter layer — this layer
                    // is tap-only, so knowing what you are about to hit matters
                    // just as much and there is no glide trail to tell you.
                    border.color: sym.containsMouse ? pal.accent : pal.keyOutline
                    border.width: sym.containsMouse ? Math.max(2, win.u * 0.045) : 1
                    Text {
                        anchors.centerIn: parent; text: modelData.l
                        font.weight: Font.Medium
                        font.family: win.uiFont
                        font.pixelSize: Math.max(9, Math.min(win.u, win.rowH) * 0.42)
                        color: pal.keyText
                    }
                    MouseArea {
                        id: sym
                        anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                        hoverEnabled: true
                        onClicked: {
                            injector.typeChar(modelData.c);
                            win.injected += modelData.c;
                        }
                    }
                }
            }

            // Live glide trail. It used to be painted only after release and never
            // cleared, so what you saw mid-glide was the PREVIOUS word's path.
            Canvas {
                id: pathCanvas
                anchors.fill: parent
                property var pts: []
                Behavior on opacity { NumberAnimation { duration: 220 } }
                onPaint: {
                    const ctx = getContext("2d"); ctx.reset();
                    if (pts.length < 2) return;
                    ctx.strokeStyle = pal.accent;
                    ctx.lineWidth = Math.max(2, Math.min(win.u, win.rowH) * 0.13);
                    ctx.lineCap = "round"; ctx.lineJoin = "round";
                    ctx.beginPath();
                    ctx.moveTo(pts[0].x * width, pts[0].y * height);
                    for (let i = 1; i < pts.length; i++) ctx.lineTo(pts[i].x * width, pts[i].y * height);
                    ctx.stroke();
                }
            }
            Timer { id: pathFade; interval: 420; onTriggered: pathCanvas.opacity = 0 }

            SwipeSurface {
                id: surface
                anchors.fill: parent
                cols: 10; rows: 3          // key pitch, for the tap/swipe threshold
                enabled: win.layer === "abc"
                onCursorMoved: function (nx, ny) {
                    if (!swiping) return;
                    const k = win.nearestKey(nx, ny);
                    win.activeKey = k ? k.l : "";
                    pathCanvas.pts.push({x: nx, y: ny});
                    pathCanvas.requestPaint();
                }
                // Between glides: show which key a press would start on.
                onHoverMoved: function (nx, ny) {
                    const k = win.nearestKey(nx, ny);
                    win.parkedKey = k ? k.l : "";
                }
                onHoverLeft: win.parkedKey = ""
                onSwipingChanged: {
                    if (swiping) {
                        win.parkedKey = "";       // the glide owns the highlight now
                        pathFade.stop();
                        pathCanvas.opacity = 1;
                        pathCanvas.pts = [];
                        pathCanvas.requestPaint();
                    } else {
                        win.activeKey = "";
                        // The cursor is now parked wherever the stroke ended, so
                        // show that at once. Waiting for the next hover event
                        // would blank the ring after every tap until the user
                        // happened to move — and "where does the next glide
                        // start" is exactly the question at that moment.
                        const p = pathCanvas.pts.length ? pathCanvas.pts[pathCanvas.pts.length - 1] : null;
                        const k = p ? win.nearestKey(p.x, p.y) : null;
                        win.parkedKey = k ? k.l : "";
                        pathFade.restart();
                    }
                }
                onSwipeCompleted: function (points) { win.glideFinished(points) }
                onTapped: function (nx, ny) {
                    const k = win.nearestKey(nx, ny);
                    if (!k) return;
                    win.typeKey(k.c);
                    win.activeKey = k.l;
                    tapFlash.restart();
                }
            }
            Timer { id: tapFlash; interval: 120; onTriggered: if (!surface.swiping) win.activeKey = "" }

            // ---- the wings: 1.5u at each end of row 3, previously empty ----
            // Declared AFTER the SwipeSurface so they take the press: a glide may
            // pass over them, but may not START on them.
            Rectangle {
                id: shiftKey
                x: win.kgap; y: 2 * win.rowH + win.kgap
                width: 1.5 * win.u - 2 * win.kgap; height: win.rowH - 2 * win.kgap
                radius: win.krad
                color: win.shiftState === 2 ? pal.accent : (win.shiftState === 1 ? pal.keyHover : pal.actionBackground)
                border.color: sh.containsMouse ? pal.accent : pal.keyOutline
                border.width: sh.containsMouse ? Math.max(2, win.u * 0.045) : 1
                Text {
                    anchors.centerIn: parent
                    text: win.shiftState === 2 ? "⇪" : "⇧"
                    font.pixelSize: Math.max(10, Math.min(win.u, win.rowH) * 0.50)
                    font.family: win.uiFont
                    color: win.shiftState === 2 ? pal.accentText : pal.actionText
                }
                MouseArea {
                    id: sh
                    anchors.fill: parent; hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: win.cycleShift()
                }
            }

            Rectangle {
                id: backspaceKey
                x: 8.5 * win.u + win.kgap; y: 2 * win.rowH + win.kgap
                width: 1.5 * win.u - 2 * win.kgap; height: win.rowH - 2 * win.kgap
                radius: win.krad
                color: bs.pressed ? pal.actionPressed : pal.actionBackground
                border.color: bs.containsMouse ? pal.accent : pal.keyOutline
                border.width: bs.containsMouse ? Math.max(2, win.u * 0.045) : 1
                Text {
                    anchors.centerIn: parent; text: "⌫"
                    font.pixelSize: Math.max(10, Math.min(win.u, win.rowH) * 0.50); color: pal.actionText
                    font.family: win.uiFont
                }
                // tap = word · hold = one char immediately, then repeat
                //   · hold+swipe-left = multi-word delete · swipe-right = undo
                // The hold deletes its first char the moment the threshold trips,
                // not one repeat-interval later. Otherwise "delete exactly one
                // character" was a 70 ms window after a 490 ms wait — unusable,
                // and single chars are exactly what tap-to-type and the symbols
                // layer produce. Now you hold, see one char go, and release.
                MouseArea {
                    id: bs
                    anchors.fill: parent
                    hoverEnabled: true
                    property real startX
                    property int  wordsDeleted
                    property bool swiping
                    property bool acted
                    property var  deletedStack
                    onPressed: function (mouse) {
                        startX = mouse.x; wordsDeleted = 0; swiping = false; acted = false; deletedStack = [];
                        holdDelay.start();
                    }
                    onPositionChanged: function (mouse) {
                        if (!pressed) return;
                        const dx = mouse.x - startX;
                        if (dx < -12) { holdDelay.stop(); swiping = true; }
                        if (swiping) {
                            const target = Math.max(0, Math.floor(-dx / win.wordSwipePx));
                            while (wordsDeleted < target) {
                                const s = win.deleteWord();
                                if (!s.length) break;
                                deletedStack.push(s); wordsDeleted++; acted = true;
                            }
                            while (wordsDeleted > target) {
                                const s = deletedStack.pop();
                                if (s === undefined) break;
                                win.undoWord(s); wordsDeleted--;
                            }
                        }
                    }
                    onReleased: {
                        holdDelay.stop(); charRepeat.stop();
                        if (!acted) win.tapDeleteWord();
                    }
                }
                Timer {
                    id: holdDelay; interval: 420
                    onTriggered: if (!bs.swiping) { win.deleteChar(); bs.acted = true; charRepeat.start() }
                }
                Timer { id: charRepeat; interval: 70; repeat: true; onTriggered: { win.deleteChar(); bs.acted = true } }
            }
        }

        // ---------------- action row: same ten columns, same outer edges ----------------
        Item {
            id: actionRow
            x: 0; y: win.chromeH + 3 * win.rowH
            width: parent.width; height: win.actionH

            Repeater {
                model: [
                    {g: "?123", from: 0.0, to: 1.5, act: "layer"},
                    {g: ",",    from: 1.5, to: 2.5, act: "comma"},
                    {g: "space",from: 2.5, to: 7.5, act: "space"},
                    {g: ".",    from: 7.5, to: 8.5, act: "period"},
                    {g: "⏎",    from: 8.5, to: 10.0, act: "enter"}
                ]
                Rectangle {
                    x: modelData.from * win.u + win.kgap
                    y: win.kgap
                    width: (modelData.to - modelData.from) * win.u - 2 * win.kgap
                    height: parent.height - 2 * win.kgap
                    radius: win.krad
                    color: modelData.act === "layer" && win.layer === "sym" ? pal.accent : pal.actionBackground
                    border.color: am.containsMouse ? pal.accent : pal.keyOutline
                    border.width: am.containsMouse ? Math.max(2, win.u * 0.045) : 1
                    Text {
                        anchors.centerIn: parent
                        text: modelData.act === "layer" && win.layer === "sym" ? "ABC" : modelData.g
                        font.pixelSize: Math.max(8, win.actionH * (modelData.act === "space" || modelData.act === "layer" ? 0.34 : 0.48))
                        font.family: win.uiFont
                        color: modelData.act === "layer" && win.layer === "sym" ? pal.accentText : pal.actionText
                    }
                    MouseArea {
                        id: am
                        anchors.fill: parent; hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            if (modelData.act === "layer")       win.layer = win.layer === "abc" ? "sym" : "abc";
                            else if (modelData.act === "space")  win.tapSpace();
                            else if (modelData.act === "enter")  win.tapEnter();
                            else if (modelData.act === "comma")  win.tapPunct(",");
                            else                                 win.tapPunct(".");
                        }
                    }
                }
            }
        }

        // swipe-delete gauge: one dot per deletable word, filling right-to-left.
        Rectangle {
            id: deleteGauge
            visible: bs.swiping && win.availWords > 0
            y: win.chromeH + 3 * win.rowH - height - win.kgap
            x: parent.width - width - win.u * 0.3
            width: Math.min(parent.width - win.u, win.u * 0.6 + win.availWords * win.u * 0.34)
            height: win.rowH * 0.8; radius: win.krad
            color: pal.diagnosticsBackground; border.color: pal.accent; border.width: 2; z: 5
            Text {
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left; anchors.leftMargin: win.u * 0.2
                text: "−" + bs.wordsDeleted; color: pal.accentText
                font.pixelSize: Math.max(8, win.u * 0.26); font.bold: true
            }
            Row {
                anchors.verticalCenter: parent.verticalCenter
                anchors.right: parent.right; anchors.rightMargin: win.u * 0.2
                layoutDirection: Qt.RightToLeft; spacing: win.u * 0.12
                Repeater {
                    model: win.availWords
                    Rectangle {
                        width: win.u * 0.22; height: width; radius: width / 2
                        color: index < bs.wordsDeleted ? pal.accent : pal.gaugeInactive
                    }
                }
            }
        }

        // ---------------- diagnostics (ADR-0004: opt-in, off by default) ----------------
        Rectangle {
            visible: win.showDiagnostics
            x: 0; width: parent.width
            height: win.chromeH * 0.8
            y: parent.height - height
            color: pal.diagnosticsBackground; opacity: 0.94; z: 40
            Text {
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left; anchors.leftMargin: win.u * 0.15
                anchors.right: parent.right; anchors.rightMargin: win.u * 0.15
                elide: Text.ElideRight
                text: win.stateText() + "   ·   aspect " + win.letterAspect.toFixed(2)
                      + "   ·   " + decoder.layoutId
                      + "   ·   IBus: " + (win.ibusActive ? "active" : "inactive")
                      + "   ·   toggle: " + toggleListener.status
                      + "   ·   preedit: " + (win.clientCaps < 0 ? "no caps reported"
                            : (win.preeditSupported ? "YES" : "no") + " (caps 0x" + win.clientCaps.toString(16) + ")")
                      + "   ·   out queue: " + injector.pending()
                      + "   ·   target gen " + win.targetGen + (injector.focused() ? " (focused)" : " (no focus)")
                      + "   ·   caret: " + (win.caretReports > 0
                            ? win.caretRect.x + "," + win.caretRect.y + " ×" + win.caretReports
                            : "never reported")
                      + "   ·   ⌨ " + (win.injected.length ? win.injected.replace(/\s+$/, "") : "—")
                      + "   ·   ptr L" + pointerSpeed.level
                color: pal.diagnosticsText
                font.pixelSize: Math.max(7, win.u * 0.20); font.family: "monospace"
            }
        }

        // ---------------- history word → its own alternatives (spec §9.3) ----------------
        MouseArea {   // click-away
            anchors.fill: parent; visible: win.histOpen >= 0; z: 55
            onClicked: win.histOpen = -1
        }
        Rectangle {
            id: histPopup
            readonly property var entry: win.histOpen >= 0 && win.histOpen < win.history.length
                                         ? win.history[win.histOpen] : null
            readonly property var alts: {
                if (!entry) return [];
                var a = [];
                for (var i = 0; i < entry.cands.length && a.length < 4; i++)
                    if (entry.cands[i].text !== entry.text) a.push(entry.cands[i].text);
                return a;
            }
            visible: entry !== null; z: 56
            x: Math.max(win.u * 0.1, Math.min(parent.width - width - win.u * 0.1, win.histOpenX))
            y: win.chromeH
            width: win.u * 2.6; height: histCol.height + win.u * 0.2
            color: pal.panelBackground; radius: Math.max(3, win.u * 0.12)
            border.color: pal.keyOutline; border.width: 1
            Column {
                id: histCol
                x: 0; y: win.u * 0.1; width: parent.width
                Repeater {
                    // Alternatives from that word's own glide, then Delete. "Add to
                    // dictionary" (spec §9.3) is deliberately absent: it needs the
                    // personal dictionary (§10.2) to reach the decode lexicon, and
                    // bumping a word the trie doesn't contain would do nothing.
                    model: histPopup.alts.length + 1
                    Rectangle {
                        readonly property bool isDelete: index === histPopup.alts.length
                        width: parent.width; height: win.u * 0.58
                        color: hm.containsMouse ? pal.panelHover : pal.panelBackground
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            x: win.u * 0.18; width: parent.width - x * 2
                            elide: Text.ElideRight
                            text: parent.isDelete ? "Delete" : histPopup.alts[index]
                            font.pixelSize: Math.max(8, win.u * 0.22)
                            color: parent.isDelete ? pal.destructive : pal.keyText
                            font.family: win.uiFont
                        }
                        MouseArea {
                            id: hm
                            anchors.fill: parent; hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                const hi = win.histOpen;
                                if (parent.isDelete) { win.deleteHistory(hi); return; }
                                const old = win.history[hi].text;
                                var nw = histPopup.alts[index];
                                if (old.length && old[0] !== old[0].toLowerCase())
                                    nw = nw.charAt(0).toUpperCase() + nw.substring(1);
                                win.replaceHistory(hi, nw);
                            }
                        }
                    }
                }
            }
        }

        // ---------------- ⋯ menu ----------------
        MouseArea {   // click-away
            anchors.fill: parent; visible: win.menuOpen; z: 60
            onClicked: win.menuOpen = false
        }
        Rectangle {
            id: menuPanel
            visible: win.menuOpen && !win.appearanceOpen; z: 61
            x: Math.min(parent.width - width - win.u * 0.1, win.u * 6.2)
            y: win.chromeH
            width: win.u * 4.6
            height: Math.min(menuCol.height + win.u * 0.2,
                             content.height - y - win.u * 0.1)
            color: pal.panelBackground; radius: Math.max(3, win.u * 0.12)
            border.color: pal.keyOutline; border.width: 1
            clip: true
            onVisibleChanged: if (visible) menuFlick.contentY = 0

            Flickable {
                id: menuFlick
                anchors.fill: parent
                contentWidth: width
                contentHeight: menuCol.height + win.u * 0.2
                boundsBehavior: Flickable.StopAtBounds
                interactive: contentHeight > height

                Column {
                    id: menuCol
                    x: 0; y: win.u * 0.1; width: menuFlick.width
                    Repeater {
                        model: [
                            {g: "Small  · 440×220",  act: "s"},
                            {g: "Medium · 560×280",  act: "m"},
                            {g: "Large  · 720×360",  act: "l"},
                            {g: "Appearance",         act: "appearance"},
                            {g: "Hide completely",   act: "hide"},
                            {g: "Avoid the caret",   act: "caret"},
                            {g: "Pointer slow",      act: "ptr"},
                            {g: "Diagnostics",       act: "diag"},
                            {g: "Quit OpenGlide",    act: "quit"}
                        ]
                        Rectangle {
                        readonly property bool disabled:
                            (modelData.act === "hide" && !toggleListener.available)
                            || (modelData.act === "ptr" && !pointerSpeed.available)
                        // A greyed-out row that only says what it CAN'T do is a
                        // dead end — the user is told a feature exists, told they
                        // may not have it, and given nothing to act on. The second
                        // line is the way out, so it names the actual fix.
                        readonly property string hint:
                              (modelData.act === "hide" && !toggleListener.available)
                                  ? "add yourself to the 'input' group"
                            : (modelData.act === "ptr" && !pointerSpeed.available)
                                  ? "needs the GNOME peripherals schema"
                            : (modelData.act === "caret" && win.caretReports === 0)
                                  ? "this app never reports its caret"
                            : ""
                        width: parent.width
                        height: hint.length ? win.u * 0.92 : win.u * 0.62
                        color: mi.containsMouse && !disabled ? pal.panelHover : pal.panelBackground
                        Column {
                            anchors.verticalCenter: parent.verticalCenter
                            x: win.u * 0.22
                            width: parent.width - x * 2
                            spacing: win.u * 0.04
                            Text {
                                width: parent.width
                                elide: Text.ElideRight
                                text: modelData.act === "appearance"
                                            ? "Appearance · " + win.themeLabel() + " / " + win.colorwayLabel()
                                      : modelData.act === "caret" ? (win.avoidCaret ? "✓ " : "") + modelData.g
                                      : modelData.act === "diag" && win.showDiagnostics ? "✓ " + modelData.g
                                      : modelData.act === "hide" && toggleListener.available
                                            ? "Hide · " + win.toggleGesture() + " to return"
                                      : modelData.act === "ptr" && pointerSpeed.available
                                            ? "Pointer slow: " + (pointerSpeed.level === 0 ? "off" : "L" + pointerSpeed.level)
                                      : modelData.g
                                font.pixelSize: Math.max(8, win.u * 0.21)
                                font.family: win.uiFont
                                color: parent.parent.disabled ? pal.disabledText
                                       : modelData.act === "quit" ? pal.destructive : pal.keyText
                            }
                            Text {
                                visible: parent.parent.hint.length > 0
                                width: parent.width
                                elide: Text.ElideRight
                                text: parent.parent.hint
                                font.pixelSize: Math.max(7, win.u * 0.165)
                                color: pal.mutedText
                                font.family: win.uiFont
                            }
                        }
                        MouseArea {
                            id: mi
                            anchors.fill: parent; hoverEnabled: true
                            cursorShape: parent.disabled ? Qt.ArrowCursor : Qt.PointingHandCursor
                            onClicked: {
                                if (parent.disabled) return;
                                if (modelData.act === "s")         win.preset(440, 220);
                                else if (modelData.act === "m")    win.preset(560, 280);
                                else if (modelData.act === "l")    win.preset(720, 360);
                                else if (modelData.act === "appearance") {
                                    win.appearanceOpen = true;
                                    win.customColorOpen = false;
                                }
                                else if (modelData.act === "hide") win.hideCompletely();
                                else if (modelData.act === "caret") {
                                    win.avoidCaret = !win.avoidCaret;
                                    settings.setValue("window/avoidCaret", win.avoidCaret);
                                    win.menuOpen = false;
                                }
                                else if (modelData.act === "diag") { win.showDiagnostics = !win.showDiagnostics; win.menuOpen = false; }
                                else if (modelData.act === "ptr") {
                                    // Cycle slowdown 0→1→2→3→0; keep the menu open so the
                                    // level is visible while the user dials it in.
                                    pointerSpeed.level = (pointerSpeed.level + 1) % 4;
                                    settings.setValue("pointer/level", pointerSpeed.level);
                                    settings.sync();
                                }
                                else { win.persistGeometry(); Qt.quit(); }
                            }
                        }
                        }
                    }
                }
            }

            // The menu lives inside a no-focus keyboard window, so it cannot
            // safely become a separate desktop popup. Show that the constrained
            // viewport scrolls instead of silently clipping its final actions.
            Rectangle {
                visible: menuFlick.contentHeight > menuFlick.height
                anchors.right: parent.right
                anchors.rightMargin: 2
                width: Math.max(3, win.u * 0.07)
                height: Math.max(win.u * 0.45,
                                 parent.height * menuFlick.height / menuFlick.contentHeight)
                y: (parent.height - height) * menuFlick.contentY
                   / Math.max(1, menuFlick.contentHeight - menuFlick.height)
                radius: width / 2
                color: pal.mutedText
                opacity: 0.65
            }
        }

        // ---------------- appearance: brightness and colour are independent -------
        Rectangle {
            id: appearancePanel
            visible: win.menuOpen && win.appearanceOpen && !win.customColorOpen
            z: 61
            x: Math.min(parent.width - width - win.u * 0.1, win.u * 6.2)
            y: win.chromeH
            width: win.u * 4.6
            height: content.height - y - win.u * 0.1
            color: pal.panelBackground
            radius: Math.max(3, win.u * 0.12)
            border.color: pal.keyOutline; border.width: 1
            clip: true
            onVisibleChanged: if (visible) appearanceFlick.contentY = 0

            Flickable {
                id: appearanceFlick
                anchors.fill: parent
                contentWidth: width
                contentHeight: appearanceCol.height + win.u * 0.2
                boundsBehavior: Flickable.StopAtBounds
                interactive: contentHeight > height

                Column {
                    id: appearanceCol
                    x: 0; y: win.u * 0.1; width: appearanceFlick.width

                    Rectangle {
                        width: parent.width; height: win.u * 0.62
                        color: apBack.containsMouse ? pal.panelHover : pal.panelBackground
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            x: win.u * 0.18
                            text: "‹  Appearance"
                            color: pal.keyText; font.family: win.uiFont
                            font.weight: Font.Medium
                            font.pixelSize: Math.max(8, win.u * 0.22)
                        }
                        MouseArea {
                            id: apBack; anchors.fill: parent; hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: win.appearanceOpen = false
                        }
                    }

                    Text {
                        width: parent.width; height: win.u * 0.42
                        leftPadding: win.u * 0.2
                        verticalAlignment: Text.AlignVCenter
                        text: "MODE"
                        color: pal.mutedText; font.family: win.uiFont
                        font.bold: true; font.pixelSize: Math.max(7, win.u * 0.16)
                    }
                    Row {
                        width: parent.width; height: win.u * 0.68
                        Repeater {
                            model: [{n: "system", l: "System"}, {n: "light", l: "Light"}, {n: "dark", l: "Dark"}]
                            Rectangle {
                                readonly property bool selected: win.themeMode === modelData.n
                                width: appearanceCol.width / 3; height: win.u * 0.68
                                color: selected ? pal.accentTint : (modeMouse.containsMouse ? pal.panelHover : pal.panelBackground)
                                border.color: selected ? pal.accent : pal.keyOutline
                                border.width: selected ? 2 : 1
                                Text {
                                    anchors.centerIn: parent; text: modelData.l
                                    color: parent.selected ? pal.accent : pal.keyText
                                    font.family: win.uiFont; font.pixelSize: Math.max(8, win.u * 0.19)
                                    font.weight: parent.selected ? Font.DemiBold : Font.Normal
                                }
                                MouseArea {
                                    id: modeMouse; anchors.fill: parent; hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: win.setTheme(modelData.n)
                                }
                            }
                        }
                    }

                    Text {
                        width: parent.width; height: win.u * 0.48
                        leftPadding: win.u * 0.2
                        verticalAlignment: Text.AlignVCenter
                        text: "COLOR"
                        color: pal.mutedText; font.family: win.uiFont
                        font.bold: true; font.pixelSize: Math.max(7, win.u * 0.16)
                    }
                    Grid {
                        width: parent.width; height: win.u * 2.16
                        columns: 3
                        Repeater {
                            model: [
                                {n: "neutral", l: "Neutral"}, {n: "ocean", l: "Ocean"},
                                {n: "forest", l: "Forest"}, {n: "plum", l: "Plum"},
                                {n: "rose", l: "Rose"}, {n: "amber", l: "Amber"},
                                {n: "custom", l: "Custom…"}
                            ]
                            Rectangle {
                                readonly property bool selected: win.colorway === modelData.n
                                width: appearanceCol.width / 3; height: win.u * 0.72
                                color: selected ? pal.accentTint : (swatchMouse.containsMouse ? pal.panelHover : pal.panelBackground)
                                border.color: selected ? pal.accent : "transparent"
                                border.width: selected ? 2 : 0
                                Rectangle {
                                    x: win.u * 0.14
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: win.u * 0.27; height: width; radius: width / 2
                                    color: win.colorwaySeed(modelData.n)
                                    border.color: pal.keyOutline; border.width: 1
                                }
                                Text {
                                    x: win.u * 0.48; width: parent.width - x - win.u * 0.06
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: modelData.l; elide: Text.ElideRight
                                    color: parent.selected ? pal.accent : pal.keyText
                                    font.family: win.uiFont; font.pixelSize: Math.max(7, win.u * 0.17)
                                    font.weight: parent.selected ? Font.DemiBold : Font.Normal
                                }
                                MouseArea {
                                    id: swatchMouse; anchors.fill: parent; hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        if (modelData.n === "custom") {
                                            win.setColorway("custom");
                                            win.customColorOpen = true;
                                        } else {
                                            win.setColorway(modelData.n);
                                        }
                                    }
                                }
                            }
                        }
                    }

                    Rectangle {
                        width: parent.width; height: win.u * 0.62
                        color: resetMouse.containsMouse ? pal.panelHover : pal.panelBackground
                        Text {
                            anchors.centerIn: parent; text: "Reset appearance"
                            color: pal.mutedText; font.family: win.uiFont
                            font.pixelSize: Math.max(8, win.u * 0.18)
                        }
                        MouseArea {
                            id: resetMouse; anchors.fill: parent; hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: { win.setTheme("system"); win.setColorway("neutral"); }
                        }
                    }
                }
            }

            Rectangle {
                visible: appearanceFlick.contentHeight > appearanceFlick.height
                anchors.right: parent.right; anchors.rightMargin: 2
                width: Math.max(3, win.u * 0.07)
                height: Math.max(win.u * 0.45,
                                 parent.height * appearanceFlick.height / appearanceFlick.contentHeight)
                y: (parent.height - height) * appearanceFlick.contentY
                   / Math.max(1, appearanceFlick.contentHeight - appearanceFlick.height)
                radius: width / 2; color: pal.mutedText; opacity: 0.65
            }
        }

        // ---------------- custom colour: mouse-first RGB picker --------------------
        Rectangle {
            id: customColorPanel
            visible: win.menuOpen && win.appearanceOpen && win.customColorOpen
            z: 62
            x: Math.min(parent.width - width - win.u * 0.1, win.u * 6.2)
            y: win.chromeH
            width: win.u * 4.6
            height: content.height - y - win.u * 0.1
            color: pal.panelBackground
            radius: Math.max(3, win.u * 0.12)
            border.color: pal.keyOutline; border.width: 1
            clip: true
            onVisibleChanged: if (visible) customFlick.contentY = 0

            Flickable {
                id: customFlick
                anchors.fill: parent
                contentWidth: width
                contentHeight: customCol.height + win.u * 0.2
                boundsBehavior: Flickable.StopAtBounds
                interactive: contentHeight > height

                Column {
                    id: customCol
                    x: 0; y: win.u * 0.1; width: customFlick.width

                    Rectangle {
                        width: parent.width; height: win.u * 0.62
                        color: customBack.containsMouse ? pal.panelHover : pal.panelBackground
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            x: win.u * 0.18; text: "‹  Custom color"
                            color: pal.keyText; font.family: win.uiFont
                            font.weight: Font.Medium
                            font.pixelSize: Math.max(8, win.u * 0.22)
                        }
                        MouseArea {
                            id: customBack; anchors.fill: parent; hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: { win.saveCustomColor(); win.customColorOpen = false; }
                        }
                    }

                    Item {
                        width: parent.width; height: win.u * 0.78
                        Rectangle {
                            anchors.centerIn: parent
                            width: parent.width - win.u * 0.4; height: win.u * 0.52
                            radius: win.u * 0.12
                            color: win.seedColor
                            border.color: pal.keyOutline; border.width: 1
                            Text {
                                anchors.centerIn: parent; text: win.customHex
                                color: win.accentTextFor(win.seedColor)
                                font.family: "monospace"; font.bold: true
                                font.pixelSize: Math.max(9, win.u * 0.22)
                            }
                        }
                    }

                    Repeater {
                        model: [
                            {c: "r", l: "R", fill: "#d85a5a"},
                            {c: "g", l: "G", fill: "#4caa70"},
                            {c: "b", l: "B", fill: "#538bd4"}
                        ]
                        Rectangle {
                            id: sliderRow
                            readonly property int channelValue: modelData.c === "r" ? win.customRed
                                                                  : modelData.c === "g" ? win.customGreen
                                                                                           : win.customBlue
                            width: customCol.width; height: win.u * 0.72
                            color: "transparent"
                            Text {
                                x: win.u * 0.2; width: win.u * 0.35
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData.l; color: pal.keyText
                                font.family: win.uiFont; font.bold: true
                                font.pixelSize: Math.max(8, win.u * 0.19)
                            }
                            Rectangle {
                                id: sliderTrack
                                x: win.u * 0.62
                                width: parent.width - win.u * 1.45
                                height: Math.max(5, win.u * 0.12)
                                anchors.verticalCenter: parent.verticalCenter
                                radius: height / 2; color: pal.actionBackground
                                Rectangle {
                                    width: parent.width * sliderRow.channelValue / 255
                                    height: parent.height; radius: parent.radius
                                    color: modelData.fill
                                }
                                Rectangle {
                                    x: parent.width * sliderRow.channelValue / 255 - width / 2
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: win.u * 0.28; height: width; radius: width / 2
                                    color: pal.panelBackground
                                    border.color: modelData.fill; border.width: 2
                                }
                            }
                            Text {
                                anchors.right: parent.right; anchors.rightMargin: win.u * 0.16
                                anchors.verticalCenter: parent.verticalCenter
                                width: win.u * 0.58
                                horizontalAlignment: Text.AlignRight
                                text: sliderRow.channelValue
                                color: pal.mutedText; font.family: "monospace"
                                font.pixelSize: Math.max(8, win.u * 0.18)
                            }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                function updateValue(mouse) {
                                    win.setCustomChannel(modelData.c,
                                        (mouse.x - sliderTrack.x) * 255 / sliderTrack.width);
                                }
                                onPressed: function(mouse) { updateValue(mouse); }
                                onPositionChanged: function(mouse) { if (pressed) updateValue(mouse); }
                                onReleased: win.saveCustomColor()
                                onWheel: function(wheel) {
                                    win.setCustomChannel(modelData.c, sliderRow.channelValue
                                                         + (wheel.angleDelta.y > 0 ? 1 : -1));
                                    win.saveCustomColor();
                                    wheel.accepted = true;
                                }
                            }
                        }
                    }

                    Text {
                        width: parent.width; height: win.u * 0.52
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        text: "Changes preview live"
                        color: pal.mutedText; font.family: win.uiFont
                        font.pixelSize: Math.max(7, win.u * 0.16)
                    }
                }
            }

            Rectangle {
                visible: customFlick.contentHeight > customFlick.height
                anchors.right: parent.right; anchors.rightMargin: 2
                width: Math.max(3, win.u * 0.07)
                height: Math.max(win.u * 0.45,
                                 parent.height * customFlick.height / customFlick.contentHeight)
                y: (parent.height - height) * customFlick.contentY
                   / Math.max(1, customFlick.contentHeight - customFlick.height)
                radius: width / 2; color: pal.mutedText; opacity: 0.65
            }
        }
    }

    // ======================= resize ring =======================
    // Deliberately NOT a full ring: the left/right strips stop at the chrome and
    // action bands so they never overlap the outer edge of the letter block —
    // grabbing a resize handle instead of starting a glide on Q or P would be a
    // worse bug than the one this fixes. Corners and the top/bottom edges cover
    // the rest; the stepper and presets are the primary path anyway.
    Item {
        visible: !win.collapsed
        anchors.fill: parent
        z: 80

        MouseArea {   // top
            x: 0; y: 0; width: parent.width; height: win.frame
            cursorShape: Qt.SizeVerCursor
            property real by: 0
            property real bh: 0
            property real s0y: 0
            onPressed: function (mouse) {
                const mp = mapToItem(win.contentItem, mouse.x, mouse.y);
                by = windowCtl.posY(); bh = win.height;
                s0y = windowCtl.posY() + mp.y;
            }
            onPositionChanged: function (mouse) {
                const mp = mapToItem(win.contentItem, mouse.x, mouse.y);
                const d = (windowCtl.posY() + mp.y) - s0y;
                win.moveTo(windowCtl.posX(), by + d);
                win.sizeTo(win.width, bh - d);
            }
            onReleased: win.persistGeometry()
        }
        MouseArea {   // bottom
            x: 0; y: parent.height - win.frame; width: parent.width; height: win.frame
            cursorShape: Qt.SizeVerCursor
            property real bh: 0
            property real s0y: 0
            onPressed: function (mouse) {
                const mp = mapToItem(win.contentItem, mouse.x, mouse.y);
                bh = win.height; s0y = windowCtl.posY() + mp.y;
            }
            onPositionChanged: function (mouse) {
                const mp = mapToItem(win.contentItem, mouse.x, mouse.y);
                win.sizeTo(win.width, bh + (windowCtl.posY() + mp.y) - s0y);
            }
            onReleased: win.persistGeometry()
        }
        MouseArea {   // left, chrome band only
            x: 0; y: win.frame; width: win.frame; height: win.chromeH
            cursorShape: Qt.SizeHorCursor
            property real bx: 0
            property real bw: 0
            property real s0x: 0
            onPressed: function (mouse) {
                const mp = mapToItem(win.contentItem, mouse.x, mouse.y);
                bx = windowCtl.posX(); bw = win.width;
                s0x = windowCtl.posX() + mp.x;
            }
            onPositionChanged: function (mouse) {
                const mp = mapToItem(win.contentItem, mouse.x, mouse.y);
                const d = (windowCtl.posX() + mp.x) - s0x;
                win.moveTo(bx + d, windowCtl.posY());
                win.sizeTo(bw - d, win.height);
            }
            onReleased: win.persistGeometry()
        }
        MouseArea {   // left, action band only
            x: 0; y: parent.height - win.frame - win.actionH; width: win.frame; height: win.actionH
            cursorShape: Qt.SizeHorCursor
            property real bx: 0
            property real bw: 0
            property real s0x: 0
            onPressed: function (mouse) {
                const mp = mapToItem(win.contentItem, mouse.x, mouse.y);
                bx = windowCtl.posX(); bw = win.width;
                s0x = windowCtl.posX() + mp.x;
            }
            onPositionChanged: function (mouse) {
                const mp = mapToItem(win.contentItem, mouse.x, mouse.y);
                const d = (windowCtl.posX() + mp.x) - s0x;
                win.moveTo(bx + d, windowCtl.posY());
                win.sizeTo(bw - d, win.height);
            }
            onReleased: win.persistGeometry()
        }
        MouseArea {   // right, chrome band only
            x: parent.width - win.frame; y: win.frame; width: win.frame; height: win.chromeH
            cursorShape: Qt.SizeHorCursor
            property real bw: 0
            property real s0x: 0
            onPressed: function (mouse) {
                const mp = mapToItem(win.contentItem, mouse.x, mouse.y);
                bw = win.width; s0x = windowCtl.posX() + mp.x;
            }
            onPositionChanged: function (mouse) {
                const mp = mapToItem(win.contentItem, mouse.x, mouse.y);
                win.sizeTo(bw + (windowCtl.posX() + mp.x) - s0x, win.height);
            }
            onReleased: win.persistGeometry()
        }
        MouseArea {   // right, action band only
            x: parent.width - win.frame; y: parent.height - win.frame - win.actionH
            width: win.frame; height: win.actionH
            cursorShape: Qt.SizeHorCursor
            property real bw: 0
            property real s0x: 0
            onPressed: function (mouse) {
                const mp = mapToItem(win.contentItem, mouse.x, mouse.y);
                bw = win.width; s0x = windowCtl.posX() + mp.x;
            }
            onPositionChanged: function (mouse) {
                const mp = mapToItem(win.contentItem, mouse.x, mouse.y);
                win.sizeTo(bw + (windowCtl.posX() + mp.x) - s0x, win.height);
            }
            onReleased: win.persistGeometry()
        }

        // corners — the discoverable grab targets, ≥16 px (ADR-0005 §4)
        Repeater {
            model: [
                {cx: 0, cy: 0, e: Qt.TopEdge    | Qt.LeftEdge,  cur: Qt.SizeFDiagCursor},
                {cx: 1, cy: 0, e: Qt.TopEdge    | Qt.RightEdge, cur: Qt.SizeBDiagCursor},
                {cx: 0, cy: 1, e: Qt.BottomEdge | Qt.LeftEdge,  cur: Qt.SizeBDiagCursor},
                {cx: 1, cy: 1, e: Qt.BottomEdge | Qt.RightEdge, cur: Qt.SizeFDiagCursor}
            ]
            MouseArea {
                readonly property real sz: Math.max(16, win.frame * 3)
                width: sz; height: sz
                x: modelData.cx === 0 ? 0 : parent.width - sz
                y: modelData.cy === 0 ? 0 : parent.height - sz
                cursorShape: modelData.cur
                property real bx: 0
                property real by: 0
                property real bw: 0
                property real bh: 0
                property real s0x: 0
                property real s0y: 0
                onPressed: function (mouse) {
                    const mp = mapToItem(win.contentItem, mouse.x, mouse.y);
                    bx = windowCtl.posX(); by = windowCtl.posY();
                    bw = win.width; bh = win.height;
                    s0x = windowCtl.posX() + mp.x; s0y = windowCtl.posY() + mp.y;
                }
                onPositionChanged: function (mouse) {
                    const mp = mapToItem(win.contentItem, mouse.x, mouse.y);
                    const dx = (windowCtl.posX() + mp.x) - s0x;
                    const dy = (windowCtl.posY() + mp.y) - s0y;
                    const left  = modelData.e & Qt.LeftEdge;
                    const top   = modelData.e & Qt.TopEdge;
                    win.moveTo(bx + (left ? dx : 0), by + (top ? dy : 0));
                    win.sizeTo(bw + (left ? -dx : dx), bh + (top ? -dy : dy));
                }
                onReleased: win.persistGeometry()
            }
        }

        // visible grip, bottom-right
        Canvas {
            width: Math.max(14, win.frame * 2.6); height: width
            x: parent.width - width; y: parent.height - height
            onPaint: {
                const ctx = getContext("2d"); ctx.reset();
                ctx.strokeStyle = pal.mutedText; ctx.lineWidth = 1;
                for (let i = 1; i <= 3; i++) {
                    ctx.beginPath();
                    ctx.moveTo(width - i * (width / 4) - 1, height - 1);
                    ctx.lineTo(width - 1, height - i * (height / 4) - 1);
                    ctx.stroke();
                }
            }
        }
    }

    Connections {
        target: decoder
        function onCandidatesReady(greedy, candidates, ms, gid) {
            watchdog.stop();
            win.greedyText = greedy; win.candidates = candidates; win.decMs = ms;
            win.lastGid = gid || 0;
            win.pending = false; win.timedOut = false;
            // A glide refused while this one was in flight left a "busy" note
            // behind; the word landing is the answer to it, so clear it rather
            // than letting it sit there contradicting the fresh candidates.
            win.dropped = false;
            // E4: a photo-finish between the top two is a question, not an
            // answer — hold the commit and let a click decide (flushAmbiguous
            // defaults to top-1 on the next input, so ignoring it is free).
            win.ambiguous = win.candsAmbiguous(candidates, win.ambigMargin);
            if (candidates.length > 0 && !win.ambiguous) win.commitDecoded(candidates[0].text);
        }
        function onDecoderDied() {
            watchdog.stop();
            win.decoderDead = true; win.pending = false; win.candidates = [];
        }
    }
}
