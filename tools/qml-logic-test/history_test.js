const fs = require('fs');
const extracted = fs.readFileSync(process.argv[2], 'utf8');

// The target application's real buffer, rebuilt ONLY from injector calls. The
// invariant under test: it must always equal `injected`, the mirror QML keeps.
// Drift between the two is what corrupts text on correction.
let target = '';
const injector = {
  backspace: n => { target = target.slice(0, Math.max(0, target.length - n)); },
  commit:      w => { target += w + ' '; },
  commitExact: s => { target += s; },
  typeChar:    c => { target += c; },
};
const decoderCalls = [];   // amendRecord/dropRecord(gid, word) log — corpus labels ride on these
const decoder = {
  bumpWord: () => {},
  amendRecord: (gid, w) => decoderCalls.push(["amend", gid, w]),
  dropRecord: (gid)     => decoderCalls.push(["drop", gid]),
};

function makeWin() {
  const state = {
    injected: '', candidates: [], history: [], histOpen: -1, histMax: 12, shiftState: 0,
    pendingUndo: '', pendingUndoEntry: null, undoGen: -1,
    targetGen: 0, staleMs: 300000,             // IBus present unless a test says otherwise
    lastGid: 0, undoTimer: { restart(){}, stop(){} },      // QML Timer stub
  };
  const build = new Function('state', 'injector', 'decoder', `
    with (state) {
      ${extracted}
      return { pushHistory, trimHistory, replaceHistory, deleteHistory, correct,
               commitDecoded, deleteChar, deleteWord, tapPunct, tapSpace, typeKey,
               consumeShift, shifted, tapDeleteWord, undoDelete, clearUndo, undoWord,
               entryLive };
    }`);
  return { state, fn: build(state, injector, decoder) };
}

let pass = 0, fail = 0;
function check(name, cond, extra = '') {
  cond ? pass++ : fail++;
  console.log(`  ${cond ? 'ok  ' : 'FAIL'} ${name}${extra && !cond ? '  <- ' + extra : ''}`);
}
function checkSync(w, label) {
  check(`${label}: mirror == target buffer`, w.state.injected === target,
        `mirror=${JSON.stringify(w.state.injected)} target=${JSON.stringify(target)}`);
}
function checkOffsets(w, label) {
  const bad = w.state.history.filter(e => w.state.injected.substr(e.start, e.len) !== e.text);
  check(`${label}: every history offset still points at its word`, bad.length === 0,
        JSON.stringify(bad));
}
function glide(w, word, alts) {
  w.state.candidates = [{text: word}, ...alts.map(t => ({text: t}))];
  w.fn.commitDecoded(word);
}

console.log('history: offsets, replacement, deletion, mirror/target sync\n');

{ // --- build up three glided words
  target = ''; const w = makeWin();
  glide(w, 'there', ['three', 'their']);
  glide(w, 'is',    ['us']);
  glide(w, 'stone', ['store', 'atone']);
  check('3 glides -> 3 history entries', w.state.history.length === 3);
  check('  text is "there is stone "', w.state.injected === 'there is stone ');
  checkSync(w, 'after glides'); checkOffsets(w, 'after glides');

  // --- correct the NEWEST word via the candidate slot (the old-only path)
  w.state.candidates = [{text:'stone'},{text:'store'}];
  w.fn.correct(1);
  check('correct newest -> "there is store "', w.state.injected === 'there is store ');
  checkSync(w, 'after newest correction'); checkOffsets(w, 'after newest correction');
}

{ // --- correct an OLD word: the thing that was impossible before
  target = ''; const w = makeWin();
  glide(w, 'there', ['three']);
  glide(w, 'is',    []);
  glide(w, 'stone', []);
  w.fn.replaceHistory(0, 'three');            // fix the FIRST of three words
  check('correct oldest -> "three is stone "', w.state.injected === 'three is stone ');
  checkSync(w, 'after oldest correction'); checkOffsets(w, 'after oldest correction');
  check('  history still 3 entries', w.state.history.length === 3);
}

{ // --- length changes must shift every following offset
  target = ''; const w = makeWin();
  glide(w, 'a', []); glide(w, 'bb', []); glide(w, 'ccc', []);
  w.fn.replaceHistory(0, 'aaaaaaaa');         // much longer
  check('grow first word', w.state.injected === 'aaaaaaaa bb ccc ');
  checkOffsets(w, 'after growing'); checkSync(w, 'after growing');
  w.fn.replaceHistory(0, 'z');                // much shorter
  check('shrink first word', w.state.injected === 'z bb ccc ');
  checkOffsets(w, 'after shrinking'); checkSync(w, 'after shrinking');
}

{ // --- delete a middle word
  target = ''; const w = makeWin();
  glide(w, 'one', []); glide(w, 'two', []); glide(w, 'three', []);
  w.fn.deleteHistory(1);
  check('delete middle -> "one three "', w.state.injected === 'one three ');
  check('  history drops to 2', w.state.history.length === 2);
  checkOffsets(w, 'after middle delete'); checkSync(w, 'after middle delete');
}

