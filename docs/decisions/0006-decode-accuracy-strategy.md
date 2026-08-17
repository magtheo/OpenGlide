# ADR-0006: Decode accuracy is layered — depth first, then context rescoring

- **Status:** Proposed — findings verified 2026-08-11; the decode-depth and
  n-gram rescoring work is not yet implemented
- **Date:** 2026-08-11
- **Supersedes:** none (addresses the accuracy line in [plan.md](../plan.md)
  Phase 2; revisits the frequency-prior finding recorded under "Tap-to-type +
  double-letter recovery" in [RESULTS.md](../../tools/RESULTS.md))

## Context

The pipeline is proven and the infrastructure (IBus commit, window/layout,
history, caret avoidance, key routing) is in place. What is **unmeasured** is
the property that decides whether OpenGlide is usable: **how accurate is glide
decoding on real input.** Every UX feature is wasted if glide accuracy is poor.

The decoder was only ever scored on a small *controlled* corpus. Two open
questions shape the strategy:

1. Where does it actually stand on real glides (varying speed, angle, overshoot)?
2. The user proposed whole-text **context correction** — analyzing the sequence
   of committed words and fixing ones that "seem wrong." Is that the right lever?

A frequency prior was tried and **rejected** earlier (RESULTS.md): it broke
`hello→help` because *help* is more frequent than *hello* in written ngrams — no
positive λ can pick hello. Any new accuracy work has to avoid repeating that.

## Findings (primary sources)

Measured 2026-08-11 with `tools/native-decode-spike/corpus_test` (trie-CTC +
double-letter recovery + personalization, the current decoder) and a real-glide
capture to `/tmp/glidetest.log`.

### F1. Controlled corpus ≠ real glides
- **Controlled corpus** (`corpus-controlled.jsonl`, 18 clean glides): **top-1
  94% (17/18).** The decoder recovers correct words from messy greedy paths
  (`stonee→stone`, `cpmpurer→computer`, `helo→hello`). The single miss
  (`window`) was a truncated glide (`greedy="wi"`), not a decode error.
- **`corpus.jsonl` (26) is unreliable:** entries #2–6 (computer/party/glide/
  open/plant) all decode to `river` despite 600–770 points each — they start and
  end at ~(0.35, 0.17) and are duplicated/mislabelled glides. Its 77% figure is
  polluted; do not cite it.
- **Real glides (24, captured live):** clearly below the controlled 94%.
  Double-letter recovery held (`god→good`, `helo→hello`) and long words with
  moderate mess recovered (`compiet→computer`, `keybiard→keyboard`,
  `difrtemxr→difference`). But: `coffee→code` ×3, `because→bose` ×3,
  `mouse→mousse`. Real wobble/speed produces greedy paths the decoder sometimes
  cannot climb back from.

