// SwipeEngine — native C++ FUTO swipe decoder. Loads the ExecuTorch .pte once
// and decodes a glide trajectory to ranked dictionary candidates, with no Python
// and no IPC. Mirrors the validated futo-spike decoder: resample(64) -> encoder
// forward -> greedy CTC + pruned dictionary CTC top-5. (Replaces futo_server.py.)
#pragma once
#include <executorch/extension/module/module.h>
#include <memory>
#include <string>
#include <vector>
#include <utility>
#include <unordered_map>
#include <mutex>

struct SwipePoint { float x, y, t; };          // t in ms
struct Candidate { std::string text; float score; };
// One key of the glide geometry, normalized to the letter block (data-formats.md
// `languages/<lang>/layout.json`). The decoder must score against the keys that
// are actually on screen (spec §7.2) — hence set_layout/layout below.
struct KeyCenter { char label; float x, y; };

// Why a known-correct word did not win — ADR-0006's F2 taxonomy turned from a
// guess into a measurement.
//
// F2 splits misses into "rerank-able" (the word was a candidate, just not #1) and
// "buried" (absent from the candidate set entirely), and notes that no re-ranker
// can recover a buried word. But "buried" has three quite different causes with
// three different fixes, and guessing between them is how you end up widening a
// beam to fix something that was really a length constant. So: pass a target to
// decode() and it reports which prune (if any) removed the word, or its actual
// rank and score if it was there and lost.
struct DecodeDiag {
    std::string target;        // IN: the word we expected. Empty = no tracing.
    // ---- OUT ----
    std::string greedy;
    int   greedy_len   = 0;
    int   max_word_len = 0;    // trie-walk depth cap (greedy_len + 3)
    bool  in_dict      = false;// present in the loaded lexicon at all
    bool  alph_ok      = false;// every letter survived the per-timestep top-3 prune
    char  alph_blocker = 0;    // first letter that did not; 0 if none
    bool  len_ok       = false;// inside |wordlen - greedylen| <= 3 and <= max_word_len
    bool  was_scored   = false;// survived every prune and received a CTC score
    int   rank         = -1;   // 0-based rank among ALL scored candidates; -1 if absent
    float score        = 0.0f; // its final score (after bonuses)
    float top_score    = 0.0f; // the winner's
    int   doublings    = -1;   // pairs to collapse to reach greedy; -1 = not a doubling of it
    // "won" | "not-in-dict" | "alph-pruned" | "length-pruned" | "out-ranked"
    const char* verdict() const;
};

class SwipeEngine {
public:
    // model_path: .../honorable_sturgeon/model_fp32.pte ; dict_path: american-english
    // freq_path: optional word<TAB>count list for the frequency prior (good>god).
    // user_freq_path: optional persistent per-user word counts (personalization).
    SwipeEngine(const std::string& model_path, const std::string& dict_path,
                const std::string& freq_path = "",
                const std::string& user_freq_path = "");
    bool ready() const { return ready_; }

    // Install the keyboard geometry the decoder scores against (spec §7.2). Needs
    // exactly the 26 letters a-z, once each (duplicates are a FUTO constraint —
    // data-formats.md); on any violation the previous geometry is kept and this
    // returns false, so a malformed layout.json degrades to the built-in QWERTY
    // rather than silently scoring against a broken keyboard.
    bool set_layout(const std::vector<KeyCenter>& keys);
    // The geometry currently in force, a-z order. This is the single runtime
    // source of truth — the UI reads it back rather than keeping its own copy.
    std::vector<KeyCenter> layout() const;

    // Weight of the word-frequency prior (score += lambda * log(count)); 0 = pure CTC.
    void set_freq_lambda(double l) { freq_lambda_ = l; }
    // Personalization: record that the user used/chose `word` (bumps its count),
    // so future decodes favor their vocabulary. Thread-safe.
    void bump(const std::string& word);
    // Persist the per-user word counts (call at shutdown).
    void save_user_freq();

    // Decode one glide -> up to 5 ranked candidates. greedy_out (optional) gets
    // the raw greedy string. `diag` (optional) traces why diag->target lost.
    // Returns empty on failure.
    std::vector<Candidate> decode(const std::vector<SwipePoint>& pts,
                                  std::string* greedy_out = nullptr,
                                  DecodeDiag* diag = nullptr);

    // Double-letter recovery: a glide crosses a doubled key once, so greedy emits
    // a single letter and the true word is CTC-penalised. Candidates that ARE the
    // greedy with n pairs doubled get n * bonus.
    //
    // `max` used to be hard-capped at 1, which made words needing two doublings
    // structurally unreachable no matter how the search was widened — `coffee`
    // (ff AND ee, greedy "cofe") could never receive the bonus at all. That is a
    // scoring cap, not a search-depth problem, and ADR-0006 attributes it to the
    // wrong lever. Tunable so the two can be A/B'd on the corpus.
    void set_double_bonus(float b) { double_bonus_ = b; }
    void set_max_doublings(int n)  { max_doublings_ = n < 0 ? 0 : n; }

private:
    std::unique_ptr<executorch::extension::Module> mod_;
    size_t n_words_ = 0;          // dict size (trie word-ends), for the ready log
    double freq_lambda_ = 0.0;      // frequency prior weight (0=off — net-negative: e.g. help>hello)
    std::unordered_map<std::string, double> freq_;   // word -> log(count) (frequency prior)
    // --- Personalization: per-user word counts (score += user_lambda * log(count+1)) ---
    std::unordered_map<std::string, long> user_freq_;
    std::mutex user_freq_mu_;
    double user_lambda_ = 2.0;       // prior weight for the user's own words
    float  double_bonus_  = 4.5f;    // per collapsed pair (see set_double_bonus)
    int    max_doublings_ = 2;       // 1 = the old behaviour
    std::string user_freq_path_;

    // --- Prefix-shared CTC decode over a dictionary trie ---
    struct TrieNode {
        int8_t letter = -1;          // 0-25; -1 for root
        bool is_word = false;
        double logfreq = 0.0;       // word-frequency prior (log count); word-end only
        std::vector<std::unique_ptr<TrieNode>> children;  // find-or-create by letter
    };
    std::unique_ptr<TrieNode> root_;
    TrieNode* trie_child(TrieNode* parent, int8_t letter);   // find-or-create
    void score_dfs(TrieNode* node, const double* pL, const double* pB,
                   bool hasPL, int8_t pletter, const float* em, const bool* alph,
                   int glen, int maxwlen, std::string& prefix,
                   std::vector<std::pair<float, std::string>>& out);
    bool ready_ = false;

    // constant key-centers tensor data [64][2] + mask [64]
    float keys_[64 * 2];
    bool mask_[64];

    void load_dict(const std::string& path);
    void load_freq(const std::string& path);    // word<TAB>count -> freq_
    void load_user_freq(const std::string& path);   // word<TAB>count -> user_freq_
    // trie CTC forward (exact port of ctc_score, prefix-shared): walks the trie
    // computing each node's letter/blank alpha from its parent's, pruned to alph.
    const float* run_forward(const float feats[2 * 64]);  // -> [T*65] emissions (owned in mod_)
};
