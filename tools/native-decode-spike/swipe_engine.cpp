// SwipeEngine implementation — native port of futo-spike's decoder.
#include "swipe_engine.h"
#include "doubling.h"

#include <executorch/runtime/platform/runtime.h>
#include <executorch/runtime/core/evalue.h>
#include <cstdlib>
#include <executorch/extension/tensor/tensor_ptr.h>
#include <executorch/extension/tensor/tensor_ptr_maker.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
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

static double lae(double a, double b) {  // logaddexp; exact: max + log1p(exp(-|d|))
    if (a <= NEG) return b;
    if (b <= NEG) return a;
    if (a < b) std::swap(a, b);  // a >= b
    return a + std::log1p(std::exp(b - a));
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

SwipeEngine::SwipeEngine(const std::string& model_path, const std::string& dict_path,
                         const std::string& freq_path,
                         const std::string& user_freq_path) {
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
    if (!freq_path.empty()) load_freq(freq_path);   // prior (good>god) — before dict so logfreq is set
    if (!user_freq_path.empty()) { user_freq_path_ = user_freq_path; load_user_freq(user_freq_path); }
    load_dict(dict_path);
    ready_ = true;
    std::fprintf(stderr, "[swipe] ready: %zu dict words\n", n_words_);
}

bool SwipeEngine::set_layout(const std::vector<KeyCenter>& keys) {
    if (keys.size() != 26) return false;
    float tmp[26 * 2];
    bool seen[26] = {false};
    for (const KeyCenter& k : keys) {
        char c = k.label;
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
        if (c < 'a' || c > 'z') return false;
        const int i = c - 'a';
        if (seen[i]) return false;              // duplicate letter — not permitted
        seen[i] = true;
        tmp[i * 2] = k.x;
        tmp[i * 2 + 1] = k.y;
    }
    for (int i = 0; i < 26; i++) if (!seen[i]) return false;   // all 26 required
    std::memcpy(keys_, tmp, sizeof(tmp));
    return true;
}

std::vector<KeyCenter> SwipeEngine::layout() const {
    std::vector<KeyCenter> out;
    out.reserve(26);
    for (int i = 0; i < 26; i++)
        out.push_back({char('a' + i), keys_[i * 2], keys_[i * 2 + 1]});
    return out;
}

void SwipeEngine::bump(const std::string& word) {
    if (word.empty()) return;
    {
        std::lock_guard<std::mutex> lk(user_freq_mu_);
        user_freq_[word] += 1;
    }
    save_user_freq();   // persist immediately — SIGTERM/crash skips the dtor
}

void SwipeEngine::save_user_freq() {
    if (user_freq_path_.empty()) return;
    std::ofstream f(user_freq_path_);
    if (!f) return;
    std::lock_guard<std::mutex> lk(user_freq_mu_);
    for (const auto& kv : user_freq_) f << kv.first << "\t" << kv.second << "\n";
}

void SwipeEngine::load_user_freq(const std::string& path) {
    std::ifstream f(path);
    if (!f) return;
    std::string line;
    while (std::getline(f, line)) {
        auto tab = line.find('\t');
        if (tab == std::string::npos) continue;
        std::string w = line.substr(0, tab);
        long c = std::strtol(line.c_str() + tab + 1, nullptr, 10);
        if (c > 0 && !w.empty()) user_freq_[w] = c;
    }
    std::fprintf(stderr, "[swipe] user freq: %zu words loaded\n", user_freq_.size());
}

void SwipeEngine::load_dict(const std::string& path) {
    std::ifstream f(path);
    root_ = std::make_unique<TrieNode>();
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
        // trie path (prefix-shared CTC decode)
        TrieNode* tn = root_.get();
        for (char c : w) tn = trie_child(tn, (int8_t)(c - 'a'));
        tn->is_word = true;
        auto it = freq_.find(w);
        tn->logfreq = (it != freq_.end()) ? it->second : 0.0;   // unknown dict words -> no prior (rare)
        n_words_++;
    }
}