{ // --- punctuation collapses a space; offsets must survive it
  target = ''; const w = makeWin();
  glide(w, 'hello', ['hallo']);
  w.fn.tapPunct(',');
  glide(w, 'world', []);
  check('punctuation -> "hello, world "', w.state.injected === 'hello, world ');
  checkSync(w, 'after punctuation');
  w.fn.replaceHistory(0, 'hallo');
  check('correct word before the comma', w.state.injected === 'hallo, world ');
  checkSync(w, 'after correcting before punctuation');
  checkOffsets(w, 'after correcting before punctuation');
}

{ // --- manual editing must invalidate, not silently mis-target
  target = ''; const w = makeWin();
  glide(w, 'alpha', []); glide(w, 'bravo', []);
  w.fn.deleteChar(); w.fn.deleteChar(); w.fn.deleteChar();   // chew into "bravo"
  check('after 3 backspaces text is "alpha bra"', w.state.injected === 'alpha bra');
  check('  damaged entry dropped from history', w.state.history.length === 1);
  check('  surviving entry is "alpha"', w.state.history[0].text === 'alpha');
  checkOffsets(w, 'after manual edit'); checkSync(w, 'after manual edit');
}

{ // --- word-delete gesture
  target = ''; const w = makeWin();
  glide(w, 'one', []); glide(w, 'two', []);
  w.fn.deleteWord();
  check('deleteWord -> "one "', w.state.injected === 'one ');
  check('  history drops the removed word', w.state.history.length === 1);
  checkOffsets(w, 'after word delete'); checkSync(w, 'after word delete');
}

{ // --- capitalization is preserved through correction
  target = ''; const w = makeWin();
  w.state.shiftState = 1;
  glide(w, 'there', ['three']);
  check('shift -> "There "', w.state.injected === 'There ');
  w.state.candidates = [{text:'there'},{text:'three'}];
  w.fn.correct(1);
  check('  correction keeps the capital -> "Three "', w.state.injected === 'Three ');
  checkSync(w, 'after capitalized correction');
}

{ // --- history is capped
  target = ''; const w = makeWin();
  for (let i = 0; i < 20; i++) glide(w, 'w' + i, []);
  check('history capped at histMax=12', w.state.history.length === 12);
  checkOffsets(w, 'after cap'); checkSync(w, 'after cap');
}

{ // --- tapping backspace deletes a word, and that must be undoable
  target = ''; const w = makeWin();
  glide(w, 'there', ['three', 'their']);
  glide(w, 'stone', ['store']);
  w.fn.tapDeleteWord();
  check('tap-delete removes the last word', w.state.injected === 'there ');
  check('  undo is staged', w.state.pendingUndo === 'stone ');
  check('  its history entry is staged too', w.state.pendingUndoEntry !== null &&
        w.state.pendingUndoEntry.text === 'stone');
  checkSync(w, 'after tap-delete'); checkOffsets(w, 'after tap-delete');

  w.fn.undoDelete();
  check('undo restores the exact text', w.state.injected === 'there stone ');
  check('  and the word is back in history', w.state.history.length === 2 &&
        w.state.history[1].text === 'stone');
  check('  with its alternatives intact', w.state.history[1].cands.length === 2);
  check('  undo offer is consumed', w.state.pendingUndo === '');
  checkSync(w, 'after undo'); checkOffsets(w, 'after undo');

  // the restored word must still be correctable through the normal path
  w.fn.replaceHistory(1, 'store');
  check('restored word is still correctable', w.state.injected === 'there store ');
  checkSync(w, 'after correcting a restored word');
}

{ // --- a stale undo offer must not survive further typing
  target = ''; const w = makeWin();
  glide(w, 'alpha', []); glide(w, 'bravo', []);
  w.fn.tapDeleteWord();
  check('undo staged', w.state.pendingUndo === 'bravo ');
  glide(w, 'charlie', []);
  check('  cleared by a new glide', w.state.pendingUndo === '');
  w.fn.tapDeleteWord(); w.fn.tapSpace();
  check('  cleared by space', w.state.pendingUndo === '');
  glide(w, 'delta', []);
  w.fn.tapDeleteWord();
  check('  staged again', w.state.pendingUndo !== '');
  w.fn.deleteChar();                       // a real edit
  check('  cleared by a char delete', w.state.pendingUndo === '');
  checkSync(w, 'after stale-undo churn');

  // A backspace on empty text changes nothing, so the offer stays valid —
  // deliberate: clearUndo() is about staleness, and nothing went stale.
  const w2 = makeWin(); target = '';
  glide(w2, 'solo', []);
  w2.fn.tapDeleteWord();
  check('empty-text backspace is a no-op', w2.state.injected === '');
  w2.fn.deleteChar();
  check('  and keeps the undo offer alive', w2.state.pendingUndo === 'solo ');
  w2.fn.undoDelete();
  check('  which still restores', w2.state.injected === 'solo ');
  checkSync(w2, 'after no-op backspace then undo');
}

