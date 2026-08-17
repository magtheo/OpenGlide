# ADR-0003: Async contracts — swipe identity, single-owner output, latency budget

- **Status:** Accepted
- **Date:** 2026-08-09
- **Supersedes:** the diagram-only threading model in spec §26–§27

## Context
The spec's threading model is a diagram, not a contract. Several race-condition
classes are unaddressed: what happens when a new swipe starts before the previous
decode returns; which thread owns the FUTO engine; how results reach the UI; what
"feel immediate" (spec §26) actually means in milliseconds.

Two grounding facts from the `swipe-library` README and `engine.hpp`:

1. **`SwipeEngine` is not thread-safe** (README: "This class is not thread-safe so
   you should take care to avoid concurrent usage"). Every call must be serialized
   by the caller.
2. The engine already exposes per-stage timing via `last_timing()` →
   `{resample_us, encoder_us, decoder_us, beam_us, lm_us, total_us}`. Latency is
   therefore **measurable directly**, not estimated.

## Decision

### 1. Swipe identity + stale-discard
Every gesture gets a monotonically increasing `SwipeId` (uint64). Every async
decode is tagged with the SwipeId that started it. When a result arrives whose
SwipeId ≠ the current one, **discard it**. Obsolete predictions are never queued,
never displayed, never committed.

```
swipe #41 start → decode(#41)
swipe #42 start before #41 returns → decode(#42), current = #42
#41 returns → STALE → discard
#42 returns → display + commit
```

### 2. Single swipe worker owns SwipeEngine
Because `SwipeEngine` is not thread-safe, exactly **one dedicated worker thread**
owns the engine instance and serializes every `recognize` / `setMode` call. The
UI thread never touches the engine. Non-negotiable.

### 3. Single-owner output dispatcher
Swipe worker, speech worker, and correction UI never write to applications
directly. All output flows through **one** `TextOutputQueue` drained by a single
backend owner:

```
SwipeEngine worker ─┐
Whisper worker     ─┼→ TextOutputQueue → one output owner → TextBackend
Correction UI      ─┘
```
No two inference workers ever commit concurrently. This also satisfies
input-method-v2's "one input-method per seat" constraint (ADR-0002).

### 4. Latency budget
Provisional target: **release → first candidate ≤ 100 ms p95** on a defined
reference machine. This is a *hypothesis*. Phase 1 measures the full pipeline
(preprocess → infer → dict → rank → visible) via `last_timing()` and reports
p50 / p95 / p99. Tighten to a firm SLO once measured; if FUTO comfortably beats
100 ms, lower the bar.

### 5. Thread layout
- **UI thread** — Qt/QML, pointer events, rendering. Never blocks on engine/whisper/sqlite.
- **Swipe worker** — owns `SwipeEngine` (serialized). Returns `DecodedWord[]` + `Timing` async.
- **Speech worker** — whisper.cpp inference.
- **Storage worker** — SQLite personalization/history.
- **Output owner** — drains `TextOutputQueue`, drives the active `TextBackend`.

## Amendment (2026-08-17): the shipped stale-discard is inverted, and now visible

§1 says the **newest** gesture wins: every decode carries a `SwipeId`, and a
result whose id is no longer current is discarded. The prototype does the
opposite, and has since the native decoder landed:

```cpp
// decoderbridge.cpp — refuses the NEW glide while one is in flight
bool expected = false;
if (!m_busy.compare_exchange_strong(expected, true)) return false;
```

So glide A, then glide B before A returns: **A commits and B is dropped.** Under
§1 it would be B that commits. `SwipeId` does not exist in the code at all — it
appears only in this ADR and in `data-formats.md`.

This was invisible until the ADR-0005 state pill gave refusals a voice
("busy — glide again"), which is the mechanism by which a silent divergence
became a reportable one. Recording it rather than quietly fixing it, because the
fix is a real choice:

- **§1 as written is better for the user** — the newest glide is the user's
  current intent, and committing the older word after they have already started
  the next one is the wrong answer arriving late.
- **The shipped guard is cheaper and safer** — one atomic, no cancellation path,
  and `SwipeEngine` is single-owner by construction (§2). Inverting it means
  either tagging results and discarding late ones (cheap, but the engine keeps
  burning a core on a decode nobody wants) or real cancellation (not exposed).
- The window is small in practice: 20–60 ms on clean strokes, 300–480 ms on
  sloppy ones (RESULTS.md, KDE section), so this fires only on fast successive
  glides.

**Open**: implement §1's SwipeId-tagged discard, or amend §1 to match the shipped
behaviour. Until it is settled the user is at least told, which is the part that
was actually broken. Do not cite §1 as describing current behaviour.

## Consequences
- No torn/interleaved commits; stale-discard removes a whole class of UI flicker bugs.
- The swipe worker is a serialization bottleneck by design — acceptable because
  inference is the only thing on it and must be serialized anyway.
- `Timing` from the engine feeds the benchmark harness directly; the corpus record
  in `docs/data-formats.md` carries it.

## References
- `SwipeEngine` thread-safety: https://gitlab.futo.org/keyboard/swipe-library/-/raw/master/README.md
- `Timing` struct / `last_timing()`: `include/swipe_decoder/engine.hpp`