void SwipeEngine::load_freq(const std::string& path) {
    std::ifstream f(path);
    if (!f) { std::fprintf(stderr, "[swipe] freq list not found (%s); no frequency prior\n", path.c_str()); return; }
    std::string line;
    size_t n = 0;
    while (std::getline(f, line)) {
        auto tab = line.find('\t');
        if (tab == std::string::npos) continue;
        std::string w = line.substr(0, tab);
        for (auto& c : w) c = (char)std::tolower((unsigned char)c);
        double count = std::atof(line.c_str() + tab + 1);
        if (count < 1.0 || w.empty()) continue;
        freq_[w] = std::log(count);
        n++;
    }
    std::fprintf(stderr, "[swipe] freq: %zu words loaded\n", n);
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

SwipeEngine::TrieNode* SwipeEngine::trie_child(TrieNode* p, int8_t letter) {
    for (auto& c : p->children)
        if (c->letter == letter) return c.get();
    auto n = std::make_unique<TrieNode>();
    n->letter = letter;
    TrieNode* raw = n.get();
    p->children.push_back(std::move(n));
    return raw;
}

void SwipeEngine::score_dfs(TrieNode* node, const double* pL, const double* pB,
                            bool hasPL, int8_t pletter, const float* em,
                            const bool* alph, int glen, int maxwlen,
                            std::string& prefix,
                            std::vector<std::pair<float, std::string>>& out) {
    const int8_t letter = node->letter;
    // CTC forward alpha for this node's letter (aL) and the blank after it (aB).
    // Exact port of ctc_score's recurrence, but each node reuses its parent's
    // already-computed prefix alpha (pL=parent letter, pB=parent blank; first-
    // letter nodes get pB=leading-blank alpha B0 and no skip).
    double aL[T_OUT], aB[T_OUT];
    aL[0] = hasPL ? NEG : (double)em[letter];
    for (int t = 1; t < T_OUT; t++) {
        double fp2 = (hasPL && letter != pletter) ? pL[t - 1] : NEG;  // CTC skip
        aL[t] = lae(lae(aL[t - 1], pB[t - 1]), fp2) + em[t * VOCAB + letter];
    }
    aB[0] = NEG;
    for (int t = 1; t < T_OUT; t++)
        aB[t] = lae(aB[t - 1], aL[t - 1]) + em[t * VOCAB + BLANK];
    if (node->is_word) {
        int wlen = (int)prefix.size();
        if (std::abs(wlen - glen) <= 3)
            out.push_back({(float)(lae(aB[T_OUT - 1], aL[T_OUT - 1]) + freq_lambda_ * node->logfreq), prefix});
    }
    if ((int)prefix.size() < maxwlen) {
        for (auto& c : node->children) {
            if (!alph[c->letter]) continue;
            prefix.push_back((char)('a' + c->letter));
            score_dfs(c.get(), aL, aB, true, letter, em, alph, glen, maxwlen, prefix, out);
            prefix.pop_back();
        }
    }
}

// True if `word` is `greedy` with exactly one letter doubled: collapsing one
// adjacent-identical pair in `word` yields `greedy`. The glide passes a doubled
// key once so the model emits a single letter — this recovers good<-god, hello<-helo.

const char* DecodeDiag::verdict() const {
    if (target.empty())  return "";
    if (rank == 0)       return "won";
    if (!in_dict)        return "not-in-dict";
    if (!alph_ok)        return "alph-pruned";
    if (!len_ok)         return "length-pruned";
    if (was_scored)      return "out-ranked";
    return "unscored";
}

std::vector<Candidate> SwipeEngine::decode(const std::vector<SwipePoint>& pts, std::string* greedy_out,
                                           DecodeDiag* diag) {
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
        std::partial_sort(idx, idx + alph_topk_, idx + 26, [&](int a, int b) { return row[a] > row[b]; });
        for (int k = 0; k < alph_topk_; k++) alph[idx[k]] = true;
    }
    if (greedy_out) *greedy_out = g;

    int glen = (int)g.size();
    int maxwlen = glen + 3;
    // Leading-blank CTC alpha (B0): the only label reachable at t=0 alongside the
    // first letter; it only stays (no prior label to advance from).
    double aB0[T_OUT];
    aB0[0] = em[BLANK];
    for (int t = 1; t < T_OUT; t++) aB0[t] = aB0[t - 1] + em[t * VOCAB + BLANK];
    std::vector<std::pair<float, std::string>> scored;
    std::string prefix;
    for (auto& c : root_->children) {
        if (!alph[c->letter]) continue;
        prefix.assign(1, (char)('a' + c->letter));
        score_dfs(c.get(), nullptr, aB0, false, (int8_t)-1, em, alph, glen, maxwlen, prefix, scored);
    }
    // Double-letter recovery: boost candidates that are the greedy with one
    // letter doubled (good<-god, hello<-helo) — the targeted fix the frequency
    // prior can't provide (e.g. "help" beats "hello" on both CTC and frequency).
    for (auto& s : scored) {
        const int d = count_doublings(s.second, g);
        if (d >= 1 && d <= max_doublings_) s.first += double_bonus_ * float(d);
    }
    // Personalization: favor words the user actually uses. Snapshot under lock
    // (bump() runs on the UI thread) + add user_lambda*log(count+1) before ranking.
    std::unordered_map<std::string, long> usnap;
    { std::lock_guard<std::mutex> lk(user_freq_mu_); usnap = user_freq_; }
    if (!usnap.empty()) {
        for (auto& s : scored) {
            auto it = usnap.find(s.second);
            if (it != usnap.end()) s.first += (float)(user_lambda_ * std::log((double)it->second + 1.0));
        }
    }
    std::sort(scored.begin(), scored.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });

    // ---- ADR-0006 F2: why did the expected word not win? --------------------
    // The prune conditions mirrored here MUST track score_dfs()/the walk above:
    // a word is scored iff every letter is in `alph`, its length <= maxwlen, and
    // |len - glen| <= 3. If those change, change these.
    if (diag && !diag->target.empty()) {
        const std::string& tw = diag->target;
        diag->greedy = g;
        diag->greedy_len = glen;
        diag->max_word_len = maxwlen;
        diag->doublings = count_doublings(tw, g);

        const TrieNode* n = root_.get();
        bool path = true;
        for (char c : tw) {
            if (c < 'a' || c > 'z') { path = false; break; }
            const TrieNode* nx = nullptr;
            for (const auto& ch : n->children)
                if (ch->letter == (int8_t)(c - 'a')) { nx = ch.get(); break; }
            if (!nx) { path = false; break; }
            n = nx;
        }
        diag->in_dict = path && n->is_word;

        diag->alph_ok = true;
        for (char c : tw) {
            if (c < 'a' || c > 'z' || !alph[c - 'a']) {
                diag->alph_ok = false;
                diag->alph_blocker = c;
                break;
            }
        }
        const int wl = (int)tw.size();
        diag->len_ok = (wl <= maxwlen) && (std::abs(wl - glen) <= 3);

        for (int i = 0; i < (int)scored.size(); i++) {
            if (scored[i].second == tw) {
                diag->was_scored = true;
                diag->rank = i;
                diag->score = scored[i].first;
                break;
            }
        }
        diag->top_score = scored.empty() ? 0.0f : scored[0].first;
    }

    for (int i = 0; i < 5 && i < (int)scored.size(); i++)
        result.push_back({scored[i].second, scored[i].first});
    return result;
}
