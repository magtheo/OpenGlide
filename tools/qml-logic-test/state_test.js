// Unit-tests main.qml's VISIBLE-state machine without Qt, a display, or the
// decoder — the same trick as history_test.js: the real functions are extracted
// from main.qml verbatim, so a copy cannot drift out of sync with the app.
//
// Two rules are under test, and both were broken before this suite existed:
//
//   1. Every exit from a finished glide leaves a state that RESOLVES. A glide
//      the decoder refuses (not ready, or busy per ADR-0003's stale-discard)
//      emits no candidatesReady, so anything that sets `pending` on those paths
//      hangs the UI until the 20 s watchdog — and the watchdog's message used to
//      render only in the opt-in diagnostics line.
//   2. The note's precedence order. A dead decoder outranks a stalled decode
//      outranks a short stroke; "decoding…" asks for patience, everything else
//      asks for another glide (`problem`), and the two must not be confused.
const fs = require('fs');
const extracted = fs.readFileSync(process.argv[2], 'utf8');

function makeWin(opts = {}) {
  const calls = { noteTimer: 0, watchdog: 0, decode: 0 };
  const state = {
    // decode state
    candidates: [], pending: false, lastShort: false, timedOut: false, dropped: false,
    decoderDead: false, warmupSecs: 0,
    // things noteShort/glideFinished touch but this suite doesn't exercise
    injected: '', history: [], histOpen: -1, histMax: 12, shiftState: 0,
    pendingUndo: '', pendingUndoEntry: null, undoGen: -1, targetGen: 0, staleMs: 300000,
    undoTimer: { restart(){}, stop(){} },
    noteTimer: { restart(){ calls.noteTimer++; }, stop(){} },
    watchdog:  { restart(){ calls.watchdog++;  }, stop(){} },
  };
  const decoder = {
    ready: opts.ready !== false,
    busy: opts.busy === true,
    bumpWord: () => {},
    // Mirrors DecoderBridge::decode — FALSE means refused, nothing will be emitted.
    decode(pts) { calls.decode++; return this.ready && !this.busy; },
  };
  const injector = { backspace(){}, commit(){}, commitExact(){}, typeChar(){} };
  const build = new Function('state', 'injector', 'decoder', `
    with (state) {
      ${extracted}
      return { noteState, noteShort, glideFinished };
    }`);
  return { state, decoder, calls, fn: build(state, injector, decoder) };
}

let pass = 0, fail = 0;
function check(name, cond, extra = '') {
  cond ? pass++ : fail++;
  console.log(`  ${cond ? 'ok  ' : 'FAIL'} ${name}${extra && !cond ? '  <- ' + extra : ''}`);
}
// The invariant behind rule 1, asserted after every outcome.
function checkResolves(w, label) {
  const note = w.fn.noteState(w.state.decoderDead, w.decoder.ready, w.state.dropped,
                              w.state.pending, w.state.lastShort, w.state.timedOut,
                              w.state.warmupSecs);
  check(`${label}: not silently pending`, !(w.state.pending && !w.decoder.ready));
  check(`${label}: the user is told something`, note.text.length > 0,
        `pending=${w.state.pending} note=${JSON.stringify(note.text)}`);
}

const glide = n => new Array(n).fill({x: 0.5, y: 0.5, t: 0});

console.log('state: glide outcomes resolve, note precedence\n');

{ // --- a normal glide hands off and waits for the decoder
  const w = makeWin();
  const out = w.fn.glideFinished(glide(40));
  check('full stroke -> "decoding"', out === 'decoding');
  check('  decoder was actually called', w.calls.decode === 1);
  check('  pending is set', w.state.pending === true);
  check('  the watchdog is armed', w.calls.watchdog === 1);
  checkResolves(w, 'while decoding');
}

