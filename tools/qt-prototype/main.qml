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
    width: 560; height: 280
    visible: true
    flags: Qt.WindowStaysOnTopHint | Qt.WindowDoesNotAcceptFocus | Qt.FramelessWindowHint
    color: pal.shell
    minimumWidth:  collapsed ? 1 : 320
    minimumHeight: collapsed ? 1 : 170

    // ---- FUTO-inspired palette ----
    readonly property QtObject pal: QtObject {
        readonly property color shell: "#c8ccd1"       // frame / resize ring
        readonly property color bg: "#e8eaed"          // keyboard background
        readonly property color key: "#ffffff"         // letter keys
        readonly property color keyPop: "#d3e3fd"      // key under the cursor
        readonly property color keyText: "#202124"
        readonly property color action: "#bdc1c6"      // space / punct / backspace
        readonly property color actionHold: "#9aa0a6"  // backspace while held
        readonly property color actionText: "#3c4043"
        readonly property color accent: "#1a73e8"
        readonly property color accentText: "#ffffff"
        readonly property color candBar: "#f1f3f4"
        readonly property color muted: "#5f6368"
        readonly property color committedBg: "#202124"
        readonly property color committedText: "#e8eaed"
    }

    // ---- geometry: two units, everything else derived (ADR-0005 §1) ----
    // The frame is the resize ring; it is the only deliberate dead space left.
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

    // ---- window state ----
    property bool collapsed: false
    property int  expandedW: 560
    property int  expandedH: 280
    readonly property int puckW: Math.max(96, Math.round(expandedW * 0.22))
    readonly property int puckH: Math.max(30, Math.round(expandedW * 0.075))
    property bool menuOpen: false
    property bool showDiagnostics: false

    // ---- input state ----
    property string layer: "abc"        // "abc" | "sym"
    property int    shiftState: 0       // 0 off · 1 once · 2 lock
    property var    candidates: []
    property string greedyText: ""
    property real   decMs: 0.0
    property bool   pending: false
    property bool   lastShort: false
    property bool   timedOut: false
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

    // What the chrome slots show. Right after a glide those slots ARE the
    // alternatives for the word just committed, so candidates take priority;
    // once the candidates are stale (you typed, spaced, punctuated) the same
    // slots become the recent words, each still carrying its own alternatives.
    // Contextual, so history costs no permanent rows and the glide surface stays
    // at 60% (ADR-0005 §1).
    readonly property bool showingHistory: candidates.length === 0 && history.length > 0
    readonly property var chromeSlots: {
        var a = [];
        if (pendingUndo.length)      // an undo offer outranks both
            a.push({text: "↶ " + pendingUndo.replace(/\s+$/, ""), hist: false, undo: true, idx: -1});
        const room = 4 - a.length;
        if (candidates.length > 0) {
            for (var i = 0; i < Math.min(room, candidates.length); i++)
                a.push({text: candidates[i].text, hist: false, undo: false, idx: i});
            return a;
        }
        for (var j = Math.max(0, history.length - room); j < history.length; j++)
            a.push({text: history[j].text, hist: true, undo: false, idx: j});
        return a;
    }
    property bool   decoderDead: false
    property string injected: ""
    property string activeKey: ""
    property int    warmupSecs: 0
    property bool   ibusActive: false
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

    function stateText() {
        if (decoderDead) return "decoder stopped — restart the app";
        if (!decoder.ready) return "loading decoder… (" + warmupSecs + " s)";
        if (lastShort) return "(too short — glide across more keys)";
        if (pending) return "decoding…";
        if (timedOut) return "decode stalled — glide again";
        if (candidates.length === 0) return "focus a text editor, then glide a word here";
        return `top-1 “${candidates[0].text}” (greedy “${greedyText}”, ${decMs.toFixed(0)} ms)`;
    }
    Timer { interval: 1000; repeat: true; running: !decoder.ready && win.visible; onTriggered: win.warmupSecs += 1 }
    Timer { interval: 800; repeat: true; running: win.visible; onTriggered: win.ibusActive = injector.ibusActive() }

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
        pushHistory(w, candidates, start);
    }

    // ================= recent-word history (spec §9.3) =================
    function pushHistory(text, cands, start) {
        var h = history.slice();
        h.push({text: text, cands: cands, start: start, len: text.length});
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
        if (injected.substr(e.start, e.len) !== e.text) { trimHistory(); return; }
        if (newText === e.text) return;
        const suffix = injected.substring(e.start + e.len);
        injector.backspace(e.len + suffix.length);
        injector.commitExact(newText + suffix);
        injected = injected.substring(0, e.start) + newText + suffix;
        const delta = newText.length - e.len;
        var h = history.slice();
        h[hi] = {text: newText, cands: e.cands, start: e.start, len: newText.length};
        for (var j = hi + 1; j < h.length; j++)
            h[j] = {text: h[j].text, cands: h[j].cands, start: h[j].start + delta, len: h[j].len};
        history = h;
        histOpen = -1;
        decoder.bumpWord(newText.toLowerCase());   // the user chose this — boost it
    }
    function deleteHistory(hi) {
        if (hi < 0 || hi >= history.length) return;
        const e = history[hi];
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
            h[j] = {text: h[j].text, cands: h[j].cands, start: h[j].start + delta, len: h[j].len};
        history = h;
        histOpen = -1;
        candidates = [];
    }
    function typeKey(c) {
        clearUndo();
        const ch = shifted(c);
        injector.typeChar(ch);
        injected += ch;
        consumeShift();
    }
    function tapSpace() { clearUndo(); injector.typeChar(" "); injected += " "; }
    function tapEnter() { injector.typeChar("\n"); injected += "\n"; }
    function tapPunct(p) {                       // collapse a preceding space, then "p "
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
        if (!injected.length) return;
        clearUndo();
        candidates = [];                  // editing invalidates the last glide's suggestions
        injector.backspace(1);
        injected = injected.substring(0, injected.length - 1);
        trimHistory();
    }
    function deleteWord() {                      // backspace-swipe: trailing spaces + one word
        if (!injected.length) return "";
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
        const hBefore = history;
        const s = deleteWord();
        if (!s.length) return;
        pendingUndoEntry = hBefore.length > history.length ? hBefore[hBefore.length - 1] : null;
        pendingUndo = s;
        undoTimer.restart();
    }
    function undoDelete() {
        if (!pendingUndo.length) return;
        const start = injected.length;          // deleteWord() returns "word" + spaces
        injector.commitExact(pendingUndo);
        injected += pendingUndo;
        if (pendingUndoEntry)
            pushHistory(pendingUndoEntry.text, pendingUndoEntry.cands, start);
        clearUndo();
    }
    function clearUndo() {
        pendingUndo = ""; pendingUndoEntry = null;
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
    // Hidden (spec §13.5): no surface at all, process resident, the global evdev
    // listener still watching. Only ever offered when that listener is actually
    // running — hiding with no way back would be the same trap as a one-click
    // quit, just slower to discover.
    function hideCompletely() {
        if (!toggleListener.available) return;
        menuOpen = false;
        persistGeometry();
        visible = false;
    }
    function unhide() {
        visible = true;
        raise();
    }
    function toggleVisibility() {
        if (!visible) unhide();
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
        settings.setValue("window/x", x);
        settings.setValue("window/y", y);
        settings.sync();
    }
    Timer { id: saveGeom; interval: 600; onTriggered: win.persistGeometry() }
    onWidthChanged:  if (!collapsed) saveGeom.restart()
    onHeightChanged: if (!collapsed) saveGeom.restart()
    onXChanged: saveGeom.restart()
    onYChanged: saveGeom.restart()

    Component.onCompleted: {
        const w = parseInt(settings.value("window/width", 560));
        const h = parseInt(settings.value("window/height", 280));
        if (w > 0 && h > 0) { win.width = Math.max(320, w); win.height = Math.max(170, h); }
        const sx = parseInt(settings.value("window/x", -1));
        const sy = parseInt(settings.value("window/y", -1));
        if (sx >= 0 && sy >= 0) { win.x = sx; win.y = sy; }
        win.expandedW = win.width; win.expandedH = win.height;
    }

    Timer {
        id: watchdog
        interval: 20000; repeat: false
        onTriggered: if (win.pending) { win.pending = false; win.timedOut = true; win.candidates = [] }
    }

    // ======================= COLLAPSED: the puck =======================
    // `×` collapses to this instead of quitting. It is draggable, always on top,
    // needs no permissions, and one click brings the keyboard back — the
    // guaranteed mouse-only way home (ADR-0005 §5). Quit lives in the ⋯ menu.
    Rectangle {
        visible: win.collapsed
        anchors.fill: parent
        color: pal.committedBg
        radius: Math.max(3, height * 0.18)
        Text {
            anchors.centerIn: parent
            text: "⌨  OpenGlide"
            color: pal.committedText
            font.pixelSize: Math.max(9, parent.height * 0.36)
        }
        Rectangle {   // IME indicator
            anchors.verticalCenter: parent.verticalCenter
            anchors.right: parent.right; anchors.rightMargin: parent.height * 0.28
            width: parent.height * 0.20; height: width; radius: width / 2
            color: win.ibusActive ? pal.accent : pal.muted
        }
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            cursorShape: Qt.PointingHandCursor
            property real px: 0
            property real py: 0
            property bool moved: false
            onPressed: function (mouse) {
                px = mouse.x; py = mouse.y; moved = false;
                if (mouse.button === Qt.RightButton) win.startSystemMove();
            }
            onPositionChanged: function (mouse) {
                if (!moved && Math.abs(mouse.x - px) + Math.abs(mouse.y - py) > 6) {
                    moved = true;
                    win.startSystemMove();
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

        Rectangle { anchors.fill: parent; color: pal.bg }

        // ---------------- chrome bar: drag + candidates + controls ----------------
        Rectangle {
            id: topbar
            x: 0; y: 0; width: parent.width; height: win.chromeH
            color: pal.candBar

            // Any chrome that is not a control is a drag handle.
            MouseArea { anchors.fill: parent; onPressed: win.startSystemMove() }

            Rectangle {   // IME status dot
                x: win.u * 0.10; width: win.u * 0.18; height: width; radius: width / 2
                anchors.verticalCenter: parent.verticalCenter
                color: win.ibusActive ? pal.accent : pal.muted
            }

            // FIXED slots — same word, same place, every glide. Candidates when
            // they are fresh, recent words otherwise (see chromeSlots).
            Repeater {
                model: 4
                Rectangle {
                    readonly property var slot: index < win.chromeSlots.length ? win.chromeSlots[index] : null
                    readonly property bool isHist: slot ? slot.hist : false
                    readonly property bool isUndo: slot ? slot.undo === true : false
                    readonly property bool isTop: slot && !slot.hist && !slot.undo && slot.idx === 0
                    x: win.u * (0.36 + index * 1.63); y: parent.height * 0.14
                    width: win.u * 1.57; height: parent.height * 0.72
                    radius: height / 2
                    visible: slot !== null
                    color: isTop ? pal.accent : (isHist || isUndo ? "transparent" : "#ffffff")
                    border.color: isUndo ? pal.accent : (isHist ? pal.muted : "#dadce0")
                    border.width: isTop ? 0 : (isUndo ? 2 : 1)
                    Text {
                        anchors.fill: parent; anchors.margins: parent.height * 0.18
                        text: parent.slot ? parent.slot.text : ""
                        horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                        font.bold: parent.isTop || parent.isUndo
                        font.pixelSize: Math.max(8, win.u * 0.26)
                        color: parent.isTop ? pal.accentText
                               : parent.isUndo ? pal.accent
                               : (parent.isHist ? pal.muted : pal.keyText)
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
                    color: ma.containsMouse ? "#ffffff" : "transparent"
                    Text {
                        anchors.centerIn: parent; text: modelData.g
                        font.pixelSize: Math.max(9, win.u * 0.30); color: pal.muted
                    }
                    MouseArea {
                        id: ma
                        anchors.fill: parent; hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            if (modelData.act === "smaller")       win.rescale(1 / 1.12);
                            else if (modelData.act === "bigger")   win.rescale(1.12);
                            else if (modelData.act === "collapse") win.collapse();
                            else                                   win.menuOpen = !win.menuOpen;
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
                    x: modelData.x * kb.width - win.u / 2 + win.kgap
                    y: modelData.y * kb.height - win.rowH / 2 + win.kgap
                    width:  win.u - 2 * win.kgap
                    height: win.rowH - 2 * win.kgap
                    radius: win.krad
                    color: modelData.l === win.activeKey ? pal.keyPop : pal.key
                    z: modelData.l === win.activeKey ? 1 : 0
                    transformOrigin: Item.Center
                    Behavior on scale { NumberAnimation { duration: 70; easing.type: Easing.OutCubic } }
                    scale: modelData.l === win.activeKey ? 1.14 : 1.0
                    Text {
                        anchors.centerIn: parent
                        text: win.shiftState > 0 ? modelData.l : modelData.c
                        font.bold: true
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
                    color: sym.pressed ? pal.keyPop : pal.key
                    Text {
                        anchors.centerIn: parent; text: modelData.l
                        font.bold: true
                        font.pixelSize: Math.max(9, Math.min(win.u, win.rowH) * 0.42)
                        color: pal.keyText
                    }
                    MouseArea {
                        id: sym
                        anchors.fill: parent; cursorShape: Qt.PointingHandCursor
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
                onSwipingChanged: {
                    if (swiping) {
                        pathFade.stop();
                        pathCanvas.opacity = 1;
                        pathCanvas.pts = [];
                        pathCanvas.requestPaint();
                    } else {
                        win.activeKey = "";
                        pathFade.restart();
                    }
                }
                onSwipeCompleted: function (points) {
                    if (points.length < 4) { win.lastShort = true; win.pending = false; win.candidates = []; return; }
                    win.lastShort = false;
                    win.candidates = []; win.pending = true; win.timedOut = false;
                    watchdog.restart();
                    if (decoder.ready) decoder.decode(points);
                }
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
                color: win.shiftState === 2 ? pal.accent : (win.shiftState === 1 ? pal.keyPop : pal.action)
                Text {
                    anchors.centerIn: parent
                    text: win.shiftState === 2 ? "⇪" : "⇧"
                    font.pixelSize: Math.max(10, Math.min(win.u, win.rowH) * 0.50)
                    color: win.shiftState === 2 ? pal.accentText : pal.actionText
                }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: win.cycleShift() }
            }

            Rectangle {
                id: backspaceKey
                x: 8.5 * win.u + win.kgap; y: 2 * win.rowH + win.kgap
                width: 1.5 * win.u - 2 * win.kgap; height: win.rowH - 2 * win.kgap
                radius: win.krad
                color: bs.pressed ? pal.actionHold : pal.action
                Text {
                    anchors.centerIn: parent; text: "⌫"
                    font.pixelSize: Math.max(10, Math.min(win.u, win.rowH) * 0.50); color: pal.actionText
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
                    color: modelData.act === "layer" && win.layer === "sym" ? pal.accent : pal.action
                    Text {
                        anchors.centerIn: parent
                        text: modelData.act === "layer" && win.layer === "sym" ? "ABC" : modelData.g
                        font.pixelSize: Math.max(8, win.actionH * (modelData.act === "space" || modelData.act === "layer" ? 0.34 : 0.48))
                        color: modelData.act === "layer" && win.layer === "sym" ? pal.accentText : pal.actionText
                    }
                    MouseArea {
                        anchors.fill: parent; cursorShape: Qt.PointingHandCursor
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
            color: pal.committedBg; border.color: pal.accent; border.width: 2; z: 5
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
                        color: index < bs.wordsDeleted ? pal.accent : "#6b7178"
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
            color: pal.committedBg; opacity: 0.94; z: 40
            Text {
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left; anchors.leftMargin: win.u * 0.15
                anchors.right: parent.right; anchors.rightMargin: win.u * 0.15
                elide: Text.ElideRight
                text: win.stateText() + "   ·   aspect " + win.letterAspect.toFixed(2)
                      + "   ·   " + decoder.layoutId
                      + "   ·   toggle: " + toggleListener.status
                      + "   ·   ⌨ " + (win.injected.length ? win.injected.replace(/\s+$/, "") : "—")
                color: pal.committedText
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
            color: "#ffffff"; radius: Math.max(3, win.u * 0.12)
            border.color: "#dadce0"; border.width: 1
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
                        color: hm.containsMouse ? pal.candBar : "#ffffff"
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            x: win.u * 0.18; width: parent.width - x * 2
                            elide: Text.ElideRight
                            text: parent.isDelete ? "Delete" : histPopup.alts[index]
                            font.pixelSize: Math.max(8, win.u * 0.22)
                            color: parent.isDelete ? "#c5221f" : pal.keyText
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
            visible: win.menuOpen; z: 61
            x: Math.min(parent.width - width - win.u * 0.1, win.u * 6.2)
            y: win.chromeH
            width: win.u * 3.7; height: menuCol.height + win.u * 0.2
            color: "#ffffff"; radius: Math.max(3, win.u * 0.12)
            border.color: "#dadce0"; border.width: 1
            Column {
                id: menuCol
                x: 0; y: win.u * 0.1; width: parent.width
                Repeater {
                    model: [
                        {g: "Small  · 440×220",  act: "s"},
                        {g: "Medium · 560×280",  act: "m"},
                        {g: "Large  · 720×360",  act: "l"},
                        {g: "Hide completely",   act: "hide"},
                        {g: "Diagnostics",       act: "diag"},
                        {g: "Quit OpenGlide",    act: "quit"}
                    ]
                    Rectangle {
                        readonly property bool disabled: modelData.act === "hide" && !toggleListener.available
                        width: parent.width; height: win.u * 0.62
                        color: mi.containsMouse && !disabled ? pal.candBar : "#ffffff"
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            x: win.u * 0.22
                            width: parent.width - x * 2
                            elide: Text.ElideRight
                            text: modelData.act === "diag" && win.showDiagnostics ? "✓ " + modelData.g
                                  : modelData.act === "hide" ? (toggleListener.available
                                        ? "Hide · " + win.toggleGesture() + " to return"
                                        : "Hide — needs /dev/input access")
                                  : modelData.g
                            font.pixelSize: Math.max(8, win.u * 0.21)
                            color: parent.disabled ? "#9aa0a6"
                                   : modelData.act === "quit" ? "#c5221f" : pal.keyText
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
                                else if (modelData.act === "hide") win.hideCompletely();
                                else if (modelData.act === "diag") { win.showDiagnostics = !win.showDiagnostics; win.menuOpen = false; }
                                else { win.persistGeometry(); Qt.quit(); }
                            }
                        }
                    }
                }
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
            onPressed: win.startSystemResize(Qt.TopEdge)
        }
        MouseArea {   // bottom
            x: 0; y: parent.height - win.frame; width: parent.width; height: win.frame
            cursorShape: Qt.SizeVerCursor
            onPressed: win.startSystemResize(Qt.BottomEdge)
        }
        MouseArea {   // left, chrome band only
            x: 0; y: win.frame; width: win.frame; height: win.chromeH
            cursorShape: Qt.SizeHorCursor
            onPressed: win.startSystemResize(Qt.LeftEdge)
        }
        MouseArea {   // left, action band only
            x: 0; y: parent.height - win.frame - win.actionH; width: win.frame; height: win.actionH
            cursorShape: Qt.SizeHorCursor
            onPressed: win.startSystemResize(Qt.LeftEdge)
        }
        MouseArea {   // right, chrome band only
            x: parent.width - win.frame; y: win.frame; width: win.frame; height: win.chromeH
            cursorShape: Qt.SizeHorCursor
            onPressed: win.startSystemResize(Qt.RightEdge)
        }
        MouseArea {   // right, action band only
            x: parent.width - win.frame; y: parent.height - win.frame - win.actionH
            width: win.frame; height: win.actionH
            cursorShape: Qt.SizeHorCursor
            onPressed: win.startSystemResize(Qt.RightEdge)
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
                onPressed: win.startSystemResize(modelData.e)
            }
        }

        // visible grip, bottom-right
        Canvas {
            width: Math.max(14, win.frame * 2.6); height: width
            x: parent.width - width; y: parent.height - height
            onPaint: {
                const ctx = getContext("2d"); ctx.reset();
                ctx.strokeStyle = pal.muted; ctx.lineWidth = 1;
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
        function onCandidatesReady(greedy, candidates, ms) {
            watchdog.stop();
            win.greedyText = greedy; win.candidates = candidates; win.decMs = ms;
            win.pending = false; win.timedOut = false;
            if (candidates.length > 0) win.commitDecoded(candidates[0].text);
        }
        function onDecoderDied() {
            watchdog.stop();
            win.decoderDead = true; win.pending = false; win.candidates = [];
        }
    }
}
