# OpenGlide probe results

Empirical output of the `tools/text-output-probe` and `tools/surface-probe` runs.
These convert ADR-0002's architecture from speculation into measured facts.
Append rows as more compositors / sessions are tested.

## Environment under test
- **Session:** GNOME on Wayland (mutter), with XWayland at `DISPLAY=:0`.
- **Compositors installed here:** `gnome-shell`, `Xorg`, `Xephyr` only — no Sway / Hyprland / Plasma.
- **Active XKB layout:** Norwegian (so `å æ ø` exist as physical keys; `日 本 🫐` do not).
- `/dev/uinput` accessible (group `input`).

## text-output-probe
Build: `cd tools/text-output-probe && make` → `./text-probe`. Dry-run by default; `--fire` emits events.

### `xtest` backend (XWayland) — TESTED ✅
`text-probe --backend xtest --text "blåbær æøå 日本 🫐"` → **injectable=12, uninjectable=3**.

| char | cp | keysym | keycode | result |
|------|----|--------|---------|--------|
| å | U+00E5 | 0xE5 | 34 | injectable (on Norwegian layout) |
| æ | U+00E6 | 0xE6 | 48 | injectable |
| ø | U+00F8 | 0xF8 | 47 | injectable |
| 日 | U+65E5 | 0x10065E5 | — | **NO keycode — uninjectable** |
| 本 | U+672C | 0x100672C | — | **NO keycode — uninjectable** |
| 🫐 | U+1FAD0 | 0x101FAD0 | — | **NO keycode — uninjectable** |

**Finding:** raw-key injection is **layout-bound**. A character is injectable iff it maps to a keycode on the active XKB layout; characters absent from the layout (CJK, emoji, arbitrary Unicode) are impossible. This is more precise than "ASCII-only": it scales with the active layout but never reaches full Unicode. Confirms ADR-0002.

### `uinput` backend — TESTED ✅
ASCII table by design (probe under-reports `å/æ/ø` which the layout *could* map to evdev codes). The underlying limit is identical to XTest: evdev has no Unicode codepoints, so injection is layout-bound.

### `im2` backend (Wayland `zwp_input_method_v2`) — HEADLINE ✅
`text-probe --backend im2` connects and dumps the mutter registry. Key observations:

- mutter advertises `zwp_text_input_manager_v3` (the **application** side) — apps can receive committed text.
- mutter does **NOT** advertise `zwp_input_method_manager_v2` — a generic client **cannot** bind the input-method peer directly.
- mutter does **NOT** advertise `zwlr_layer_shell_v1`.

