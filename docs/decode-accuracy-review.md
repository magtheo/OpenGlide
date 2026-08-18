# Decode accuracy — how to accept sloppier glides without losing reliability

**Status:** Analysis + brainstorm. Feeds [ADR-0006](decisions/0006-decode-accuracy-strategy.md),
whose §Decision is explicitly waiting on a measurement that has not been run.
Nothing here is settled.
**Date:** 2026-08-17
**Subject:** `tools/native-decode-spike/swipe_engine.cpp` @ `4175731` (the decoder
the Qt prototype actually runs).
**Measured here:** the length-band arithmetic, reproducibly, via
`tools/native-decode-spike/band_probe.py` — no model required.
**Not measured here:** anything needing the encoder's emissions. This box has no
ExecuTorch, no `.pte`, and no system dictionary.

---

## 0. The framing

"Tolerate sloppier input" and "give a reliable answer" only fight each other if
the decoder is **one ranked list built from one source of evidence**. Today it
essentially is:

```
score = CTC(path, word)                  # the neural encoder, via trie DP
      + 4.5 × doublings                  # one hand-built correction
      + 2.0 × log(user_count + 1)        # personalization
      + 0 × log(corpus_freq)             # disabled: it broke hello -> help
```

With a single axis, loosening anything that lets more words in mechanically
raises the chance one of them out-scores the right one. That is the real reason
the current design is strict — the prunes are load-bearing, because nothing else
is holding the ranking up.

So the strategy is three moves, and **they must ship together or not at all**:

1. **Widen recall** — stop *excluding* the right word (the hard gates, §1).
2. **Add independent evidence** — so precision at rank 1 survives the wider set
   (§3). Independent is the operative word: the frequency prior failed precisely
   because it was *correlated* with CTC's error (`help` beat `hello` on both).
3. **Make a wrong #1 cheap** — top-3 sufficiency beats top-1 heroics (§4). This
   is spec §36's actual claim, and it is the cheapest of the three.

---

## 1. What makes it strict today: four constants

Three hard gates decide whether a word is *scored at all*. None is a soft
penalty; each is a cliff.

| gate | rule | where |
|---|---|---|
| **G1 alph prune** | a word is scored only if **every** letter is in the per-timestep top-3 set | `swipe_engine.cpp:298-300`, used at `:247`, `:314` |
| **G2a depth cap** | the trie DFS stops at `greedy_len + 3` | `:245`, `:305` |
| **G2b length band** | `\|word_len − greedy_len\| ≤ 3` | `:242` |
| **G3 lexicon filter** | `2 ≤ word_len ≤ 12` | `:139` |

### G1 is the harshest, and it is un-measured

`alph` is the union, over all 32 timesteps, of each timestep's top-3 letters. A
word survives only if **all** its letters are in that set — so a 7-letter word
gets seven independent chances to be eliminated, and one letter that never makes
any timestep's top-3 kills it outright, no matter how good the rest of the path
is. This is a hard gate standing in for what should be a cost.

I cannot measure how often it fires without the model. **`corpus_test
--diagnose` already reports exactly this** (`alph-pruned` in `DecodeDiag`) and
has never been run. That tally is gate zero for everything below.

### G2 is measurable, and it is already throwing away a known miss

