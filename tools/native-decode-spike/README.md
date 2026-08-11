# native-decode-spike — native C++ FUTO SwipeEngine (replaces futo_server.py)

ExecuTorch C++ runtime running the FUTO `honorable_sturgeon/model_fp32.pte`,
with a full native decode pipeline (resample → encoder `forward` → greedy CTC +
pruned dictionary CTC top-5). No Python, no IPC. This is the engine the Qt
prototype links; it removes the ~8 s warmup and cuts per-decode latency ~8×.

## Why ExecuTorch from source
The `.pte` is **XNNPACK-delegated** (~40 delegate partitions), so it won't run on
a portable-only build. There's no source PyTorch model to re-export to ONNX, and
`swipe-library` would also need ExecuTorch — so building the ExecuTorch C++
runtime (with `EXECUTORCH_BUILD_XNNPACK=ON`) is the only viable native path.

## Build
```
# one-time: clone executorch + the build-critical submodules
git clone --depth 1 -b v1.4.0 https://github.com/pytorch/executorch.git ../../third_party/executorch
cd ../../third_party/executorch && git submodule update --init --depth 1 \
  third-party/{flatcc,flatbuffers,json,gflags} shim \
  backends/xnnpack/third-party/{XNNPACK,FP16,FXdiv,cpuinfo,pthreadpool}
# cmake 4.x (venv: pip install cmake>=3.29); venv python provides torchgen+pyyaml for codegen
../../tools/futo-spike/.venv/bin/cmake -B build -S . -DCMAKE_BUILD_TYPE=Release \
  -DPYTHON_EXECUTABLE=$PWD/../../tools/futo-spike/.venv/bin/python
../../tools/futo-spike/.venv/bin/cmake --build build -j4
```

## Targets
- `spike` — replays Python's exact input tensors, runs native `forward`, asserts
  greedy == `computer` and `max|diff| < 1e-3` vs Python. (The risk gate.)
- `swipe_engine` — static lib: the reusable native decoder.
- `corpus_test` — accuracy + latency over a swipe corpus, plus `--sweep`: the
  ADR-0005 aspect-ratio pre-check (see below).

## Run
```
.venv/bin/python dump_io.py /tmp/spike_io     # Python's reference tensors + output
MODEL=$(ls ../../tools/futo-spike/models/hub/models--futo-org--futo-swipe/snapshots/*/honorable_sturgeon/model_fp32.pte)
./build/spike "$MODEL" /tmp/spike_io           # → PASS (greedy computer, diff ~4e-6)
.venv/bin/python dump_corpus.py ../../tools/futo-spike/corpus-controlled.jsonl /tmp/corpus_flat.txt
./build/corpus_test "$MODEL" /usr/share/dict/american-english /tmp/corpus_flat.txt
```

### Aspect-ratio band (ADR-0005 step 4)
The window now resizes freely, so the letter block is no longer pinned at 10:3.
How far it can drift before decode degrades is an open verification gate:
```
./build/corpus_test "$MODEL" /usr/share/dict/american-english /tmp/corpus_flat.txt --sweep
./build/corpus_test "$MODEL" /usr/share/dict/american-english /tmp/corpus_flat.txt --aspect 10:2.2
```
`--sweep` re-projects each recorded glide onto blocks from 10:5 to 10:1.8 and
prints top-1 per aspect; clamp the window's resize range to the rows that hold
within noise of the 10:3 baseline.

**What the re-projection does and does not model.** Key centres are at fixed
*normalized* positions and the user aims at keys, so the intended path is
unchanged by a resize; what changes is what physical mouse momentum produces —
wobble and overshoot — because normalizing by a shorter block magnifies it in y.
So `reaspect()` splits y into a locally-straight component and a deviation, and
scales only the deviation by `k = target_aspect / (10/3)`. It assumes the user
re-aims perfectly and does not change speed, and its high-pass is crude. **It is
a pre-check that narrows where to look, not the gate** — real glides captured at
the edge aspects close that.

## Verified (2026-08-09)
- spike: native `forward` == Python to **3.8e-6**; greedy `computer`. **PASS**
- corpus (controlled, 18 glides): **top-1 17/18 (94%)** — identical to the Python
  decoder (same lone too-short-stroke miss).
- latency: model load **0.1 ms** (was ~8 s warmup); encoder `forward` **7 ms**;
  dict decode avg **~79 ms**, worst-case ~350 ms (was ~1350 ms / 919 ms outlier).
  Decode is a prefix-shared **trie CTC** forward — one DFS over the dictionary
  trie, each node reusing its parent's α (exact: byte-identical ranking to
  per-word scoring, ~1.8× faster).

## Notes
- The `.pte` returns 3 outputs; emissions are `output[0]` = `[1,32,65]`
  (32 timesteps × 65 vocab: 26 letters + blank@64).
- The model could also be `magic_macaw`; this targets `honorable_sturgeon`.
