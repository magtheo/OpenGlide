# Architecture Decision Records

Each significant decision is one file `NNNN-short-slug.md`. The main technical
spec (`../openglide_technical_spec.md`) records the *vision*; this directory
records *decisions* — things we have committed to after investigation, with
primary-source evidence. **Do not expand the spec with guesses.** Put a
verified choice here; put an open executable question in `tools/*-probe/`.

## Statuses

`Proposed` → `Accepted` → `Superseded` (by a later ADR) / `Deprecated`.

## Index

| ADR    | Title                                                              | Status    |
|--------|--------------------------------------------------------------------|-----------|
| 0001   | FUTO Swipe licensing is a preliminary GO                           | Accepted* |
| 0002   | Text-output architecture: commit UTF-8 first, raw keys as fallback | Accepted  |
| 0003   | Async contracts: swipe identity, single-owner output, latency      | Accepted  |
| 0004   | Diagnostics and operations policy                                  | Accepted  |
| 0005   | One grid unit drives the surface; the window collapses, never quits | Accepted* |
| 0006   | Decode accuracy is layered — depth first, then context rescoring     | Proposed  |

\* 0001 and 0005 carry verification gates that must close before public release
(0005's remaining gates need real hardware: a build, the aspect-band corpus
measurement, and the mouse-only walkthrough).

## Conventions

- Cite primary sources for every load-bearing claim. Mark anything not yet
  personally verified as a **verification gate**.
- A supersession records *which* prior decision/ADR (or spec section) it
  replaces; the superseded artifact is left intact for history.
- New `template.md` copies start each ADR.
