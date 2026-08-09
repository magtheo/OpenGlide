// SwipeEngine implementation — native port of futo-spike's decoder.
#include "swipe_engine.h"

#include <executorch/runtime/platform/runtime.h>
#include <executorch/runtime/core/evalue.h>
#include <executorch/runtime/core/exec_aten/util/scalar_type_util.h>
#include <executorch/extension/tensor/tensor_ptr.h>
#include <executorch/extension/tensor/tensor_ptr_maker.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

using ::executorch::runtime::Error;
using ::executorch::runtime::EValue;
using ::executorch::runtime::runtime_init;
using ::executorch::extension::from_blob;
using ::executorch::extension::Module;
using ::executorch::aten::ScalarType;

static constexpr int T_IN = 64;     // resample length
static constexpr int T_OUT = 32;    // emission timesteps
static constexpr int VOCAB = 65;    // 26 letters + blank@64
static constexpr int BLANK = 64;
static constexpr double NEG = -1e18;

static double lae(double a, double b) {  // logaddexp
    if (a <= NEG) return b;
    if (b <= NEG) return a;
    double m = std::max(a, b);
    return m + std::log(std::exp(a - m) + std::exp(b - m));
}
static float interp1(float q, const std::vector<float>& x, const std::vector<float>& y) {
    int n = (int)x.size();
    if (q <= x[0]) return y[0];
    if (q >= x[n - 1]) return y[n - 1];
    int lo = 0, hi = n - 1;
    while (hi - lo > 1) { int mid = (lo + hi) / 2; (x[mid] <= q) ? lo = mid : hi = mid; }
    float f = (q - x[lo]) / (x[hi] - x[lo]);
    return y[lo] + f * (y[hi] - y[lo]);
}

SwipeEngine::SwipeEngine(const std::string& model_path, const std::string& dict_path) {
    // QWERTY key centres in a..z order (validate.py QWERTY, LETTERS=sorted).
    static const float C[26][2] = {
        {0.10f, 0.500f}, {0.60f, 0.833f}, {0.40f, 0.833f}, {0.30f, 0.500f}, {0.25f, 0.167f},
        {0.40f, 0.500f}, {0.50f, 0.500f}, {0.60f, 0.500f}, {0.75f, 0.167f}, {0.70f, 0.500f},
        {0.80f, 0.500f}, {0.90f, 0.500f}, {0.80f, 0.833f}, {0.70f, 0.833f}, {0.85f, 0.167f},
        {0.95f, 0.167f}, {0.05f, 0.167f}, {0.35f, 0.167f}, {0.20f, 0.500f}, {0.45f, 0.167f},
        {0.65f, 0.167f}, {0.50f, 0.833f}, {0.15f, 0.167f}, {0.30f, 0.833f}, {0.55f, 0.167f}, {0.20f, 0.833f},
    };
    for (int i = 0; i < 26; i++) { keys_[i * 2] = C[i][0]; keys_[i * 2 + 1] = C[i][1]; mask_[i] = true; }
    for (int i = 26; i < 64; i++) mask_[i] = false;

    runtime_init();
    mod_ = std::make_unique<Module>(model_path);
    if (mod_->load() != Error::Ok) { std::fprintf(stderr, "[swipe] model load failed\n"); return; }
    load_dict(dict_path);
    ready_ = true;
    std::fprintf(stderr, "[swipe] ready: %zu dict words\n", dict_.size());
}

void SwipeEngine::load_dict(const std::string& path) {
    std::ifstream f(path);
    std::string w;
    while (std::getline(f, w)) {
        // trim
        size_t a = w.find_first_not_of(" \t\r\n"), b = w.find_last_not_of(" \t\r\n");
        if (a == std::string::npos) continue;
        w = w.substr(a, b - a + 1);
        for (auto& c : w) c = (char)std::tolower((unsigned char)c);
        if (w.size() < 2 || w.size() > 12) continue;
        bool ok = true;
        for (char c : w) if (c < 'a' || c > 'z') { ok = false; break; }
        if (!ok) continue;
        DictWord d;
        d.s = w; d.len = (int)w.size();
        d.labels.push_back(BLANK);
        for (char c : w) { d.labels.push_back(c - 'a'); d.labels.push_back(BLANK); }
        d.skip.resize(d.labels.size(), 0);
        for (size_t i = 2; i < d.labels.size(); i++) d.skip[i] = (d.labels[i] != d.labels[i - 2]);
        int seen[26] = {0};
        for (char c : w) seen[c - 'a'] = 1;
        for (int i = 0; i < 26; i++) if (seen[i]) d.idxset.push_back(i);
        dict_.push_back(std::move(d));
    }
}

