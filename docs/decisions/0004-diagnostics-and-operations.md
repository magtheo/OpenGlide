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

## Amendment (2026-08-11): the sanctioned opt-in, named

§2 called for "a `--log-content` flag or a settings toggle" without naming one.
The prototype now has content logging in a real code path — `og_process_key_event`
traces every key it routes, and a key name is user content: that trace is a
keystroke log, passwords included. Naming the opt-in so it cannot be stumbled
into:

- **`OPENGLIDE_LOG_CONTENT=1`** is the sanctioned switch (`OPENGLIDE_KEY_DEBUG`
  is kept as an accepted alias). Both route through one resolver.
- It **announces itself loudly** on first use — a banner naming exactly what will
  be captured — so nobody enables it without knowing.
- It is **per-session and never persisted**: an environment variable dies with
  the process, satisfying §2's "not persisted across restarts by default".
- Output goes to **stderr, not a file**. §2 says "a local file at a path the user
  can see and delete"; stderr is strictly more conservative — nothing is written
  to disk to be forgotten about, and it is visible in the terminal that enabled
  it. If a file sink is added later it must be at a documented, user-deletable
  path.

The rule this amendment enforces: **any new diagnostic that can print a key, a
word, a candidate, or a transcript must go through this switch** — not its own
ad-hoc environment variable.

## Consequences
- Bug reports cannot accidentally contain user text; contributors ask for
  structure, not logs-by-default.
- The opt-in path keeps real decoding bugs diagnosable without weakening the
  default posture.
- `correction_history` in SQLite stores a context **hash**, not raw surrounding
  text (see `docs/data-formats.md`).