**Finding:** on GNOME Wayland, OpenGlide cannot use `input-method-v2` directly; the seat's input-method slot is mediated by GNOME's IBus integration. **OpenGlide on GNOME must commit UTF-8 via the IBus path** (ADR-0002's "GNOME → flows through IBus" branch), now empirically grounded. (On Sway / Hyprland / wlroots, `input-method-v2` is expected to be advertised to clients — run this same probe there to confirm.)

> Caveat: the IBus `commit_text` backend is **not yet implemented** (needs `libibus-1.0-dev`, which requires `sudo` to install). The GNOME UTF-8 commit is therefore *architecturally determined*, not yet *empirically witnessed landing in an app*.

## surface-probe (X11/XWayland) — TESTED ✅
Build: `cd tools/surface-probe && make` → `./surface-probe [seconds]`.

Maps an override-redirect window, then is clicked twice via `xdotool` while keyboard focus is monitored.

- Probe window `0x1200001` mapped (`override_redirect=True`) on `:0`.
- Initial keyboard focus = `0x600003`.
- Two `ButtonPress` events received (at 130,80). After each, **focus stayed `0x600003`**.
- `focus_is_probe = no` for the entire 6 s run.

**Finding:** an override-redirect X11 window receives mouse input **without stealing keyboard focus** from the target app. The X11 / XWayland focus-preservation mechanism (spec §17) works as designed.

> GNOME-native Wayland surface: no `wlr-layer-shell` (registry-confirmed) → GNOME does not support layer-shell; its OSK/IME popup uses a GNOME-private path. The layer-shell surface implementation is therefore **pending a wlroots compositor** (Sway/Hyprland) to build and test against.

## Matrix status

| Compositor       | text-commit path                  | surface/focus path        | status |
|------------------|-----------------------------------|---------------------------|--------|
| GNOME Wayland    | IBus (im-v2 **not** exposed)      | no layer-shell            | ✅ tested |
| X11 / XWayland   | xtest/uinput (layout-bound)       | override-redirect ✅      | ✅ tested |
| Sway             | input-method-v2 (expected)        | wlr-layer-shell           | pending |
| Hyprland         | input-method-v2 (expected)        | wlr-layer-shell           | pending |
| KDE Plasma       | Fcitx5 / own IME                  | LayerShellQt              | pending |

## FUTO decoder spike (spec §35 / Risk 3)

Script: `tools/futo-spike/validate.py` (Python; venv at `tools/futo-spike/.venv`).
Setup: `python3 -m venv tools/futo-spike/.venv && .venv/bin/pip install --index-url https://download.pytorch.org/whl/cpu torch && .venv/bin/pip install executorch huggingface_hub numpy`.
Run: `HF_HOME=$PWD/tools/futo-spike/models .venv/bin/python tools/futo-spike/validate.py`.

### Baseline — PASS
Dictionary-free greedy decode (encoder-only) of the model-card sample trajectory yields **"computer"**. The FUTO stack runs operationally — ADR-0001's GO is now empirically grounded, not just legally permitted.

### Robustness (greedy decode under perturbation)

| perturbation | result | margin |
|---|---|---|
| spatial Gaussian σ=0.005 / 0.01 / 0.02 | `computer` | tolerates ~2% noise |
| spatial Gaussian σ=0.04 / 0.08 | `vompuer` / `vpmpirtre` | breaks ~0.03 |
| subsample to 40 / 27 / 18 / 13 pts | `computer`×3 / `comper` | tolerates ~18 raw pts |
| timing jitter 20 / 50 / 100 / 200 ms | `computer`×2 / `compouer` / `compter` | tolerates ~50 ms |
| speed warp 0.5× / 2× / 4× | `computer`×3 | fully tolerant |

**Reading:** the encoder is shape-driven and time-normalized — robust to sparse sampling and speed variation (both relevant to mice), moderately robust to spatial noise and timing jitter. Encouraging for the mouse case, but the real mouse-vs-finger answer needs actual glides.

### Real mouse glides (the actual Risk-3 experiment) — PASS
Recorder `recorder.py` (tkinter QWERTY grid) + offline `dictionary_decode.py` (CTC word scoring over `/usr/share/dict/american-english`, 71k words). 26 glides captured.

| metric | result |
|---|---|
| greedy exact-match (dictionary-free) | 16/26 = 62% |
| **dictionary top-1** | **21/26 = 81% overall; 21/21 = 100% on clean glides** |
| dictionary top-3 / top-5 | 81% / 81% |
| decode latency (per glide) | ~5 ms (well under the 100 ms p95 budget, ADR-0003) |

The dictionary recovered every greedy near-miss: `geen→green`, `tavble→table`, `helo→hello`, `riber→river`, `compuer→computer`.

**The 5 misses (glides 2–6) are a capture anomaly, not a model failure:** their recorded paths all physically cross the R-I-V-E-R key cluster (`rtuijbvrer`, `rtuibvfer`, …) despite different prompts, so FUTO correctly decoded the captured stroke as "river." Glides 1 and 7–26 capture target-matching strokes and decode perfectly. Root cause of the early-session quirk is unconfirmed (planned: live in-glide path display + X pointer grab for out-of-window capture in recorder v2).

**Conclusion:** FUTO decodes real mouse trajectories with dictionary at **100% top-1 on clean captures** at ~5 ms. Risk 3 is answered affirmatively — the project's core assumption holds on mouse input. Corpus saved at `tools/futo-spike/corpus.jsonl` (data-formats.md JSONL schema) for regression use.

### Start-position hypothesis — CONFIRMED (controlled re-collect)
Re-ran with the first letter of each prompt highlighted and the instruction "start the glide on the highlighted key" (`corpus-controlled.jsonl`, 18 glides). Every glide started on the correct first key, the "everything → river" anomaly vanished, and path-traces matched targets (window→`wetyui`, stone→`sdtyonbre`, place→`pljfacfe`, …). Result: **dict top-1 = 17/18 (94%)**; the single miss was a too-short stroke, not a start-position failure → 17/17 on full strokes.

**Product insight (mouse-specific):** because the cursor persists between words (a finger lifts; a mouse cursor does not), reliable decode requires beginning each glide on the target's first letter. The product UX must make this natural, or the decoder must tolerate mid-keyboard starts. (Phone swipe gets this for free; mouse glide typing does not.)

### Spec-aligned capture surface (§4.2 / §6.2 / §6.3) + overshoot finding
`tools/swipe-capture/` — a minimal C/`Xlib` `SwipeSurface`: on LMB press it `XGrabPointer`s the pointer (capture continues outside the window, §6.2) and records **unclamped** normalized coords (overshoot preserved, §6.3). Validated end-to-end: dictionary top-1 = **9/9 (100%)** on captured glides (`capture.jsonl`) — the spec-aligned capture path produces trajectories the decoder handles perfectly.

**Overshoot probe (`overshoot_probe.py`) refines §6.3:** the encoder tolerates *leading* overshoot (start before the first key, x→−0.3 → still `computer`) but **trailing and mid-trajectory overshoot corrupt decode** (tail to x=1.3 → `computep`; mid spike x>1.1 → `comppter`). This is mouse-specific — phone-swipe overshoot is usually at the start (tolerated), but mouse momentum produces trailing overshoot. **Conclusion: keep the collector unclamped (§6.3 stands for capture), but the decoder adapter must handle trailing/mid overshoot** (clamp to `[0,1]`, or drop trailing out-of-bounds points, before inference). Heads-up for the real `SwipeDecoder` adapter.

## Phase 1 working prototype (Qt6) — spec §28 milestone
`tools/qt-prototype/`: Qt6/QML QWERTY + C++ `SwipeSurface` (§4.2) → persistent Python FUTO decoder (greedy + dict top-5) → uinput injection. **End-to-end verified: hold LMB → glide → decoded word → injected into a focused app (Firefox/editor/terminal) on XWayland.**

Findings from building it:
- **`Qt.WindowDoesNotAcceptFocus | Qt.WindowStaysOnTopHint` preserves keyboard focus on XWayland** (§17) — gliding in the prototype does not steal focus from the target app, so injected words land there. Override-redirect remains the fallback if a compositor ignores the hint.
- **evdev `KEY_*` are physical scancodes (QWERTY order), not alphabetical** — `KEY_A + (c-'a')` is wrong and `'m'` collided with `KEY_LEFTSHIFT`, producing uppercase gibberish. Fixed with an explicit per-letter table (same latent bug fixed in `tools/text-output-probe/src/uinput.c`).
- **QML binding-break gotcha:** assigning a property that already has a declarative binding destroys the binding — the status label stuck at "decoding…" until it was made a pure binding. Worth remembering for the real UI.
- **Decode latency** via Python-over-pipe is ~100–300 ms/glide (dictionary CTC); the encoder itself is ~5 ms. The native C++ `SwipeEngine` removes the pipe + Python overhead.

Scope: ASCII-only injection (uinput, layout-bound — ADR-0002); UTF-8 needs the input-method/IBus path. Out-of-window capture depends on Qt's mouse-grab per platform.

## IBus commit path — GNOME UTF-8 (ADR-0002 gate) ✅ MET
`tools/ibus-engine-probe/` validates the one cell `text-output-probe` left open: GNOME/mutter does **not** expose `zwp_input_method_v2`, so the IME commit path on GNOME runs through **IBus**. The probe registers a throwaway engine `openglide-probe` with the running ibus-daemon, activates it as the global engine, and on `enable` calls `ibus_engine_commit_text("blåbær æøå 日本 🫐")`.

**Result (2026-08-09): EXACT MATCH.** A GTK3 sink (`gtk_sink.py`) self-triggers the probe the instant its `TextView` gains focus; the committed string lands verbatim in the buffer (`/tmp/ibus_out.txt` == `blåbær æøå 日本 🫐`), focus is retained, and the previous global engine (here `vocalinux`) is restored. Automated and deterministic.

Mechanism findings:
- **Activation works in-process:** `ibus_factory_new(ibus_bus_get_connection(bus))` + `ibus_factory_add_engine` + `ibus_bus_request_name` + `ibus_bus_register_component` on one connection is enough for ibus-daemon to `CreateEngine` back into our factory — no installed component file needed for the transient probe.
- **Deadlock gotcha:** `ibus_bus_set_global_engine` is synchronous; calling it before the main loop runs deadlocks (ibus-daemon's `CreateEngine` callback can't dispatch while we block). Use `ibus_bus_set_global_engine_async`, kicked from an idle once the loop runs.
- **Engine-desc NULL strings:** every string property must be populated or `register_component`'s GVariant serialization hits `g_variant_new_string(NULL)`. `IBusComponent`'s executable property is `command_line` (not `exec`).
- **`commit_text` routes only to the focused client's context owned by the *active* engine** — so production use requires the OpenGlide engine to be the selected input method (the user switches to it, like any IME). Raw `uinput`/`XTest` cannot do non-ASCII on a non-matching layout — that asymmetry is exactly ADR-0002's point.

## Native C++ SwipeEngine (ExecuTorch) — replaces futo_server.py ✅
`tools/native-decode-spike/`: the FUTO encoder runs natively via the ExecuTorch C++ runtime (built from source, v1.4.0, **XNNPACK backend** — the `.pte` is XNNPACK-delegated, ~40 partitions; a portable-only build fails to load it). Full decode ported to C++ (resample → `forward` → greedy + pruned dictionary CTC top-5, overshoot adapter). Wired into the Qt prototype (`DecoderBridge` is now native; `futo_server.py` superseded).

**Result (2026-08-09):**
- **Warmup eliminated:** model load **0.1 ms** (was ~8 s of Python imports — dominated by `import executorch.runtime` ~5.3 s; the model itself loads in 0.27 s, dict 0.55 s).
- **Per-decode:** encoder `forward` **7 ms**; dictionary decode avg **~79 ms** (worst-case ~350 ms on long common-prefix words; was ~1350 ms / 919 ms outlier in Python) — ~17× faster avg.
- **Correctness parity:** `spike` replays Python's exact tensors — native `forward` matches to **3.8e-6** (greedy `computer`). Corpus (controlled, 18 glides) top-1 **17/18 (94%)**, identical to the Python decoder (same lone too-short-stroke miss).

Mechanism / gotchas:
- The `.pte` returns 3 outputs; emissions are `output[0] = [1,32,65]` (32 timesteps × 65 vocab: 26 letters + blank@64).
- ExecuTorch C++ runtime isn't packaged — built from source (clone v1.4.0 + build-critical submodules: `third-party/{flatcc,flatbuffers,json,gflags}` + XNNPACK's `{FP16,FXdiv,cpuinfo,pthreadpool}` + `shim`). flatc/flatcc build from the vendored trees (no system flatc). CMake ≥3.24; the `futo-spike` venv provides cmake 4.x and the `torchgen`/`pyyaml` the runtime's codegen step requires (system python lacks `torchgen`).
- `EXECUTORCH_BUILD_EXTENSION_MODULE` requires `EXECUTORCH_BUILD_EXTENSION_NAMED_DATA_MAP`. Use the high-level `extension::Module` (load → execute("forward", inputs)); tensors via `from_blob(data, sizes, ScalarType)` and `EValue` implicit-converts from a dereferenced `TensorPtr`. Read output with `.template const_data_ptr<float>()`.
- **Trie CTC decode (2026-08-10):** replaced per-word CTC scoring with a single forward DP over a dictionary prefix trie — each node computes its letter/blank α from its parent's (prefix sharing), pruned to the greedy-derived `alph` set + length band. Exact port (corpus ranking byte-identical to the per-word scorer), ~1.8× faster avg (144→79 ms) and ~2.4× on the outlier (840→350 ms); `lae` uses the single-`exp` `log1p` form. Next lever: beam pruning over the trie to bound the candidate set.

