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

struct SwipePoint { float x, y, t; };          // t in ms
struct Candidate { std::string text; float score; };

class SwipeEngine {
public:
    // model_path: .../honorable_sturgeon/model_fp32.pte ; dict_path: american-english
    SwipeEngine(const std::string& model_path, const std::string& dict_path);
    bool ready() const { return ready_; }

    // Decode one glide -> up to 5 ranked candidates. greedy_out (optional) gets
    // the raw greedy string. Returns empty on failure.
    std::vector<Candidate> decode(const std::vector<SwipePoint>& pts,
                                  std::string* greedy_out = nullptr);

private:
    std::unique_ptr<executorch::extension::Module> mod_;
    size_t n_words_ = 0;          // dict size (trie word-ends), for the ready log

    // --- Prefix-shared CTC decode over a dictionary trie ---
    struct TrieNode {
        int8_t letter = -1;          // 0-25; -1 for root
        bool is_word = false;
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
    // trie CTC forward (exact port of ctc_score, prefix-shared): walks the trie
    // computing each node's letter/blank alpha from its parent's, pruned to alph.
    const float* run_forward(const float feats[2 * 64]);  // -> [T*65] emissions (owned in mod_)
};