`band_probe.py`, over all 39 usable recorded glides (the controlled 18 plus the
uncontrolled 21 that RESULTS.md doesn't disown):

```
(word_len − greedy_len) distribution: {-1: 3, 0: 32, +1: 3, +4: 1}
exact-length greedy: 32/39      within ±1: 38/39
length-pruned:        1/39  ->  window (greedy 'wi')
```

Two things fall out, and the second is the interesting one:

**a. The band does no discrimination work on clean input.** 38 of 39 greedy
strings land within one character of the target length. A ±3 window around a
value that is almost always exact is not a filter — it is a no-op that costs
nothing to widen *on the input it already handles*.

**b. The controlled corpus's only miss was never scored.** `window` with greedy
`wi` is `|6−2| = 4 > 3`, so it was excluded from scoring entirely. RESULTS.md and
ADR-0006 F1 both record this as "a truncated stroke, not a decode error" — true
about the *cause*, but it has been read ever since as "nothing to fix here." The
decoder never got to have an opinion about `window`. Whether it would have won is
unknown; that it was not allowed to compete is now arithmetic.

That single case is the whole thesis in miniature: **the gates fail exactly on
the sloppy input we are trying to accept, and they fail silently.**

### The cost of widening is small and shrinks with word length

Length-eligible words at ±3 vs ±5 (proxy lexicon, `word_freq.txt`):

| greedy len | ±3 | ±5 | ratio |
|---|---|---|---|
| 4 | 21 514 | 37 129 | 1.73× |
| 5 | 29 661 | 43 139 | 1.45× |
| 6 | 37 038 | 47 316 | 1.28× |
| 8 | 43 825 | 49 999 | 1.14× |

And that overstates it: these are words eligible *by length only*. G1 cuts the
set before any CTC work happens, and the trie shares prefixes, so DFS cost grows
far slower than the word count. Current decode is ~79 ms avg / ~350 ms worst
(RESULTS.md) against a 100 ms p95 budget (ADR-0003 §4) — there is not unlimited
headroom, which is why §2's frontier idea matters more than simply raising the
constants.

### G3 quietly drops 6% of the lexicon

`word_len > 12` removes 3 274 of 53 373 proxy words — *understanding*,
*conversation*, *particularly*. Probably not where the losses are, but it is a
constant nobody chose deliberately.

---

## 2. Replace hard gates with a cost-bounded frontier

Raising G1/G2's constants is the cheap experiment and should be run first, but it
trades accuracy for latency along a bad curve: the constants are global, so they
pay for the worst case on every glide.

The structural fix is to stop pruning by *rule* and start pruning by *cost*:

- **Beam the trie walk.** At each depth keep the best N partial prefixes by alpha
  rather than every prefix that clears the gates. Cost becomes bounded by N
  (tunable, predictable) instead of by the lexicon's shape, which is what lets
  G1 and G2 be loosened or dropped without the latency blowing up. This is what
  ADR-0006 §1 calls "a wider effective beam", but the point is not width — it is
  that a frontier is *self-limiting* where a gate is not.
- **Demote G1 from gate to prior.** Instead of "every letter must be in the
  top-3", charge a penalty proportional to how far down the letter's best
  timestep posterior sat. A sloppy letter then costs the candidate something
  instead of erasing it.
- **Derive the length band from a soft statistic, not the greedy string.**
  Everything downstream — `glen`, `maxwlen`, the band, the doubling comparison —
  is computed from one greedy path, so when greedy collapses (`wi`), every
  downstream decision inherits the collapse. Expected non-blank emissions,
  `Σ_t (1 − P(blank | t))`, is a length estimate that degrades gracefully where
  the argmax path falls off a cliff. **This is the direct fix for the measured
  `window` miss**, and it is a dozen lines.

---

## 3. Independent evidence — the part that keeps it reliable

Widening recall without this is how you turn a 94% decoder into an 85% one.

### E1. A shape channel — the biggest missing signal, and half-built already ★

Nothing currently scores **how well the drawn path matches the candidate word's
key polyline**. The CTC score is about letter emissions over time; it does not
directly ask "does this stroke look like the shape of *because*?"

This is the classic pre-neural channel (SHARK²/ShapeWriter scored shape and
location separately and combined them), and it is the ideal complement here
because **its errors are uncorrelated with CTC's**. That is exactly the property
the frequency prior lacked.

The geometry already exists: `reaspect.h` builds the polyline through a word's
key centres and measures a path's deviation from it — written for the aspect
sweep, directly reusable. For each of the top-K survivors: resample both to a
common length, take DTW or discrete Fréchet distance, add
`ν · (−normalized_distance)`.

Why it fits *this* problem: shape matching is tolerant of local wobble and speed
variation (the definition of sloppy) while remaining sharply sensitive to
visiting the wrong keys (the definition of wrong). `mouse` vs `mousse` and
`coffee` vs `code` are shape-separable in a way they are not CTC-separable.

### E2. N-gram context rescoring — ADR-0006 layer 2

`score += μ · log P(word | previous 1–2 words)`. The case for it is already made
in ADR-0006 F3, and it is the one lever that provably fixes the class that killed
the frequency prior: *sequence* probability separates "say **hello**" from "need
**help**" where *word* frequency cannot. Conservative override only — it must not
be able to overturn a confident CTC+shape agreement, or it becomes the
over-correction failure mode.

Practical note: bigrams are a new data dependency, and the T460s is the budget.
A hashed top-N bigram table is a few MB and microseconds to query.

### E3. Generalize the doubling bonus into an elision model

`count_doublings()` handles exactly one phenomenon: a doubled key crossed once.
But the recorded corpus shows the *other* direction just as often — `stonee`,
`hpouse`, `tavble` are greedy strings with **inserted** letters from passing over
keys en route.

Both are the same phenomenon: the drawn path visits key regions the word does not
contain, and misses ones it does. The layout says which. **A letter whose key
centre lies close to the straight segment between its neighbours is cheap to skip
or cheap to insert**; one that requires a real detour is not. A geometric edit
cost over (candidate, greedy) subsumes the doubling bonus, the length band, and
the `because → bose` class in one model — and it is computable entirely from
`layout()`, which is already the runtime source of truth.

### E4. Use the score margin you already have

`scored[0].first − scored[1].first` is a confidence signal that is computed and
discarded. Low margin should change *behaviour*, not just ranking: don't
auto-commit, or commit and flag. High margin: commit silently. This converts a
slice of "wrong word committed, now correct it" into "pick one of two", which is
the cheapest accuracy win available and needs no model work at all.

### E5. Learn from corrections, not just from use

`bump()` increments the chosen word. But a correction carries strictly more
information than a use: it is the pair *(rejected #1, chosen word)* for a
specific glide. Recording pairs lets a correction outweigh a plain use, and it is
the raw material for spec §11.2's adaptation. `correction_history` is already in
the spec's schema and already specified to store a context **hash**, not text
(ADR-0004), so the privacy shape is settled.

### E6. Adapt the key centres to the user

The decoder scores against `layout.json`'s centres, but a given user's aim has
systematic bias — consistently short of the top row, consistently left on the
home row. Since `set_layout()` exists and the geometry is already dynamic,
nudging centres toward the user's measured aim (from confirmed-correct glides
only) turns "sloppy" into "consistent", which the model handles fine. Speculative
and needs guardrails, but nobody else can do this and the plumbing is built.

### E7. Resampling convention (probe, and treat as risky)

`resample_into()` resamples uniformly in **time**, so a slow dwell gets many
samples and a fast sweep gets few. Arc-length resampling is usually more robust
for sloppy input. **But** the FUTO encoder was trained on a specific convention,
and changing it may take the input out of distribution — this is a measure-first
item that could as easily lose 10 points as gain them. Cheap to A/B, dangerous to
assume.

---

## 4. The other half: make a wrong answer cheap

Spec §36 already says it: *"a system that predicts 94% of words correctly and
lets the remaining 6% be corrected with one click may be substantially more
usable than a nominally more accurate system with poor correction."* Two items
outrank most of §3 on effort-per-benefit:

- **Preedit** (ADR-0006's own consequences section, and the UX review's B20). If
  the current word stays uncommitted, a wrong #1 costs one click and *no
  deletion* — which also routes around the fact that deletion is broken on GNOME
  (`delete_surrounding_text` aborts gnome-shell). This lowers the accuracy bar
  rather than raising the decoder over it.
- **Confidence-gated auto-commit** (E4), which is the same idea applied to when
  the system should be sure.

---

## 5. Order of work

Every item in §2 and §3 is a hypothesis until the first row exists.

| # | Work | Why here |
|---|---|---|
| 0 | **Run `corpus_test --diagnose`; record the tally.** | ADR-0006's own gate, never run. Splits misses into alph-pruned / length-pruned / out-ranked / not-in-dict, and each points at a *different* fix. Without it §2 is guesswork. |
| 1 | **Grow the corpus from real use.** | Every gate needs ≥30 real glides and nobody has hand-captured them since the 24-glide session. Record `(points, target, candidates, chosen)` behind the existing `OPENGLIDE_LOG_CONTENT` opt-in — the UX review's B22 and this are the same feature. |
| 2 | **Cheap constants A/B**: G2 band ±3→±5 (with `maxwlen` raised in step), G1 top-3→top-5, G3 12→longest dict word. | Hours of work, targets the measured `window` class directly, and the §1 table puts the cost at 1.1–1.7× length-eligible words before the alph prune even applies. Accept only with zero regressions. |
| 3 | **Soft length estimate** (§2, expected non-blank emissions). | The direct fix for the one miss we can prove was excluded. A dozen lines. |
| 4 | **E4 margin → behaviour**, **E1 shape channel**. | E4 is nearly free. E1 is the highest-value new evidence and `reaspect.h` is 80% of it. |
| 5 | **Beam frontier** (§2). | Buys the latency headroom that makes loosening G1/G2 permanent rather than a trade. |
| 6 | **E2 n-gram rescoring.** | ADR-0006 layer 2. Needs a bigram dependency; do it once the recall work has stopped moving. |
| 7 | **E3 elision model**, **E5 correction pairs**, **E6 centre adaptation**, **E7 resample probe.** | Longer-horizon; E7 measure-first. |

ADR-0006's acceptance rule stands for all of it: **a change is accepted only if
it lifts top-1 without regressing cases that currently work** — `hello`, `good`,
and the doubles that already recover must survive; `help` must still not win when
`hello` was meant.

> **Measured update 2026-08-18 — row 0 ran, and it edits this table.** Full
> numbers in RESULTS.md ("corpus_test --diagnose finally run"). Three rows move:
>
> - **Row 2 is half-emptied by measurement.** 0 length-pruned across 39 glides;
>   the G2/G3 halves of row 2 have no measured miss to fix, and the `window`
> class is **alph-pruned** (`'n'` never top-3), not length-pruned — the G1
> top-3→top-5 half is what targets it, and the soft length estimate (row 3)
> should be understood as fixing the alph/depth interaction, not the band.
> - **A new row 0.5 jumps the queue: the doubling bonus.** Six of seven misses
> at the shipped config lost to a spurious doubled consonant; `--doublings 0`
> takes combined top-1 82% → 94.9% with `hello`/`green` still winning on CTC
> alone. The flat λ=4.5 (review §3 E3's "degenerate case") is measured
> net-negative; E3 dwell-gating is now the principled fix, not a generalization.
> - **Row 4's E4 margin got its exemplar**: the last cap-0 miss loses by 0.09
> nats (`riber` → river/rober, rank 2) — exactly the don't-auto-commit case.
>
> Caveat carried from RESULTS.md: the live-24 glides that motivated the bonus
> were never persisted; re-capture (row 1) before deleting it outright.
>
> **Measured update (later the same day) — row 2's G1 half is now measured
> NET-NEGATIVE.** `--alph K` A/B over all three corpora (RESULTS.md "G1 alph
> A/B"): widening changes nothing on controlled/daily, costs 8 pp on live, and
> `window` stays alph-pruned at K=6 — a truncated stroke never emitted those
> letters at any rank, so no K recovers it. Keep K=3; truncated glides are a
> data problem ("too short — glide further" is the fix), not a gate problem.
> Row 0.5's E4 margin instead SHIPPED: gap < 0.6 nats between top-1/top-2 now
> holds the commit and offers the contenders as chips.

---

## 6. What this does not change

- The decoder stays word-level CTC over a dictionary trie. Everything proposed
  here is a **prune change or an added score term**, not a replacement — the
  proven path stays intact and each term is independently switchable, which is
  what makes the acceptance gate meaningful.
- The aspect clamp (ADR-0006 F4, top-1 83% at 10:5) is independent and can land
  any time.