## IBus commit — wired into the Qt prototype ✅
`tools/qt-prototype/src/ibus_engine.{c,h}`: the app hosts the OpenGlide IBus engine itself (no installed component, no separate engine binary). On startup it connects to ibus-daemon, registers a pass-through `openglide` engine (forwards physical keys so typing still works while it's active), and self-activates it as the global engine via in-process `ibus_bus_set_global_engine_async`. Glide decodes commit via `og_ibus_commit` → `ibus_engine_commit_text` (UTF-8, layout-independent); `uinput` is the layout-bound fallback. The previous engine is restored on shutdown.

**Result (2026-08-10): EXACT MATCH.** With a focused GTK sink (`GTK_IM_MODULE=ibus`), the prototype's `og_ibus_commit("æøå 🫐 openglide")` lands verbatim in the buffer (`/tmp/og_ibus_out.txt` == `æøå 🫐 openglide`). The full ADR-0002 primary path works from the app.

Mechanism findings:
- **External `ibus engine openglide` fails** ("Cannot find engine") — a runtime `register_component` isn't visible to ibus-daemon's external `SetGlobalEngine` resolver. But the **in-process** `set_global_engine_async` on the registering connection works (the probe's proven path) — so the app self-activates.
- **GLib/Qt event loops don't mix** — IBus runs in its own thread with a dedicated `GMainContext` (the bus is bound to it via `push_thread_default` before `ibus_bus_new`). Cross-thread commits marshal via `g_main_context_invoke`, which is **asynchronous here** (it returns before the source dispatches) — so `og_ibus_commit` heap-allocates the job (+ `strdup`s the text) and **waits for dispatch** (polls `done`, ≤0.5 s). A stack job dangles: that was the first crash — `commit_idle` ran after the caller returned, `strlen`'d the freed text → SIGSEGV in `ibus_text_new_from_string`.
- **Pass-through key forwarding** (`ibus_engine_forward_key_event` in `process_key_event`) is required so the active engine doesn't absorb physical keys — otherwise self-activating would break normal typing.
- **The engine pointer can dangle across threads too** — `g_engine` is set in `enable`/cleared in `disable` (GLib thread). Reading it on the Qt thread and passing it to `commit_idle` races with disable (use-after-free). Fix: `commit_idle` re-reads `g_engine` *itself* on the GLib thread (serialized with enable/disable → valid-or-NULL, never dangling). If the engine disabled between the caller's check and dispatch (focus drifted), `commit_idle` sees NULL → safe no-op → Injector falls back to `uinput`.

## Tap-to-type + double-letter recovery ✅
- **Tap-to-type** (`src/swipesurface.{h,cpp}`): `SwipeSurface` tells tap from swipe by total displacement (<5% normalized = stayed on one key). A tap emits `tapped(nx,ny)` instead of `swipeCompleted`; QML types the nearest key (lowercase) via the injector + a brief key-pop. Coexists with glide.
- **Double-letter recovery** (`swipe_engine.cpp`): the glide passes a doubled key once, so greedy emits a single letter (god, helo) and the true double-letter word (good, hello) is CTC-penalized. Candidates equal to the greedy with one letter doubled (`is_greedy_with_double`: collapse one adjacent-identical pair == greedy) get +4.5 nats → recovers good←god, hello←helo.

**Why not a frequency prior (tried, rejected):** a log-frequency prior (`score += λ·log(count)`) was added first to fix good→god ("good" ~10× commoner). It fixed that but broke **hello→help**. Logged scores showed why — at λ=1.0 `help(15.15) hello(7.95)`; backing out the prior gives **help CTC −3.25 vs hello CTC −5.27** (help's CTC is *higher*) and help is ~178× more frequent ("hello" is rare in written ngrams). So help beats hello on **both** axes → no positive λ can pick hello. The doubled letter is the real signal; the targeted bonus fixes good/god *and* hello/help. Corpus stays 94% throughout (neither prior nor bonus override clear CTC winners). `decoderbridge` logs top-5 scores per glide for tuning.

## Personalization — learn the user's words ✅
`swipe_engine` keeps per-user word counts at `~/.local/share/openglide/user_freq.tsv` (loaded at startup). `decode` snapshots them under a mutex (`bump` runs on the UI thread) and adds `user_lambda·log(count+1)` (λ=2.0) before ranking — so words the user actually uses win ties over time. This is the right "learn common words": it learns the USER's vocabulary, sidestepping the help>hello problem that killed the generic frequency prior (the user teaches it "hello" by using/correcting it). Bumped on every glide commit + candidate correction (`decoder.bumpWord`); `bump()` saves immediately because SIGTERM/crash skips the dtor (so shutdown-only save would lose everything). Verified: persistence test (bump hello×2/world/good → file has `hello 2, world 1, good 1`); corpus still 94% (empty user_freq → no-op).

## Letter-block aspect ratio — cliff at the tall end (re-run with the fixed model) ⚠️
ADR-0005 made `u` (column) and `rowH` (row) independent so the window fills any
shape, which unpinned the letter block from the 10:3 it was captured at. Run:
`corpus_test … --sweep` (re-projects `corpus-controlled.jsonl`, 18 glides).

**Result (2026-08-11): no cliff.** Top-1 was flat at **17/18 (94%)** — the corpus
baseline — at every aspect from 10:5 (tall) to 10:1.8 (wide), i.e. k = 0.60 →
1.67. Top-5 identical, latency flat at ~76 ms.

| letter block | k | top-1 | top-5 | avg ms |
|---|---|---|---|---|
| 10:5.0 (tall) | 0.60 | 17 (94%) | 17 (94%) | 76.7 |
| 10:4.0 | 0.75 | 17 (94%) | 17 (94%) | 75.4 |
| 10:3.5 | 0.86 | 17 (94%) | 17 (94%) | 75.4 |
| **10:3.0 (ref)** | 1.00 | **17 (94%)** | 17 (94%) | 75.7 |
| 10:2.6 | 1.15 | 17 (94%) | 17 (94%) | 77.4 |
| 10:2.2 | 1.37 | 17 (94%) | 17 (94%) | 76.3 |
| 10:1.8 (wide) | 1.67 | 17 (94%) | 17 (94%) | 76.3 |

The single miss is the same word at every aspect — the known too-short stroke, a
dictionary/stroke issue, not a shape one.

**Decision (line model — SUPERSEDED): resizing stays unclamped.** Re-run with the
fixed word model found a cliff; see below.

**⚠️ This run used the `line` model, which cannot see the dangerous case.**
`--model line` scales the residual from a *locally straight* path, and trailing
overshoot — the mechanism this very file identifies as the one that **corrupts**
decode — is a smooth, low-frequency excursion, so a local fit follows it, calls
it intended, and never amplifies it. The sweep was structurally blind to the
failure it was looking for, which is the likeliest reason every row is identical
to the digit.

**`--model word` (now the default) fixes that** by decomposing against the
polyline through the *target word's* key centres: overshoot lands past the end of
that polyline, so it registers as deviation and does scale. Verified in
`aspect_model_test.cpp` — at k=1.67 the word model amplifies a trailing overshoot
**1.56×** and pushes more points out of bounds, where the line model leaves it at
**1.00×**.

**So the sweep above should be re-run** (`--sweep`, which now defaults to `word`).
If it stays flat, the "no cliff" conclusion is real; if it does not, the resize
band needs clamping after all.

**Re-run 2026-08-11 with `--model word` (now the default): there IS a cliff, at the
tall end.** Top-1 drops to **15/18 (83%) at 10:5.0 (k=0.60)** — the fixed model
sees the trailing-overshoot amplification the line model was blind to — and holds
**17/18 (94%) from 10:4.0 through 10:1.8 (wide)**.

| letter block | k | top-1 (`word`) | top-1 (`line`, blind) |
|---|---|---|---|
| 10:5.0 (tall) | 0.60 | **15 (83%)** | 17 (94%) |
| 10:4.0 | 0.75 | 17 (94%) | 17 (94%) |
| 10:3.0 (ref) | 1.00 | 17 (94%) | 17 (94%) |
| 10:1.8 (wide) | 1.67 | 17 (94%) | 17 (94%) |

**Corrected decision: clamp the tall end.** The board must not get taller than
~**10:4** (aspect ≥ ~2.5); the wide direction stays free (robust to 10:1.8).
See ADR-0006 §F4.

Remaining caveats either way: both models assume perfect re-aiming at the new key
centres and unchanged speed, and the corpus is 18 glides from one writer. Real
glides captured at 10:1.8 and 10:5 are what actually closes this.

## Decode accuracy — data-drive baseline (2026-08-11) 🟡

ADR-0006. The decoder was only ever scored on a controlled corpus; this measures
real input.

- **Controlled corpus** (`corpus-controlled.jsonl`, 18): top-1 **94% (17/18)**.
  Strong recovery from messy greedy (`cpmpurer→computer`, `helo→hello`); the one
  miss (`window`) was a truncated stroke, not decode.
- **`corpus.jsonl` (26) is unreliable** — entries #2–6 (computer/party/glide/
  open/plant) all decode to `river`; 600–770 points each but identical start/end
  ~(0.35,0.17) → duplicated/mislabelled glides. Do not cite its 77%.
- **Real glides (24, live capture):** clearly below 94%. Doubles held
  (`god→good`, `helo→hello`), long words recovered from moderate mess
  (`compiet→computer`, `keybiard→keyboard`). Misses: `coffee→code` ×3,
  `because→bose` ×3, `mouse→mousse` ×2.

**Failure taxonomy (the strategy hinges on this):**
- **Rerank-able** — right word was a candidate, just not #1 (`mouse→mousse`,
  mouse was 2nd). A context re-ranker fixes these.
- **Decode-depth** — right word *absent* from top-5 (`coffee`, `because` on the
  misses). No re-ranker can recover a buried word; needs decode/scoring work.

→ ADR-0006: **decode depth first, then n-gram context rescoring** (the user's
whole-text idea), both as probes. A standing real-glide number is not yet
reported — needs a ≥30-word fresh corpus (the existing ones are too small/corrupt).

## Committed-text deletion is not viable on GNOME Wayland via IBus (2026-08-11) ❌

`ibus_engine_delete_surrounding_text` **SIGABRTs gnome-shell**
(`org.gnome.Shell@wayland: status=6/ABRT`, core-dump) — reproduced on backspace
3×; each crash took down the whole session ("locked out, all apps crashed"). No
OOM, no fault in our process — the shell died.

The NULL-surrounding-text hypothesis was **disproved**: declaring interest at
enable + gating on `set_surrounding_text` still crashed (a glide commit triggers
`set_surrounding_text`, so the gate let the delete through → crash). It is a
deeper gnome-shell Wayland-IM bug.

All three IBus deletion paths are dead here: `delete_surrounding_text` (crashes),
`forward_key_event(BackSpace)` (clients don't apply it), and uinput (target-
mismatched — hits the focused surface, not the IBus commit context, so
inconsistent). **Backspace ships uinput-only** — non-crashing, works for the
immediate glide-then-correct case, imprecise otherwise. Durable fix is a preedit
model (current word stays uncommitted, so correcting it needs no deletion) —
deferred; see ADR-0006 consequences.

**Two consequences of the uinput-only backspace, fixed 2026-08-11:**
- It needs ~17 ms of real sleeps per character (the compositor drops keystrokes
  without them), and it ran on the **Qt UI thread**. Fine while correction only
  touched the last word; recent-word correction (spec §9.3) deletes the word *and
  retypes everything after it*, so correcting an early history entry is 70+
  characters — **over a second of frozen keyboard**.
- uinput writes land immediately while an IBus commit is queued onto the GLib
  thread, so **"commit then backspace" could invert**: glide a word, tap ⌫ at
  once, and the delete reaches the field before the word does.

Both are the same root cause — output had no single owner (ADR-0003). All four
ops (commit / commitExact / typeChar / backspace) now queue onto **one serialized
worker thread** in submission order; the UI thread only enqueues. The worker uses
a new `og_ibus_commit_sync` that waits for the GLib dispatch — fire-and-forget was
the right fix at the wrong layer, and a dedicated output thread can afford to
block, which is what buys back ordering against uinput.

**Preedit support is now probed** (`set_capabilities` → `IBUS_CAP_PREEDIT_TEXT`).
No behaviour change: the diagnostics overlay reports `preedit: YES/no (caps 0x…)`
per focused client, plus the output-queue depth. Whether a preedit text model is
worth building depends on what real apps declare — check a GTK field, a terminal
and an Electron app before deciding.

## KDE / Plasma 6 matrix row (2026-08-14, Fedora 44, second box) 🟡

First hands-on on KDE: Plasma 6 Wayland, app forced to `QT_QPA_PLATFORM=xcb`
per `run.sh`, 16-core laptop, uinput output path only.

### Session IM state — no input method configured at all
`[env] desktop=KDE session=wayland QT_IM_MODULE=(unset) GTK_IM_MODULE=(unset) XMODIFIERS=@im=none`
(the `[env]` line was added in 6425926 for exactly this question). The running
`ibus-daemon` is connected to nothing — so the app's in-process engine cannot
bind (`not connected to ibus-daemon`) and this is **not an app bug**: the session
never wired an IM. Path forward: Plasma System Settings → Keyboard → Virtual
Keyboard → IBus, relogin, and only then does the bind failure become a real bug
worth debugging. Fcitx5 backend (spec Phase 4) is the fallback architecture.

### uinput fallback works end-to-end on KDE
Glide → decode → commit landed in a focused konsole sink (`cat > /tmp/sink`):
ground truth `help te` after a hello→help old-word correction plus a ⌫-hold.
**Output-queue smoke test (3c6b226) PASSES on hardware**: old-word correction
(delete word + suffix, retype) and a 2 s ⌫-hold — no freeze, no reordering.

### Synthetic pointer injection is dead on Plasma 6 — test by hand
Measured, in attempt order:
- **XTEST FakeInput (pointer)**: extension present, events are a no-op — cursor
  never moves. (Text injection via XTEST worked on the GNOME box; pointer does
  not here.)
- **uinput ABS pointer** (ABS_X/Y + BTN_LEFT): device creates, cursor never
  moves — libinput does not support the generic absolute-pointer class.
- **uinput tablet-pen** (BTN_TOOL_PEN + ABS): device creates, ignored by kwin.
- **ydotool**: daemon running, but its device registers REL only (no EV_ABS) —
  absolute moves silently do nothing. Relative events were observed on-device
  but didn't move the cursor in the test window; abandoned as unreliable.
- udev tags both virtual keyboards (`openglide-qt-injector`, ydotoold) with
  `power-switch` (they register `KEY_POWER`); keyboard-event delivery works
  regardless.
Conclusion: automation on Plasma needs libei/portal work; manual driving is the
reliable loop today.

### Pointer slowdown is a no-op on KDE (and was a crash risk off-GNOME)
It writes `org.gnome.desktop.peripherals.*`; no gnome-settings-daemon on KDE and
kwin does not read them. Now schema-gated and default-off (6425926) — which also
fixes the latent abort: `g_settings_new()` kills the process when the schema is
absent, so the old default (level 2) hard-crashed any schema-less box on first
window hover.

### Real-glide latency + accuracy (anecdotal; ADR-0006 territory)
Clean strokes decode in 20–60 ms; sloppy/long strokes hit 300–480 ms
(`hwlo`/`helo`/`wltjf`/`testing` class) with one 1.7 s outlier (`tresitibg`).
Accuracy under sloppy glides is mostly top-1 but re-glides happen (`cifr` ranked
cir/cider above code until a cleaner pass). No decoder changes made on this box —
this is the ADR-0006 corpus story, not a new signal.

### Window focus + positioning on Plasma — the "nothing lands" chain, resolved by override-redirect (c156f5c)

The uinput text that "stopped landing" after the IBus connect fix had a window
cause, not a text cause: **kwin takes focus from a MANAGED xcb window on click
whenever the previous target is a Wayland window** (WindowDoesNotAcceptFocus is
an X hint that only protects X-to-X focus). Every glide's keystrokes went into
the keyboard window itself. Same keyboard + XWayland sink = worked; same
keyboard + Wayland sink = void — which is why it "worked before".

Fix ladder, each rung measured:
- **layer-shell** (`LayerShellQt`, Wayland QPA): keyboard-interactivity none
  DOES fix focus — but **kwin freezes margins at surface creation**: no live
  repositioning, `requestUpdate()` and resize-nudge commits do not move the
  surface (screenshot-diff verified, 311/598/31k-pixel runs). wlroots applies
  margin changes dynamically, so the code path stays for Sway/Hyprland.
- **override-redirect on xcb** (`X11BypassWindowManagerHint`): an OR window is
  unmanaged — `setX/setY` are authoritative (verified: window moves) and clicks
  cannot activate anything (verified: text lands while pressing the keyboard).
  This ships as the KDE default; no user configuration.
- Two QML-side traps fixed along the way: `startSystemMove/Resize` needs a WM
  (silent no-op on OR/layer-shell) → manual drag/resize throughout; and
  **`QCursor::pos()` returns SURFACE-RELATIVE coordinates on Qt Wayland** —
  mixing it with global math clamps every drag to (0,0). Deltas now derive from
  `posX() + mapToItem(mouse)`.

Shipping matrix (run.sh + WindowCtl): GNOME = managed xcb (unchanged, the
long-verified path) · KDE = xcb + override-redirect · wlroots = layer-shell
Wayland (untested on hardware). `OPENGLIDE_QPA` and `OPENGLIDE_OR=0/1` override.

A kwin window rule ("accept focus: no") was trialled during diagnosis and
REJECTED: per-user configuration is not a shipping solution; it was deleted.
Screenshot-diff via `spectacle` + numpy is the standing technique for
compositor-behaviour questions that have no queryable state.

### Chip coherence (user-reported in the wild) — fixed by 6425926, hardware pass pending
Stale words from an earlier target appeared in the chrome slots after switching
targets mid-session; root cause: history validates against the app's mirror, and
the uinput path has no focus signal. 6425926 stamps entries with a target
generation (IBus `focus_in`/`focus_out` where available) plus age caps
(`staleMs`: 20 s on the uinput fallback, 5 min with IBus); non-live entries
leave the slots and all edit paths refuse stale entries. Logic suite: 85
assertions (passing on the dev box; `node` absent on the KDE box — install to
re-run locally).

## How to run on another session
```
cd tools/text-output-probe && make
# focus a text field in some app first; then:
./text-probe --backend im2   --text "blåbær æøå 日本 🫐"   # Wayland primary path
./text-probe --backend xtest --text "blåbær æøå 日本 🫐"   # add --fire to actually type
cd ../surface-probe && make
./surface-probe 6            # click the white window; watch focus_is_probe stays "no"
```
Record per cell: did the string land **exactly**? did focus stay on the target?

## Build prerequisites (fresh machine)
- `gcc`, `wayland-client` dev, `wayland-scanner`, `libx11-dev`, `libxtst-dev`.
- `/dev/uinput` write access (group `input`, or a udev rule).
- IBus backend (GNOME UTF-8 path): `libibus-1.0-dev` — **needs `sudo`**, not yet implemented.
- Qt6 dev packages only needed for the eventual Qt UI wrapper, not for these protocol probes.

## Blocking follow-ups
1. **IBus backend** (`libibus-dev`, sudo) to actually witness UTF-8 commit land in GNOME apps — the one unverified cell on this machine.
2. **Sway / Hyprland / KDE sessions** — KDE measured 2026-08-14 (see the KDE section above); Sway/Hyprland remain open (input-method-v2 + layer-shell).
3. Qt6 dev install for the Qt UI wrapper (optional, later).
