import QtQuick
import QtQuick.Window
import OpenGlide 1.0

Window {
    id: win
    width: 980; height: 560; visible: true
    flags: Qt.WindowStaysOnTopHint | Qt.WindowDoesNotAcceptFocus   // §17: don't steal keyboard focus
    title: "OpenGlide — Phase 1 prototype (Qt6)"
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
    property bool decoderDead: false
    property string injected: ""
    property string activeKey: ""        // key under the cursor during a glide (key-pop)
    property int warmupSecs: 0

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
        injector.backspace(1);
        injected = injected.substring(0, injected.length - 1);
    }
    function deleteWord() {                      // backspace-swipe: trailing spaces + one word
        if (!injected.length) return "";
        var i = injected.length, n = 0;
        while (i > 0 && injected[i - 1] === " ") { i--; n++; }
        while (i > 0 && injected[i - 1] !== " ") { i--; n++; }
        const deleted = injected.substring(i);
        injector.backspace(n);
        injected = injected.substring(0, i);
        return deleted;                          // staged so swipe-right can undo it
    }
    function undoWord(s) {                        // swipe-right: re-inject a staged deletion verbatim
        for (var k = 0; k < s.length; k++) injector.typeChar(s[k]);
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

    // ---- candidate strip ----
    Rectangle {
        id: topbar
        anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
        height: 64; color: pal.candBar
        Row {
            anchors.centerIn: parent; spacing: 8
            Repeater {
                model: win.candidates
                Rectangle {
                    width: 158; height: 44; radius: 22
                    color: index === 0 ? pal.accent : "#ffffff"
                    border.color: "#dadce0"; border.width: index === 0 ? 0 : 1
                    Text {
                        anchors.centerIn: parent
                        text: modelData.text; font.bold: index === 0; font.pixelSize: 18
                        color: index === 0 ? pal.accentText : pal.keyText
                    }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: win.correct(index) }
                }
            }
        }
    }

    Text {
        id: status
        anchors.top: topbar.bottom; anchors.horizontalCenter: parent.horizontalCenter; anchors.topMargin: 8
        text: win.stateText()
        font.pixelSize: 15; color: pal.muted
    }

    Timer {
        id: watchdog
        interval: 20000; repeat: false
        onTriggered: if (win.pending) { win.pending = false; win.timedOut = true; win.candidates = [] }
    }

    // ---- keyboard / glide surface ----
    Item {
        id: kb
        anchors.top: status.bottom; anchors.topMargin: 8
        anchors.bottom: actionRow.top; anchors.bottomMargin: 8
        anchors.left: parent.left; anchors.right: parent.right
        anchors.leftMargin: 20; anchors.rightMargin: 20

        Repeater {
            model: win.keys
            Rectangle {
                x: modelData.x * kb.width - 32; y: modelData.y * kb.height - 32
                width: 64; height: 64; radius: 16
                color: modelData.l === win.activeKey ? pal.keyPop : pal.key
                z: modelData.l === win.activeKey ? 1 : 0
                transformOrigin: Item.Center
                Behavior on scale { NumberAnimation { duration: 70; easing.type: Easing.OutCubic } }
                scale: modelData.l === win.activeKey ? 1.18 : 1.0
                Text { anchors.centerIn: parent; text: modelData.l; font.bold: true; font.pixelSize: 22; color: pal.keyText }
            }
        }

        Canvas {
            id: pathCanvas
            anchors.fill: parent
            property var pts: []
            onPaint: {
                const ctx = getContext("2d"); ctx.reset();
                if (pts.length < 2) return;
                ctx.strokeStyle = pal.accent; ctx.lineWidth = 6; ctx.lineCap = "round"; ctx.lineJoin = "round";
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
        }
    }

    // ---- action row: comma · space · period · backspace(FUTO gesture) ----
    Row {
        id: actionRow
        anchors.bottom: committedBar.top; anchors.bottomMargin: 8
        anchors.left: parent.left; anchors.right: parent.right; anchors.leftMargin: 20; anchors.rightMargin: 20
        height: 54; spacing: 8

        Rectangle {   // comma
            width: 92; height: 54; radius: 16; color: pal.action
            Text { anchors.centerIn: parent; text: ","; font.pixelSize: 24; color: pal.actionText }
            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: win.tapPunct(",") }
        }
        Rectangle {   // space (fills the middle)
            height: 54; radius: 16; color: pal.action
            width: actionRow.width - 92 - 92 - 132 - 24
            Text { anchors.centerIn: parent; text: "space"; font.pixelSize: 18; color: pal.actionText }
            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: win.tapSpace() }
        }
        Rectangle {   // period
            width: 92; height: 54; radius: 16; color: pal.action
            Text { anchors.centerIn: parent; text: "."; font.pixelSize: 24; color: pal.actionText }
            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: win.tapPunct(".") }
        }
        Rectangle {   // backspace — tap=char, hold=rapid char, hold+swipe-left=word delete (swipe-right=undo)
            width: 132; height: 54; radius: 16
            color: bs.pressed ? pal.actionHold : pal.action
            Text { anchors.centerIn: parent; text: "⌫"; font.pixelSize: 26; color: pal.actionText }
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

    Rectangle {
        id: committedBar
        anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.right: parent.right
        height: 44; color: pal.committedBg
        Text {
            anchors.verticalCenter: parent.verticalCenter; anchors.left: parent.left; anchors.leftMargin: 12
            text: "committed: " + (win.injected.length ? win.injected.replace(/\s+$/,"") : "—")
            color: pal.committedText; font.pixelSize: 15; font.family: "monospace"
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