static void resample_into(const std::vector<SwipePoint>& pts, float out[2 * T_IN]) {
    int n = (int)pts.size();
    std::vector<float> x(n), y(n), t(n);
    for (int i = 0; i < n; i++) { x[i] = pts[i].x; y[i] = pts[i].y; t[i] = pts[i].t; }
    float t0 = t[0];
    for (auto& v : t) v -= t0;
    if (t[n - 1] > 1e-3f) {
        int n60 = std::max(2, (int)std::round(t[n - 1] / (1000.0f / 60.0f)) + 1);
        std::vector<float> tt(n60), nx(n60), ny(n60), ar(n);
        for (int i = 0; i < n; i++) ar[i] = (float)i;
        for (int i = 0; i < n60; i++) tt[i] = (float)i / (n60 - 1) * t[n - 1];
        for (int i = 0; i < n60; i++) { nx[i] = interp1(tt[i], t, x); ny[i] = interp1(tt[i], t, y); }
        x = nx; y = ny; n = n60;
        (void)ar;
    }
    std::vector<float> ar(n);
    for (int i = 0; i < n; i++) ar[i] = (float)i;
    for (int i = 0; i < T_IN; i++) {
        float q = (float)i / (T_IN - 1) * (n - 1);
        out[i] = interp1(q, ar, x);
        out[T_IN + i] = interp1(q, ar, y);
    }
}

const float* SwipeEngine::run_forward(const float feats[2 * 64]) {
    std::vector<int32_t> sf{1, 2, 64}, sk{1, 64, 2}, sm{1, 64};
    auto t0 = from_blob((void*)feats, sf, ScalarType::Float);
    auto t1 = from_blob((void*)keys_, sk, ScalarType::Float);
    auto t2 = from_blob((void*)mask_, sm, ScalarType::Bool);
    std::vector<EValue> inputs;
    inputs.push_back(*t0);
    inputs.push_back(*t1);
    inputs.push_back(*t2);
    auto r = mod_->execute("forward", inputs);
    if (!r.ok()) return nullptr;
    auto& outs = r.get();
    if (outs.empty() || !outs[0].isTensor()) return nullptr;
    return outs[0].toTensor().template const_data_ptr<float>();
}

static double ctc_score(const float* em, const std::vector<int>& labels, const std::vector<unsigned char>& skip) {
    int L = (int)labels.size();
    std::vector<double> prev(L, NEG), cur(L);
    prev[0] = em[labels[0]];
    if (L > 1) prev[1] = em[labels[1]];
    for (int t = 1; t < T_OUT; t++) {
        for (int i = 0; i < L; i++) {
            double stay = prev[i];
            double p1 = (i >= 1) ? prev[i - 1] : NEG;
            double p2 = (i >= 2 && skip[i]) ? prev[i - 2] : NEG;
            cur[i] = lae(lae(stay, p1), p2) + em[t * VOCAB + labels[i]];
        }
        std::swap(prev, cur);
    }
    double total = prev[L - 1];
    if (L > 1) total = lae(total, prev[L - 2]);
    return total;
}

std::vector<Candidate> SwipeEngine::decode(const std::vector<SwipePoint>& pts, std::string* greedy_out) {
    std::vector<Candidate> result;
    if (!ready_ || pts.size() < 4) return result;
    // Overshoot adapter (§6.3): drop trailing out-of-bounds points (mouse momentum
    // past the last key) and clamp the rest to [0,1] (mid overshoot -> edge).
    int end = (int)pts.size();
    while (end > 1 && (pts[end - 1].x < 0.f || pts[end - 1].x > 1.f ||
                       pts[end - 1].y < 0.f || pts[end - 1].y > 1.f)) end--;
    std::vector<SwipePoint> a;
    a.reserve(end);
    for (int i = 0; i < end; i++) {
        SwipePoint p = pts[i];
        p.x = std::min(1.f, std::max(0.f, p.x));
        p.y = std::min(1.f, std::max(0.f, p.y));
        a.push_back(p);
    }
    float feats[2 * 64];
    resample_into(a, feats);
    const float* em = run_forward(feats);
    if (!em) return result;

    static const char* L = "abcdefghijklmnopqrstuvwxyz";
    std::string g; int prev = -1; bool alph[26] = {false};
    for (int t = 0; t < T_OUT; t++) {
        const float* row = em + t * VOCAB;
        int c = 0; for (int v = 1; v < VOCAB; v++) if (row[v] > row[c]) c = v;
        if (c != prev && c != BLANK && c < 26) g.push_back(L[c]);
        prev = c;
        int idx[26]; for (int v = 0; v < 26; v++) idx[v] = v;
        std::partial_sort(idx, idx + 3, idx + 26, [&](int a, int b) { return row[a] > row[b]; });
        for (int k = 0; k < 3; k++) alph[idx[k]] = true;
    }
    if (greedy_out) *greedy_out = g;

    int glen = (int)g.size();
    std::vector<std::pair<float, std::string>> scored;
    for (const auto& d : dict_) {
        if (std::abs(d.len - glen) > 3) continue;
        bool subset = true;
        for (int idx : d.idxset) if (!alph[idx]) { subset = false; break; }
        if (!subset) continue;
        scored.push_back({(float)ctc_score(em, d.labels, d.skip), d.s});
    }
    std::sort(scored.begin(), scored.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });
    for (int i = 0; i < 5 && i < (int)scored.size(); i++)
        result.push_back({scored[i].second, scored[i].first});
    return result;
}