{ // --- undoing a tap-delete of a word that was never in history
  target = ''; const w = makeWin();
  w.fn.typeKey('h'); w.fn.typeKey('i'); w.fn.tapSpace();
  check('typed text present', w.state.injected === 'hi ');
  w.fn.tapDeleteWord();
  check('tap-delete removes typed word', w.state.injected === '');
  check('  no history entry to stage', w.state.pendingUndoEntry === null);
  w.fn.undoDelete();
  check('  undo still restores the text', w.state.injected === 'hi ');
  checkSync(w, 'after undoing a non-history word');
}

{ // === target identity: never edit a document we no longer own ===============
  target = ''; const w = makeWin();
  glide(w, 'there', ['three']);
  glide(w, 'stone', ['store']);
  const textBefore = w.state.injected, bufBefore = target;
  check('two words committed', w.state.injected === 'there stone ');

  // focus moves to a different field -> the offsets now address someone else's
  // document. Every correction path must refuse rather than edit blind.
  w.state.targetGen = 1;

  w.fn.replaceHistory(0, 'three');
  check('replaceHistory REFUSES after focus change', w.state.injected === textBefore);
  check('  and issued no output at all', target === bufBefore);

  w.fn.deleteHistory(1);
  check('deleteHistory REFUSES after focus change', w.state.injected === textBefore);
  check('  and issued no output at all', target === bufBefore);

  w.state.candidates = [{text:'stone'},{text:'store'}];
  w.fn.correct(1);
  check('candidate correct() REFUSES after focus change', w.state.injected === textBefore);
  check('  and issued no output at all', target === bufBefore);

  // a word glided into the NEW target is live again and fully correctable
  glide(w, 'water', ['eater']);
  check('new word under the new target commits', w.state.injected === 'there stone water ');
  w.fn.replaceHistory(2, 'eater');
  check('  and is correctable', w.state.injected === 'there stone eater ');
  checkSync(w, 'after correcting under the new target');
}

{ // === age cap: the only guard available on the uinput fallback ==============
  target = ''; const w = makeWin();
  w.state.staleMs = 20000;                    // no IBus -> short window
  glide(w, 'alpha', ['alpha2']);
  check('fresh entry is live', w.fn.entryLive(w.state.history[0]) === true);
  w.state.history[0].t -= 25000;              // pretend 25 s elapsed
  check('  aged-out entry is not live', w.fn.entryLive(w.state.history[0]) === false);
  const before = w.state.injected, buf = target;
  w.fn.replaceHistory(0, 'alpha2');
  check('  and correcting it REFUSES', w.state.injected === before && target === buf);
}

{ // === staged undo is target-scoped too ======================================
  target = ''; const w = makeWin();
  glide(w, 'alpha', []); glide(w, 'bravo', []);
  w.fn.tapDeleteWord();
  check('undo staged', w.state.pendingUndo === 'bravo ');
  w.state.targetGen = 7;                      // focus moved before the user clicked undo
  const before = w.state.injected, buf = target;
  w.fn.undoDelete();
  check('undo REFUSES into a different target', w.state.injected === before && target === buf);
  check('  and the stale offer is cleared', w.state.pendingUndo === '');
}

{ // --- corpus amendments: a correction names the glide it corrects (ADR-0006 step 1)
  target = ''; const w = makeWin(); decoderCalls.length = 0;
  w.state.lastGid = 755217000001;                 // what candidatesReady(gid) set
  glide(w, 'stone', ['store', 'atone']);          // commits top-1 "stone", gid 755217000001
  w.state.lastGid = 755217000002;
  glide(w, 'river', ['rover']);                   // commits "river", gid 755217000002
  check('glides record no amendments by themselves', decoderCalls.length === 0);
  w.fn.replaceHistory(0, 'store');                // correct the OLDER word
  check('correction amends the right glide',
        JSON.stringify(decoderCalls) === '[["amend",755217000001,"store"]]',
        JSON.stringify(decoderCalls));
  w.fn.replaceHistory(1, 'rover');                // then the newer one
  check('second correction amends its own glide',
        decoderCalls.length === 2 && decoderCalls[1][0] === "amend" && decoderCalls[1][1] === 755217000002);
  w.fn.deleteHistory(0);                          // deleting marks, not amends
  check('delete DROPS its record (intent unknown)',
        decoderCalls.length === 3 && decoderCalls[2][0] === "drop" && decoderCalls[2][1] === 755217000001,
        JSON.stringify(decoderCalls));
  const gids = w.state.history.map(e => e.gid);
  check('surviving entries keep their gids', JSON.stringify(gids) === '[755217000002]',
        JSON.stringify(gids));
}

console.log(`\n${pass} passed, ${fail} failed`);
process.exit(fail ? 1 : 0);
