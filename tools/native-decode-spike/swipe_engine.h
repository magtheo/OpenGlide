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

class SwipeEngine {
public:
    // model_path: .../honorable_sturgeon/model_fp32.pte ; dict_path: american-english
    // freq_path: optional word<TAB>count list for the frequency prior (good>god).
    // user_freq_path: optional persistent per-user word counts (personalization).
    SwipeEngine(const std::string& model_path, const std::string& dict_path,
                const std::string& freq_path = "",
                const std::string& user_freq_path = "");
    bool ready() const { return ready_; }
    // Weight of the word-frequency prior (score += lambda * log(count)); 0 = pure CTC.
    void set_freq_lambda(double l) { freq_lambda_ = l; }
    // Personalization: record that the user used/chose `word` (bumps its count),
    // so future decodes favor their vocabulary. Thread-safe.
    void bump(const std::string& word);
    // Persist the per-user word counts (call at shutdown).
    void save_user_freq();

    // Decode one glide -> up to 5 ranked candidates. greedy_out (optional) gets
    // the raw greedy string. Returns empty on failure.
    std::vector<Candidate> decode(const std::vector<SwipePoint>& pts,
                                  std::string* greedy_out = nullptr);

private:
    std::unique_ptr<executorch::extension::Module> mod_;
    size_t n_words_ = 0;          // dict size (trie word-ends), for the ready log
    double freq_lambda_ = 0.0;      // frequency prior weight (0=off — net-negative: e.g. help>hello)
    std::unordered_map<std::string, double> freq_;   // word -> log(count) (frequency prior)
    // --- Personalization: per-user word counts (score += user_lambda * log(count+1)) ---
    std::unordered_map<std::string, long> user_freq_;
    std::mutex user_freq_mu_;
    double user_lambda_ = 2.0;       // prior weight for the user's own words
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
