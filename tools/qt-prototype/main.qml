import QtQuick
import QtQuick.Window
import OpenGlide 1.0

Window {
    id: win
    width: 900; height: 360; visible: true
    flags: Qt.WindowStaysOnTopHint | Qt.WindowDoesNotAcceptFocus | Qt.FramelessWindowHint
    color: pal.bg

    // ---- FUTO-inspired palette ----
    readonly property QtObject pal: QtObject {
        readonly property color bg: "#e8eaed"          // keyboard background (soft gray)
        readonly property color key: "#ffffff"         // letter keys
        readonly property color keyPop: "#d3e3fd"      // key under the cursor
        readonly property color keyText: "#202124"
        readonly property color action: "#bdc1c6"      // space / punct / backspace
        readonly property color actionHold: "#9aa0a6"  // backspace while held
        readonly property color actionText: "#3c4043"
        readonly property color accent: "#1a73e8"      // FUTO-ish blue
        readonly property color accentText: "#ffffff"
        readonly property color candBar: "#f1f3f4"
        readonly property color muted: "#5f6368"
        readonly property color committedBg: "#202124"
        readonly property color committedText: "#e8eaed"
    }

    property int wordSwipePx: 60      // leftward px per word deleted (≈ one key width)

    // Keys scale with the window (min of width/height constraints) so they never
    // squish or overlap when resized. keySize = one cell; the key visual is smaller.
    property real keySize: Math.max(30, Math.min((width - 24) / 10, (height - 48) / 5.8))
    property real keyVis: keySize * 0.88
    property real keyRadius: keySize * 0.20

    property var keys: [
        {l:"Q",x:0.05,y:0.167},{l:"W",x:0.15,y:0.167},{l:"E",x:0.25,y:0.167},{l:"R",x:0.35,y:0.167},{l:"T",x:0.45,y:0.167},
        {l:"Y",x:0.55,y:0.167},{l:"U",x:0.65,y:0.167},{l:"I",x:0.75,y:0.167},{l:"O",x:0.85,y:0.167},{l:"P",x:0.95,y:0.167},
        {l:"A",x:0.10,y:0.500},{l:"S",x:0.20,y:0.500},{l:"D",x:0.30,y:0.500},{l:"F",x:0.40,y:0.500},{l:"G",x:0.50,y:0.500},
        {l:"H",x:0.60,y:0.500},{l:"J",x:0.70,y:0.500},{l:"K",x:0.80,y:0.500},{l:"L",x:0.90,y:0.500},
        {l:"Z",x:0.20,y:0.833},{l:"X",x:0.30,y:0.833},{l:"C",x:0.40,y:0.833},{l:"V",x:0.50,y:0.833},{l:"B",x:0.60,y:0.833},
        {l:"N",x:0.70,y:0.833},{l:"M",x:0.80,y:0.833}
    ]

    // ---- state ----
    property var candidates: []
    property string greedyText: ""
    property real decMs: 0.0
    property bool pending: false
    property bool lastShort: false
    property string lastWord: ""
    property bool timedOut: false
    // words in the committed text available to delete (drives the swipe-delete gauge)
    readonly property int availWords: injected.length ? injected.replace(/^\s+|\s+$/g, "").split(/\s+/).filter(function(w){return w.length;}).length : 0
    property bool decoderDead: false
    property string injected: ""
    property string activeKey: ""        // key under the cursor during a glide (key-pop)
    property int warmupSecs: 0
    property bool ibusActive: false

    function stateText() {
        if (decoderDead) return "decoder stopped — restart the app";
        if (!decoder.ready) return "loading decoder… (" + warmupSecs + " s)";
        if (lastShort) return "(too short — glide across more keys)";
        if (pending) return "decoding…";
        if (timedOut) return "decode stalled — glide again";
        if (candidates.length === 0) return "focus a text editor, then glide a word here";
        return `top-1 “${candidates[0].text}” committed   (greedy “${greedyText}”, ${decMs.toFixed(0)} ms)`;
    }
    Timer { interval: 1000; repeat: true; running: !decoder.ready && visible; onTriggered: warmupSecs += 1 }
    Timer { interval: 800; repeat: true; running: visible; onTriggered: ibusActive = injector.ibusActive() }

    // ---- injection ops (each keeps `injected` in sync with the target field) ----
    function commitDecoded(word) { injector.commit(word); injected += word + " "; lastWord = word; }
    function tapSpace()          { injector.typeChar(" "); injected += " "; }
    function tapPunct(p) {                       // collapse a preceding space, then "p "
        if (injected.length && injected[injected.length - 1] === " ") {
            injector.backspace(1);
            injected = injected.substring(0, injected.length - 1);
        }
        injector.typeChar(p); injector.typeChar(" ");
        injected += p + " ";
    }
    function deleteChar() {
        if (!injected.length) return;
        candidates = []; lastWord = "";   // editing invalidates the last glide's suggestions
        injector.backspace(1);
        injected = injected.substring(0, injected.length - 1);
    }
    function deleteWord() {                      // backspace-swipe: trailing spaces + one word
        if (!injected.length) return "";
        candidates = []; lastWord = "";
        var i = injected.length, n = 0;
        while (i > 0 && injected[i - 1] === " ") { i--; n++; }
        while (i > 0 && injected[i - 1] !== " ") { i--; n++; }
        const deleted = injected.substring(i);
        injector.backspace(n);
        injected = injected.substring(0, i);
        return deleted;                          // staged so swipe-right can undo it
    }
    function undoWord(s) {                        // swipe-right: re-inject a staged deletion verbatim
        candidates = []; lastWord = "";
        injector.commitExact(s);   // one IBus commit of the exact deleted string (per-char typeChar was slow/racey mid-gesture)
        injected += s;
    }
    function correct(i) {
        if (i < 0 || i >= candidates.length) return;
        const nw = candidates[i].text;
        if (nw === lastWord) return;
        if (lastWord.length > 0) {
            injector.backspace(lastWord.length + 1);
            injected = injected.substring(0, injected.length - (lastWord.length + 1));
        }
        injector.commit(nw);
        injected += nw + " ";
        lastWord = nw;
    }
    function nearestKey(nx, ny) {                 // for key-pop: closest key to the cursor, in pixels
        var best = "", bestD = 1e9, W = kb.width, H = kb.height;
        for (var i = 0; i < keys.length; i++) {
            var k = keys[i], dx = (k.x - nx) * W, dy = (k.y - ny) * H, d = dx*dx + dy*dy;
            if (d < bestD) { bestD = d; best = k.l; }
        }
        return best;
    }

    // ---- top bar: drag handle (frameless move) + candidates + IME status + close ----
    Rectangle {
        id: topbar
        anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
        height: keyVis + keySize * 0.30; color: pal.candBar
        MouseArea { anchors.fill: parent; onPressed: function(mouse) { win.startSystemMove() } }  // drag empty space to move
        Text {   // IME status (left)
            anchors.verticalCenter: parent.verticalCenter; anchors.left: parent.left; anchors.leftMargin: 10
            text: win.ibusActive ? "OpenGlide IME ✓" : "IME: other"
            font.pixelSize: keySize * 0.24; color: win.ibusActive ? pal.accent : pal.muted
        }
        Row {   // candidates (center)
            anchors.centerIn: parent; spacing: keySize * 0.10
            Repeater {
                model: win.candidates
                Rectangle {
                    width: keySize * 2.4; height: keyVis * 0.80; radius: height / 2
                    color: index === 0 ? pal.accent : "#ffffff"
                    border.color: "#dadce0"; border.width: index === 0 ? 0 : 1
                    Text {
                        anchors.centerIn: parent
                        text: modelData.text; font.bold: index === 0; font.pixelSize: keySize * 0.26
                        color: index === 0 ? pal.accentText : pal.keyText
                    }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: win.correct(index) }
                }
            }
        }
        Rectangle {   // close (right)
            anchors.verticalCenter: parent.verticalCenter; anchors.right: parent.right; anchors.rightMargin: 8
            width: keyVis * 0.7; height: keyVis * 0.7; radius: height / 2; color: "transparent"
            Text { anchors.centerIn: parent; text: "×"; font.pixelSize: keySize * 0.40; color: pal.muted }
            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: Qt.quit() }
        }
    }

    Text {
        id: status
        anchors.top: topbar.bottom; anchors.horizontalCenter: parent.horizontalCenter; anchors.topMargin: 2
        text: win.stateText()
        font.pixelSize: keySize * 0.22; color: pal.muted
    }

    Timer {
        id: watchdog
        interval: 20000; repeat: false
        onTriggered: if (win.pending) { win.pending = false; win.timedOut = true; win.candidates = [] }
    }

    // ---- keyboard / glide surface (keys scale with the window) ----
    Item {
        id: kb
        anchors.top: status.bottom; anchors.topMargin: keySize * 0.06
        anchors.horizontalCenter: parent.horizontalCenter
        width: 10 * keySize; height: 3 * keySize

        Repeater {
            model: win.keys
            Rectangle {
                x: modelData.x * kb.width - keyVis / 2; y: modelData.y * kb.height - keyVis / 2
                width: keyVis; height: keyVis; radius: keyRadius
                color: modelData.l === win.activeKey ? pal.keyPop : pal.key
                z: modelData.l === win.activeKey ? 1 : 0
                transformOrigin: Item.Center
                Behavior on scale { NumberAnimation { duration: 70; easing.type: Easing.OutCubic } }
                scale: modelData.l === win.activeKey ? 1.18 : 1.0
                Text { anchors.centerIn: parent; text: modelData.l; font.bold: true; font.pixelSize: keyVis * 0.44; color: pal.keyText }
            }
        }

        Canvas {
            id: pathCanvas
            anchors.fill: parent
            property var pts: []
            onPaint: {
                const ctx = getContext("2d"); ctx.reset();
                if (pts.length < 2) return;
                ctx.strokeStyle = pal.accent; ctx.lineWidth = keyVis * 0.12; ctx.lineCap = "round"; ctx.lineJoin = "round";
                ctx.beginPath();
                ctx.moveTo(pts[0].x * width, pts[0].y * height);
                for (let i = 1; i < pts.length; i++) ctx.lineTo(pts[i].x * width, pts[i].y * height);
                ctx.stroke();
            }
        }

        SwipeSurface {
            id: surface
            anchors.fill: parent
            onCursorMoved: function(nx, ny) { if (swiping) win.activeKey = win.nearestKey(nx, ny) }
            onSwipingChanged: if (!swiping) win.activeKey = ""
            onSwipeCompleted: function(points) {
                if (points.length < 4) { win.lastShort = true; win.pending = false; win.candidates = []; return; }
                win.lastShort = false;
                pathCanvas.pts = points; pathCanvas.requestPaint();
                win.candidates = []; win.pending = true; win.timedOut = false;
                watchdog.restart();
                if (decoder.ready) decoder.decode(points);
            }
            onTapped: function(nx, ny) {
                var uk = win.nearestKey(nx, ny);
                if (!uk) return;
                injector.typeChar(uk.toLowerCase());
                win.injected += uk.toLowerCase();
                win.activeKey = uk;            // brief key-pop flash
                tapFlash.restart();
            }
        }
        Timer { id: tapFlash; interval: 120; onTriggered: if (!surface.swiping) win.activeKey = "" }
    }

    // ---- action row: comma · space · period · backspace(FUTO gesture) — integrated, scaling ----
    Row {
        id: actionRow
        anchors.top: kb.bottom; anchors.topMargin: keySize * 0.06
        anchors.horizontalCenter: parent.horizontalCenter
        height: keyVis; spacing: keySize * 0.12

        Rectangle {   // comma
            width: keySize * 1.5; height: keyVis; radius: keyRadius; color: pal.action
            Text { anchors.centerIn: parent; text: ","; font.pixelSize: keyVis * 0.46; color: pal.actionText }
            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: win.tapPunct(",") }
        }
        Rectangle {   // space (fills the middle)
            height: keyVis; radius: keyRadius; color: pal.action
            width: keySize * 5.0
            Text { anchors.centerIn: parent; text: "space"; font.pixelSize: keyVis * 0.34; color: pal.actionText }
            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: win.tapSpace() }
        }
        Rectangle {   // period
            width: keySize * 1.5; height: keyVis; radius: keyRadius; color: pal.action
            Text { anchors.centerIn: parent; text: "."; font.pixelSize: keyVis * 0.46; color: pal.actionText }
            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: win.tapPunct(".") }
        }
        Rectangle {   // backspace — tap=char, hold=rapid char, hold+swipe-left=word delete (swipe-right=undo)
            width: keySize * 2.0; height: keyVis; radius: keyRadius
            color: bs.pressed ? pal.actionHold : pal.action
            Text { anchors.centerIn: parent; text: "⌫"; font.pixelSize: keyVis * 0.50; color: pal.actionText }
            MouseArea {
                id: bs
                anchors.fill: parent
                property real startX
                property int wordsDeleted
                property bool swiping
                property bool acted
                property var deletedStack
                onPressed: function(mouse) {
                    startX = mouse.x; wordsDeleted = 0; swiping = false; acted = false; deletedStack = [];
                    holdDelay.start();
                }
                onPositionChanged: function(mouse) {
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
                    if (!acted) win.deleteChar();
                }
            }
            Timer { id: holdDelay;  interval: 420; onTriggered: if (!bs.swiping) charRepeat.start() }
            Timer { id: charRepeat; interval: 70; repeat: true; onTriggered: { win.deleteChar(); bs.acted = true } }
        }
    }

    // swipe-delete gauge: one dot per deletable word; fills right-to-left as you
    // pull left, so you can see exactly how far each word takes (no more "feel it").
    Rectangle {
        id: deleteGauge
        visible: bs.swiping && win.availWords > 0
        anchors.bottom: actionRow.top; anchors.bottomMargin: 6
        anchors.right: parent.right; anchors.rightMargin: keySize * 0.5
        width: Math.min(parent.width - keySize, keySize * 0.6 + win.availWords * keySize * 0.34)
        height: keyVis * 0.8; radius: keyRadius
        color: pal.committedBg; border.color: pal.accent; border.width: 2; z: 5
        Text {
            anchors.verticalCenter: parent.verticalCenter; anchors.left: parent.left; anchors.leftMargin: keySize * 0.2
            text: "−" + bs.wordsDeleted; color: pal.accentText; font.pixelSize: keySize * 0.26; font.bold: true
        }
        Row {
            anchors.verticalCenter: parent.verticalCenter; anchors.right: parent.right; anchors.rightMargin: keySize * 0.2
            layoutDirection: Qt.RightToLeft; spacing: keySize * 0.12
            Repeater {
                model: win.availWords
                Rectangle {
                    width: keySize * 0.22; height: keySize * 0.22; radius: width / 2
                    color: index < bs.wordsDeleted ? pal.accent : "#6b7178"
                }
            }
        }
    }

    Rectangle {
        id: committedBar
        anchors.top: actionRow.bottom; anchors.topMargin: keySize * 0.06
        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
        color: pal.committedBg
        Text {
            anchors.verticalCenter: parent.verticalCenter; anchors.left: parent.left; anchors.leftMargin: 12
            text: "committed: " + (win.injected.length ? win.injected.replace(/\s+$/,"") : "—")
            color: pal.committedText; font.pixelSize: keySize * 0.24; font.family: "monospace"
        }
    }

    Connections {
        target: decoder
        function onCandidatesReady(greedy, candidates, ms) {
            watchdog.stop();
            win.greedyText = greedy; win.candidates = candidates; win.decMs = ms; win.pending = false; win.timedOut = false;
            if (candidates.length > 0) win.commitDecoded(candidates[0].text);
        }
        function onDecoderDied() {
            watchdog.stop();
            win.decoderDead = true; win.pending = false; win.candidates = [];
        }
    }
}
