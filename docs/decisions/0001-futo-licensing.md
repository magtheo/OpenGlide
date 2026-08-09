# ADR-0001: FUTO Swipe licensing is a preliminary GO

- **Status:** Accepted (preliminary) — two verification gates remain before public release
- **Date:** 2026-08-09
- **Supersedes:** none (narrows spec §30 from "must be checked" to a recorded decision)

## Context
The entire C++-native architecture (spec §4.1, §5.1) rests on being able to use
and distribute FUTO Swipe. The original review flagged licensing as the top
risk because FUTO as an organization is associated with source-available,
non-OSI licenses. After checking the *specific* components OpenGlide would use,
the picture is substantially better — but one detail needs a repo-wide check.

OpenGlide depends on **two separate** FUTO components with **two separate** licenses.

## Findings (primary sources)

### 1. `swipe-library` (C++ recognition library) — GPL-3.0
- The `LICENSE` file at GitLab master is the verbatim **GPLv3** text (confirmed by direct read).
- The primary public header `include/swipe_decoder/engine.hpp` carries **no** SPDX identifier and **no** "or any later version" notice.
- GPLv3's "or-later" option must be stated explicitly; absent that, the correct SPDX reading is **GPL-3.0-only**.

### 2. FUTO Model Weights (`.pte` files) — FUTO Model Weights License 1.0
Verbatim terms retrieved from the repository's `LICENSE.md`:
- Grants "a non-exclusive, royalty-free, worldwide, non-sublicensable, non-transferable license to use, copy, distribute, make available, and prepare Derivative Models of the Weights **for any purpose**."
- "You may use the Weights or any Derivative Model for any purpose, **including in commercial products and services**, provided that you display a visible notice to end users stating that the product is powered by **'FUTO Swipe'** technology."
- The notice must appear in "settings, about screen, or equivalent disclosure area"; **"Failure to include this notice is a material breach of these terms."**
- Derivative models must pass this license along + carry a derived-from / modified notice.
- Limited trademark license for the "FUTO Swipe" name — *only* as required by the attribution notice.
- Patent-retaliation clause (license terminates immediately if you assert the weights infringe a patent).
- Explicitly does **not** cover inference code / source / datasets — those keep their own licenses (consistent with the library being GPL). Texas governing law.

### 3. Component granularity (model card + README)
Only the encoder (`honorable_sturgeon`, 2.65 MB, layout-agnostic) is required.
The English/QWERTY decoder (`magic_macaw`) and English ContextLM (`hungry_jellyfish`)
are optional refinements. This gives a de-risking fallback: if any optional
model's terms ever become a problem, Phase 1 can ship **encoder-only + our own trie**.

## Decision
**GO (preliminary).** OpenGlide may depend on FUTO Swipe.

1. OpenGlide's own license will be **GPL-3.0-or-later**. Compatible with a
   GPL-3.0-only `swipe-library`, and keeps future flexibility. This makes
   OpenGlide copyleft — the whole application must remain GPL. This matches the
   "open, local, no mandatory cloud" stance in spec §2.2.
2. **Display "Powered by FUTO Swipe"** in a persistent, user-visible location
   (Settings → About). This is a hard UI requirement, not a nicety — omission is
   a material breach of the weights license.
3. **Pin exact revisions at ship time**: record the `swipe-library` git commit
   hash and each model file's SHA-256. Do not depend on whatever `master`
   contains later.

## Consequences
- OpenGlide is a GPL project; no proprietary component may be statically
  combined without compatibility analysis.
- The attribution notice is a permanent UI element.
- Any Norwegian decoder we fine-tune later is a "Derivative Model" → pass-through
  notice obligation (relevant to spec Phase 5).

## Verification gates (close before first public release)
- [ ] Repo-wide check of `swipe-library` for an explicit "or any later version"
      notice, to flip GPL-3.0-only → -or-later. (The main public header was checked and is bare.)
- [ ] Brief legal/license review of both licenses; record outcome as an amendment.
- [ ] Pin and record exact component versions + hashes (e.g. in a `THIRD_PARTY.md`).
- [ ] Confirm the **dictionary** data license is independent of FUTO. AOSP
      `.combined` dictionaries carry their own (AOSP/Apache) license — verify
      before shipping an English lexicon, since the lexicon is bring-your-own
      (see `swipe-library` README, `load_trie_simple`).

## References
- swipe-library LICENSE (GPLv3): https://gitlab.futo.org/keyboard/swipe-library/-/raw/master/LICENSE
- `engine.hpp` (bare of license notice): https://gitlab.futo.org/keyboard/swipe-library/-/raw/master/include/swipe_decoder/engine.hpp
- Weights license (verbatim): https://huggingface.co/futo-org/futo-swipe/resolve/main/LICENSE.md
- Model card: https://huggingface.co/futo-org/futo-swipe
- swipe-library README: https://gitlab.futo.org/keyboard/swipe-library/-/raw/master/README.md
