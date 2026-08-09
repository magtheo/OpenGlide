# ADR-0004: Diagnostics and operations policy

- **Status:** Accepted
- **Date:** 2026-08-09
- **Supersedes:** none (fills a gap; spec §2.2 commits to local-only data)

## Context
OpenGlide sits between the user and every application, handling typed text,
swiped geometry, and speech. Input software carries an unusually high privacy
bar: a leaked diagnostics log can expose everything a person typed. The spec
(§2.2) commits to local-only personalization and no cloud; the logging and
operations posture must match.

## Decision

### 1. Never log user content by default
No typed text, no swiped words, no recognized candidates, no speech audio, no
transcribed text is written to logs at default levels. Diagnostics carry
**structure and telemetry only**: stage timings, candidate counts, backend
selection, errors, focus/visibility transitions, sample counts — never payloads.

### 2. Explicit opt-in content logging
A deliberately-awkward opt-in (`--log-content` flag or a settings toggle with a
clear warning) enables geometry/timing/content logging for one session. It is
**not** persisted across restarts by default. When on, content goes to a local
file at a path the user can see and delete.

### 3. Lifecycle
- Runs as a normal user process — never root (spec §14.1).
- Optional XDG autostart entry and/or `systemd --user` unit; auto-restart off by default.
- Crash-safe: a crash mid-gesture must not leave the input-method or uinput device
  in a stuck/grabbed state. Backends release all grabs and fds on teardown.

### 4. Input grabbing
- The global toggle listener (spec §13.2) is **observe-only** initially — it
  watches evdev chord state without consuming events. Full interception /
  re-emission via `uinput` is deferred until the §13.3 test proves it is needed.
- No permanent exclusive mouse grab.

## Consequences
- Bug reports cannot accidentally contain user text; contributors ask for
  structure, not logs-by-default.
- The opt-in path keeps real decoding bugs diagnosable without weakening the
  default posture.
- `correction_history` in SQLite stores a context **hash**, not raw surrounding
  text (see `docs/data-formats.md`).
