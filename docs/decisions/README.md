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
| 0005   | One grid unit drives the surface; the window collapses, never quits | Proposed |

\* 0001 carries verification gates that must close before public release.

## Conventions

- Cite primary sources for every load-bearing claim. Mark anything not yet
  personally verified as a **verification gate**.
- A supersession records *which* prior decision/ADR (or spec section) it
  replaces; the superseded artifact is left intact for history.
- New `template.md` copies start each ADR.