{ // --- REGRESSION: a glide refused by stale-discard must not hang
  const w = makeWin({busy: true});
  const out = w.fn.glideFinished(glide(40));
  check('busy decoder -> "dropped"', out === 'dropped');
  check('  pending is NOT left set', w.state.pending === false);
  check('  dropped is flagged', w.state.dropped === true);
  check('  the expiry timer is armed', w.calls.noteTimer === 1);
  check('  the watchdog is NOT armed', w.calls.watchdog === 0);
  checkResolves(w, 'after a dropped glide');
  const note = w.fn.noteState(false, true, true, false, false, false, 0);
  check('  and it reads "busy — glide again"', note.text === 'busy — glide again');
  check('  flagged as a problem, not progress', note.problem === true);
}

{ // --- REGRESSION: a glide before the decoder is ready must not hang
  const w = makeWin({ready: false});
  const out = w.fn.glideFinished(glide(40));
  check('decoder not ready -> "notready"', out === 'notready');
  check('  pending is NOT left set', w.state.pending === false);
  check('  the decoder was not called', w.calls.decode === 0);
  check('  the watchdog is NOT armed', w.calls.watchdog === 0);
  checkResolves(w, 'before the decoder loads');
}

{ // --- a stroke too short to decode says so, and the note expires
  const w = makeWin();
  const out = w.fn.glideFinished(glide(3));
  check('3-point stroke -> "short"', out === 'short');
  check('  pending is NOT left set', w.state.pending === false);
  check('  the decoder was not called', w.calls.decode === 0);
  check('  the expiry timer is armed', w.calls.noteTimer === 1);
  checkResolves(w, 'after a short stroke');

  // A note that never expires becomes furniture — lastShort used to be cleared
  // only by the NEXT full-length glide.
  w.state.lastShort = false;                      // what noteTimer does
  const note = w.fn.noteState(false, true, false, false, false, false, 0);
  check('  once expired, no note at all', note.text === '');
}

{ // --- a full stroke clears a previous "too short"
  const w = makeWin();
  w.fn.glideFinished(glide(3));
  check('short stroke set lastShort', w.state.lastShort === true);
  w.fn.glideFinished(glide(40));
  check('  a full stroke clears it', w.state.lastShort === false);
}

{ // --- precedence: the most actionable state wins
  const w = makeWin();
  const all = (d, r) => w.fn.noteState(d, r, true, true, true, true, 7);
  check('dead decoder outranks everything', all(true, true).text === 'decoder stopped — restart');
  check('  not-ready outranks the rest', all(false, false).text === 'loading decoder… 7 s');
  check('  loading counts as progress, not a problem', all(false, false).problem === false);
  check('busy outranks pending', w.fn.noteState(false, true, true, true, true, true, 0).text === 'busy — glide again');
  check('pending outranks short', w.fn.noteState(false, true, false, true, true, true, 0).text === 'decoding…');
  check('  decoding is progress, not a problem', w.fn.noteState(false, true, false, true, false, false, 0).problem === false);
  check('short outranks stalled', w.fn.noteState(false, true, false, false, true, true, 0).text === 'too short — glide further');
  check('stalled is last', w.fn.noteState(false, true, false, false, false, true, 0).text === 'decode stalled — glide again');
  check('idle says nothing at all', w.fn.noteState(false, true, false, false, false, false, 0).text === '');
}

{ // --- ADR-0004: the note carries structure, never content
  const w = makeWin();
  const texts = [
    w.fn.noteState(true,  true,  false, false, false, false, 0).text,
    w.fn.noteState(false, false, false, false, false, false, 3).text,
    w.fn.noteState(false, true,  true,  false, false, false, 0).text,
    w.fn.noteState(false, true,  false, true,  false, false, 0).text,
    w.fn.noteState(false, true,  false, false, true,  false, 0).text,
    w.fn.noteState(false, true,  false, false, false, true,  0).text,
  ];
  // Nothing here may interpolate a decoded word, a candidate, or typed text —
  // the only variable any of them may carry is a counter.
  const leaks = texts.filter(t => /[a-z]{2,}’|"|“/.test(t));
  check('no note interpolates user content', leaks.length === 0, JSON.stringify(leaks));
  check('every state has a message', texts.every(t => t.length > 0));
}

console.log(`\n${pass} passed, ${fail} failed`);
process.exit(fail ? 1 : 0);
