import QtQuick
import QtQuick.Window
import OpenGlide 1.0

Window {
    id: win
    width: 980; height: 560; visible: true
    flags: Qt.WindowStaysOnTopHint | Qt.WindowDoesNotAcceptFocus   // S17: don't steal keyboard focus
    title: "OpenGlide — Phase 1 prototype (Qt6)"
    color: "#f5f5f5"

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
    property string committed: ""
    property string lastWord: ""
    property bool timedOut: false
    property bool decoderDead: false

    // PURE binding target — do NOT assign status.text imperatively anywhere
    function stateText() {
        if (decoderDead) return "decoder stopped — restart the app";
        if (!decoder.ready) return "decoder warming up…";
        if (lastShort) return "(too short — glide across more keys)";
        if (pending) return "decoding…";
        if (timedOut) return "decode timed out — glide again";
        if (candidates.length === 0) return "focus a text editor, then glide a word here";
        return `top-1 “${candidates[0].text}” committed   (greedy “${greedyText}”, ${decMs.toFixed(0)} ms)`;
    }

    // One-click correction: the top candidate is auto-committed; clicking any other
    // candidate backspaces over the last word (+ its trailing space) and injects it.
    function correct(i) {
        if (i < 0 || i >= candidates.length) return;
        const nw = candidates[i].text;
        if (nw === lastWord) return;                       // already what's committed
        if (lastWord.length > 0) injector.backspace(lastWord.length + 1);
        injector.commit(nw);                               // types nw + trailing space
        committed = committed.slice(0, committed.length - lastWord.length) + nw;
        lastWord = nw;
    }

    Rectangle {
        id: topbar
        anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
        height: 64; color: "#eaeaea"
        Row {
            anchors.centerIn: parent; spacing: 8
            Repeater {
                model: win.candidates
                Rectangle {
                    width: 150; height: 44; radius: 6
                    color: index === 0 ? "#1a73e8" : "#ffffff"
                    border.color: "#cccccc"; border.width: 1
                    Text {
                        anchors.centerIn: parent
                        text: modelData.text; font.bold: index === 0; font.pixelSize: 18
                        color: index === 0 ? "#ffffff" : "#333"
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: win.correct(index)
                    }
                }
            }
        }
    }

    Text {
        id: status
        anchors.top: topbar.bottom; anchors.horizontalCenter: parent.horizontalCenter; anchors.topMargin: 8
        text: win.stateText()   // pure binding — updates automatically on every state-var change
        font.pixelSize: 15; color: "#444"
    }
    Timer {
        id: watchdog
        interval: 20000; repeat: false   // stuck-but-alive backstop only; real crashes are caught by decoderDied
        onTriggered: if (win.pending) { win.pending = false; win.timedOut = true; win.candidates = [] }
    }

    Item {
        id: kb
        anchors.fill: parent
        anchors.topMargin: 110; anchors.bottomMargin: 64; anchors.leftMargin: 20; anchors.rightMargin: 20

        Repeater {
            model: win.keys
            Rectangle {
                x: modelData.x * kb.width - 32; y: modelData.y * kb.height - 32
                width: 64; height: 64; radius: 8; color: "#ffffff"; border.color: "#cccccc"; border.width: 1
                Text { anchors.centerIn: parent; text: modelData.l; font.bold: true; font.pixelSize: 22; color: "#333" }
            }
        }

        Canvas {
            id: pathCanvas
            anchors.fill: parent
            property var pts: []
            onPaint: {
                const ctx = getContext("2d"); ctx.reset();
                if (pts.length < 2) return;
                ctx.strokeStyle = "#1a73e8"; ctx.lineWidth = 3; ctx.beginPath();
                ctx.moveTo(pts[0].x * width, pts[0].y * height);
                for (let i = 1; i < pts.length; i++) ctx.lineTo(pts[i].x * width, pts[i].y * height);
                ctx.stroke();
            }
        }

        SwipeSurface {
            id: surface
            anchors.fill: parent
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

    Rectangle {
        id: committedBar
        anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.right: parent.right
        height: 44; color: "#222"
        Text {
            anchors.verticalCenter: parent.verticalCenter; anchors.left: parent.left; anchors.leftMargin: 12
            text: "committed: " + (win.committed.length ? win.committed : "—")
            color: "#eee"; font.pixelSize: 15; font.family: "monospace"
        }
    }

    Connections {
        target: decoder
        function onCandidatesReady(greedy, candidates, ms) {
            watchdog.stop();
            win.greedyText = greedy; win.candidates = candidates; win.decMs = ms; win.pending = false; win.timedOut = false;
            if (candidates.length > 0) {
                const word = candidates[0].text;
                injector.commit(word);
                win.lastWord = word;
                win.committed = (win.committed.length ? win.committed + " " : "") + word;
            }
        }
        function onDecoderDied() {
            watchdog.stop();
            win.decoderDead = true; win.pending = false; win.candidates = [];
        }
        // no onReadyChanged: the status binding reads decoder.ready directly
    }
}
