// Native inference spike: load the FUTO .pte with the ExecuTorch C++ runtime,
// replay the exact input tensors Python produced (feats/keys/mask), run forward,
// greedy-decode the [T,65] log-emissions, and compare element-wise to Python's
// output. Pass: greedy == "computer" AND max|diff| < 1e-3.
#include <executorch/runtime/platform/runtime.h>
#include <executorch/runtime/platform/log.h>
#include <executorch/runtime/core/evalue.h>
#include <executorch/runtime/core/exec_aten/util/scalar_type_util.h>
#include <executorch/extension/module/module.h>
#include <executorch/extension/tensor/tensor_ptr.h>
#include <executorch/extension/tensor/tensor_ptr_maker.h>

#include <cstdio>
#include <cstdint>
#include <chrono>
#include <string>
#include <vector>

using ::executorch::runtime::Error;
using ::executorch::runtime::EValue;
using ::executorch::runtime::runtime_init;
using ::executorch::extension::from_blob;
using ::executorch::extension::Module;
using ::executorch::extension::TensorPtr;
using ::executorch::aten::ScalarType;

static std::vector<uint8_t> readfile(const std::string& p) {
    FILE* f = std::fopen(p.c_str(), "rb");
    if (!f) { std::fprintf(stderr, "cannot open %s\n", p.c_str()); std::exit(1); }
    std::fseek(f, 0, SEEK_END); long n = std::ftell(f); std::fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> b(n);
    if (n > 0 && std::fread(b.data(), 1, (size_t)n, f) != (size_t)n) {
        std::fprintf(stderr, "short read on %s\n", p.c_str());
    }
    std::fclose(f);
    return b;
}

int main(int argc, char** argv) {
    const char* model = argc > 1 ? argv[1] : "model_fp32.pte";
    const std::string io = argc > 2 ? argv[2] : "/tmp/spike_io";
    std::printf("[spike] model=%s io=%s\n", model, io.c_str());

    runtime_init();
    auto tl0 = std::chrono::steady_clock::now();
    Module mod(model);
    if (mod.load() != Error::Ok) { std::fprintf(stderr, "[spike] load() failed\n"); return 1; }
    std::printf("[spike] model load: %.1f ms\n", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tl0).count());

    auto fb = readfile(io + "/feats.bin");
    auto kb = readfile(io + "/keys.bin");
    auto mb = readfile(io + "/mask.bin");
    if (fb.size() != 1*2*64*4 || kb.size() != 1*64*2*4 || mb.size() != 1*64) {
        std::fprintf(stderr, "[spike] input size mismatch\n"); return 1;
    }

    std::vector<int32_t> sf{1, 2, 64}, sk{1, 64, 2}, sm{1, 64};
    TensorPtr t0 = from_blob(fb.data(), sf, ScalarType::Float);
    TensorPtr t1 = from_blob(kb.data(), sk, ScalarType::Float);
    TensorPtr t2 = from_blob(mb.data(), sm, ScalarType::Bool);

    std::vector<EValue> inputs;
    inputs.push_back(*t0);
    inputs.push_back(*t1);
    inputs.push_back(*t2);

    auto te0 = std::chrono::steady_clock::now();
    auto r = mod.execute("forward", inputs);
    std::printf("[spike] execute: %.2f ms\n", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - te0).count());
    if (!r.ok()) { std::fprintf(stderr, "[spike] execute failed: %d\n", (int)r.error()); return 1; }
    auto& outs = r.get();
    std::printf("[spike] %zu outputs:\n", outs.size());
    int em_idx = -1; long em_elems = 0;
    for (size_t i = 0; i < outs.size(); i++) {
        if (!outs[i].isTensor()) { std::printf("  [%zu] non-tensor (tag=%d)\n", i, (int)outs[i].tag); continue; }
        auto t = outs[i].toTensor();
        std::printf("  [%zu] ndim=%d sizes=[", i, (int)t.dim());
        long elems = 1; bool has65 = false;
        for (int d = 0; d < (int)t.dim(); d++) {
            std::printf("%ld%s", (long)t.size(d), d + 1 < (int)t.dim() ? "," : "");
            elems *= (long)t.size(d); if ((long)t.size(d) == 65) has65 = true;
        }
        std::printf("] dtype=%d nbytes=%zu\n", (int)t.scalar_type(), t.nbytes());
        if (has65 && t.scalar_type() == ScalarType::Float) { em_idx = (int)i; em_elems = elems; }
    }
    if (em_idx < 0) { std::fprintf(stderr, "[spike] no [.,65] float emissions output found\n"); return 2; }
    auto outT = outs[em_idx].toTensor();
    int T = (int)outT.size(outT.dim() - 2), V = (int)outT.size(outT.dim() - 1);
    const float* em = outT.template const_data_ptr<float>();
    std::printf("[spike] emissions = output[%d]: T=%d V=%d (%ld elems)\n", em_idx, T, V, em_elems);

    static const char* LETTERS = "abcdefghijklmnopqrstuvwxyz";
    int blank = V - 1; std::string g; int prev = -1;
    for (int t = 0; t < T; t++) {
        int c = 0;
        for (int v = 1; v < V; v++) if (em[t * V + v] > em[t * V + c]) c = v;
        if (c != prev && c != blank && c < 26) g.push_back(LETTERS[c]);
        prev = c;
    }
    std::printf("[spike] CPP greedy: %s\n", g.c_str());

    auto pb = readfile(io + "/out_py.bin");
    long N = em_elems, M = (long)pb.size() / 4;
    bool ok = false;
    if (M == N) {
        const float* py = (const float*)pb.data();
        double maxd = 0;
        for (long i = 0; i < N; i++) { double d = std::fabs((double)em[i] - (double)py[i]); if (d > maxd) maxd = d; }
        ok = maxd < 1e-3;
        std::printf("[spike] max|diff| vs Python: %.3e over %ld elems  -> %s\n",
                    maxd, N, ok ? "MATCH (native forward == Python)" : "MISMATCH");
    } else {
        std::printf("[spike] size mismatch: py=%ld floats vs cpp=%ld\n", M, N);
    }
    std::printf("[spike] VERDICT: %s\n", (ok && g == "computer") ? "PASS" : "FAIL");
    return (ok && g == "computer") ? 0 : 2;
}
