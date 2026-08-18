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

**Amendments.** An ADR that is refined rather than replaced grows an
`## Amendment (date): <what changed>` section instead of being superseded — the
original text stays put so the reasoning is still readable. Four carry one (0006
uses `## Addendum` per its own convention):

| ADR  | Amendment                                                                  |
|------|----------------------------------------------------------------------------|
| 0003 | the shipped stale-discard is *inverted* vs §1 — the new glide is refused, so the older word commits; `SwipeId` is unimplemented. Open choice |
| 0004 | `OPENGLIDE_LOG_CONTENT=1` named as the sanctioned content-logging opt-in |
| 0005 | machine **state** returns to the default surface (a pill, no payload); the committed **mirror** stays opt-in |
| 0006 | diagnose gate ran: the doubling bonus is a measured −12 pp regression (0 rescues, 5 flips); `window` is alph-pruned, not length-pruned |

## Conventions

- Cite primary sources for every load-bearing claim. Mark anything not yet
  personally verified as a **verification gate**.
- A supersession records *which* prior decision/ADR (or spec section) it
  replaces; the superseded artifact is left intact for history.
- New `template.md` copies start each ADR.
