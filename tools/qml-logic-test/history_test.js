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
const decoder = { bumpWord: () => {} };

function makeWin() {
  const state = {
    injected: '', candidates: [], history: [], histOpen: -1, histMax: 12, shiftState: 0,
  };
  const build = new Function('state', 'injector', 'decoder', `
    with (state) {
      ${extracted}
      return { pushHistory, trimHistory, replaceHistory, deleteHistory, correct,
               commitDecoded, deleteChar, deleteWord, tapPunct, tapSpace, typeKey,
               consumeShift, shifted };
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

console.log(`\n${pass} passed, ${fail} failed`);
process.exit(fail ? 1 : 0);
