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