### F2. Misses split into two classes — and only one is fixable by re-ranking
- **Rerank-able** (the right word was a candidate, just not #1): `mouse→mousse`
  (mouse was a close 2nd). A re-ranker can fix these.
- **Decode-depth** (the right word was *absent* from the top-5): `coffee→code`
  (coffee absent on 2 of 3), `because→bose` (because absent on 3 of 5). **No
  post-hoc re-ranker can recover a word the decoder buried** — there is nothing
  to re-rank up. These need the decode/scoring itself to surface the word.

> **Addendum 2026-08-11 — "buried" has three causes, not one, and they were being
> guessed at.** A word is scored only if every letter survives the per-timestep
> top-3 `alph` prune, its length is ≤ `greedy_len + 3`, and
> `|wordlen − greedylen| ≤ 3`. Each of those has a different fix, and none of them
> is "wider beam". `decode()` now takes an optional `DecodeDiag` naming which
> prune removed the word (or its rank if it was merely out-scored), and
> `corpus_test --diagnose` prints a per-miss verdict plus a tally. **The §Decision
> ordering below should be re-derived from that tally**, since it currently rests
> on three words.
>
> Two specifics already established by reading the code:
> - **`coffee` was never a depth problem.** The double-letter bonus required
>   `word.size() == greedy.size() + 1` — exactly ONE doubling — so `coffee`
>   (needs `ff` *and* `ee`) could not receive it at any beam width. That is a
>   scoring cap. `count_doublings()` now handles N pairs, tunable via
>   `--doublings` (1 = old behaviour) so the two can be A/B'd directly.
> - **`because` may be length-pruned, not out-ranked.** At 7 letters it survives
>   only if greedy came out ≥ 4 characters; a shorter greedy excludes it from
>   scoring entirely. `--diagnose` answers this definitively.

### F3. Context rescoring is real, and avoids the frequency-prior trap
Re-ranking the top-K with a sequence model (n-gram / LM) is exactly what
smartphone keyboards do. Crucially it disambiguates where frequency could not:
"say **hello**" vs "need **help**" — *sequence* probability separates them, where
*word* frequency picked the wrong one (F-prior rejection). The risk is
over-correction (changing a word the user meant), so it must override only when
context strongly favors an alternative.

### F4. Aspect band — there IS a cliff, at the tall end (re-done with the fixed model)
The earlier "no cliff" sweep used the line-fit `reaspect`, which is structurally
blind to trailing overshoot (a smooth excursion a local line treats as intended).
With the fixed word-polyline model (`reaspect.h`, `--model word`): top-1 drops to
**83% at 10:5.0 (k=0.60, tall)** and holds 94% from 10:4.0 through 10:1.8 (wide).
Implication: clamp resize so the board cannot get taller than ~**10:4** (aspect
≥ 2.5); the wide direction stays free.

## Decision

Accuracy work is **layered, depth before re-ranking**, because F2 shows
re-ranking is useless without decode depth:

1. **Decode depth first.** Get the right word *into* the candidate set. Levers:
   a wider effective beam during the trie search; stronger multi-double-letter
   handling (`coffee` needs ff **and** ee — the current heuristic doubles one
   letter); better recovery from very messy greedy paths (the `because→bose`
   class). This is the higher-leverage half.
2. **Context rescoring second** — the user's whole-text idea, as an **n-gram LM**
   over the previous 1–2 words that re-ranks the top-K. **n-gram, not neural**:
   microseconds and a small table, viable on the T460s; a neural LM is too heavy.
   Conservative — override only on strong contextual evidence (avoids
   over-correction and the F-prior trap, F3).
3. Both ship as **probes first** (`tools/` experiment + RESULTS.md number),
   measured on the controlled corpus **and** a fresh real-glide set, before any
   change reaches the product decoder. A change is accepted only if it lifts
   top-1 **without** regressing cases that currently work.
4. **Clamp the resize band** per F4 (aspect ≥ ~2.5) — small, independent, can
   land anytime.

## Consequences

- The decoder stays word-level CTC; the LM is a **re-ranker over its output**,
  not a replacement. This keeps the proven CTC path intact and the LM swappable.
- Decode-depth work may touch the trie search + double-letter heuristic
  (`tools/native-decode-spike/`) — shared by the spike and the Qt prototype.
- An n-gram LM needs a corpus (the project already uses
  `word_freq.txt`/ps-kostikov; bigram/trigram data is a new dependency to source).
- Over-correction is the live risk of layer 2; the acceptance gate (no
  regressions) is what guards it.
- This does **not** address committed-text deletion (separate problem —
  `delete_surrounding_text` faults gnome-shell; see RESULTS.md). Accuracy and
  deletion are independent *workstreams* — but not independent *priorities*:
  spec §36 says a 94% decoder with one-click correction beats a nominally better
  one with poor correction, and correction currently rests on a primitive that is
  imprecise by its own comment. Preedit would make correcting the current word
  exact and deletion-free, which lowers the accuracy bar this ADR is trying to
  raise. **`IBUS_CAP_PREEDIT_TEXT` is now probed and shown in diagnostics** — if
  real apps report it, preedit deserves to be weighed against both levers here
  rather than deferred behind them.

## Verification gates

- [ ] **Run `corpus_test --diagnose` and record the miss tally** (out-ranked /
  length-pruned / alph-pruned / not-in-dict) in RESULTS.md — this is what turns
  F2 from a three-word anecdote into a measurement, and it should come *before*
  the depth-vs-rescoring ordering is locked.
- [ ] **A/B the doubling cap**: `--doublings 1` vs `--doublings 2` on the
  controlled corpus. Accept only with zero regressions (`hello`, `good`, and the
  doubles that already recover must all still work; `help` must still not).
- [ ] Decode-depth probe: on the controlled corpus **and** a fresh ≥30-real-glide
  set, the change lifts top-1 with zero regressions vs the current decoder.
  Source: `tools/` probe + `corpus_test`.
- [ ] n-gram rescoring probe: re-ranking the top-K lifts top-1 on the real-glide
  set **without** breaking `hello`/`good`/the doubles that currently recover.
  Source: `tools/` probe + RESULTS.md.
- [ ] Real-glide corpus of ≥30 words captured + committed (the existing
  corpora are too small / partly corrupt — F1).
- [ ] Resize clamp lands (aspect ≥ ~2.5) and the 10:5 row no longer degrades.
- [ ] A standing real-glide accuracy number is reported in RESULTS.md (target:
  competitive with the controlled 94% on real input).

## References

- Current decoder: `tools/native-decode-spike/` (trie-CTC, double-letter
  recovery); accuracy harness `corpus_test --sweep [--model word|line]`.
- Fixed aspect model: `reaspect.h` + `aspect_model_test.cpp` (commit 900ea17).
- Frequency-prior rejection + double-letter recovery: RESULTS.md "Tap-to-type +
  double-letter recovery".
- Real-glide capture: `/tmp/glidetest.log` `[decode]` lines (decoderbridge).
