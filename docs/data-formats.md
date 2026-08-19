# OpenGlide — Data Formats and Schemas

**Status:** Living contract. Everything is versioned from day one. These mirror
the real `swipe-library` API surface (`SwipeEngine`, `KeyboardLayout`, `Timing`),
not guesses.

## Principles
- Every persisted structure carries a `schema_version`.
- Text is UTF-8; offsets/lengths into text are **bytes**, not codepoints (matches
  input-method-v2 — ADR-0002).
- The FUTO decoder consumes coordinates **normalized to 0.0–1.0** in layout-relative
  space (per `SwipeEngine.recognize`). Overshoot (values outside [0,1]) is
  **preserved**, not clipped (spec §6.3); any clipping happens inside the decoder
  adapter, never in the collector.

## Core C++ types

```cpp
// Raw collected sample. time_us is monotonic microseconds since gesture start.
struct SwipePoint {
    float    x;        // normalized layout-relative x (may be outside [0,1])
    float    y;        // normalized layout-relative y (may be outside [0,1])
    uint32_t time_us;  // microseconds since first point
};

struct SwipePath {
    uint32_t                schema_version = 1;
    uint64_t                swipe_id;        // monotonic; see ADR-0003
    std::string             layout_id;       // e.g. "en_qwerty_v1"
    std::vector<SwipePoint> points;
};

enum class CandidateSource : uint8_t {
    SwipeEngine, PersonalDict, Hunspell, CorrectionHistory
};

struct Candidate {
    std::string     text;    // UTF-8
    float           score;   // engine score; ordering semantics per model
    CandidateSource source;
};
```

> **Unit note:** `SwipeEngine.recognize` takes `t` as **milliseconds since the
> first point**. Convert `time_us` → ms at the adapter boundary
> (`t = time_us / 1000.0f`). Defaults: `top_k = 4`, `beam_width = 100`.

## Keyboard layout — `languages/<lang>/layout.json`
Mirrors `swipe_decoder::KeyboardLayout`: letters + normalized centers, one entry
per key. **Duplicate letters are not permitted** (FUTO constraint).

```json
{
  "layout_id": "en_qwerty_v1",
  "language": "en",
  "schema_version": 1,
  "keys": [
    { "label": "q", "x": 0.05, "y": 0.167 },
    { "label": "w", "x": 0.15, "y": 0.167 }
  ]
}
```

## Swipe corpus — `tests/swipe-corpus/*.jsonl`
Recognition regression set (spec §24.2). One JSON object per line:

```json
{
  "schema_version": 1,
  "target": "computer",
  "layout_id": "en_qwerty_v1",
  "points": [
    {"x": 0.4141, "y": 0.8991, "time_us": 0},
    {"x": 0.4478, "y": 0.8580, "time_us": 100000}
  ],
  "candidates": [{"text": "computer", "score": 12.3, "source": "SwipeEngine"}],
  "selected": "computer",
  "timing": {"resample_us": 120, "encoder_us": 9100, "decoder_us": 0,
             "beam_us": 4300, "lm_us": 0, "total_us": 13520}
}
```

`timing` mirrors `SwipeEngine::Timing` (ADR-0003), so corpus runs double as
latency samples.

### Live corpus — `~/.local/share/openglide/corpus-live.jsonl`

Corpus-from-real-use (ADR-0006 step 1). Appended by the Qt prototype when
`OPENGLIDE_LOG_CONTENT=1` (the ADR-0004 content opt-in — a recorded corpus IS
user content; the app announces the file loudly at startup). Three line types:

```json
{"glide_id": 1787035301348, "schema_version": 1, "target": "test",
 "greedy": "test", "layout_id": "en_qwerty_v1", "points": [...],
 "candidates": [...], "selected": "test", "decode_ms": 41.9,
 "context": ["need", "to"]}
{"amend_of": 1787035301348, "selected": "test"}
{"drop_of": 1787035301348}
{"drop_of": 1787035301348, "ambiguous_reject": true}
```

- `glide_id` is epoch-ms at decode start (unique: decodes are serialized).
- `context` is the last 0-2 committed words (QML `history`, newest last) at
  glide start. Added for ADR-0006 layer 2 (n-gram context rescoring): every
  corpus before this had no sequence data, only isolated words, so a
  rescorer had nothing real to be tested against. Older lines lack this
  field; treat missing as `[]`.
- `context_overridden` (bool) is true when the layer-2 rescorer changed which
  candidate is top-1/`target`/`selected` for THIS glide, based on `context`.
  It is live (RESULTS.md "context rescoring — real signal, and a real risk"):
  known to fix some real cases and known to misfire on casual spellings the
  bigram source (formal, edited text) has never seen. Older lines lack this
  field; treat missing as `false`.
- `ambiguous_reject` (bool, only present on `drop_of` lines) is true when the
  glide was in the E4 ambiguous state (RESULTS.md "which one?" friction fix,
  2026-08-19) and backspace rejected it — neither offered candidate was the
  intended word, and nothing was ever committed. A plain `drop_of` (field
  absent/false) means top-1 committed and was chip-deleted afterward — a
  different failure (top-1 wrong but not offered as a choice) from this one
  (both offered choices wrong). Distinguishing the two is what lets a future
  margin/candidate-quality sweep measure "how often are the ambiguous chips
  themselves both wrong" the same way the original margin sweep measured
  ambigMargin.
- A glide line's `target`/`selected` are the **top-1 guess, not a label** — the
  guess is the thing being measured. Trustworthy labels come from the app's
  edit paths: a chip **correction** appends an `amend_of` line naming the true
  word; a chip **delete** appends `drop_of` (the top-1 was wrong; intent
  unknown). Backspace-retyping is not attributed and emits nothing.
- `fold_corpus.py` (native-decode-spike) merges amendments, marks drops, and
  emits both this JSONL schema (with `corrected`/`deleted` flags) and the flat
  TSV `corpus_test` reads. `--corrected` selects only chip-corrected glides
  (gold labels); unamended survivors are "user let it stand" labels — weaker,
  but real.

## SQLite schema — `storage/migrations/`
Numbered, applied in order, recorded in `schema_migrations`.

```sql
-- 001_initial.sql
CREATE TABLE IF NOT EXISTS schema_migrations (
    id          INTEGER PRIMARY KEY,
    applied_at  TEXT NOT NULL DEFAULT (datetime('now'))
);
CREATE TABLE settings (
    key    TEXT PRIMARY KEY,
    value  TEXT NOT NULL
);

-- 002_correction_history.sql
CREATE TABLE correction_history (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    original     TEXT NOT NULL,
    replaced     TEXT NOT NULL,
    layout_id    TEXT NOT NULL,
    context_hash TEXT,                      -- hash, not raw context (ADR-0004)
    occurrences  INTEGER NOT NULL DEFAULT 1,
    updated_at   TEXT NOT NULL DEFAULT (datetime('now'))
);

-- 003_personal_dictionary.sql
CREATE TABLE personal_words (
    word        TEXT PRIMARY KEY,
    frequency   INTEGER NOT NULL DEFAULT 0,
    language    TEXT NOT NULL,
    added_at    TEXT NOT NULL DEFAULT (datetime('now'))
);
```
