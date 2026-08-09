// SwipeEngine — native C++ FUTO swipe decoder. Loads the ExecuTorch .pte once
// and decodes a glide trajectory to ranked dictionary candidates, with no Python
// and no IPC. Mirrors the validated futo-spike decoder: resample(64) -> encoder
// forward -> greedy CTC + pruned dictionary CTC top-5. (Replaces futo_server.py.)
#pragma once
#include <executorch/extension/module/module.h>
#include <memory>
#include <string>
#include <vector>

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
    struct DictWord {
        std::string s;
        std::vector<int> labels;     // CTC label sequence (with BLANKs)
        std::vector<unsigned char> skip; // skip[i] = labels[i]!=labels[i-2]
        std::vector<int> idxset;     // unique letter indices (for subset filter)
        int len;
    };

    std::unique_ptr<executorch::extension::Module> mod_;
    std::vector<DictWord> dict_;
    bool ready_ = false;

    // constant key-centers tensor data [64][2] + mask [64]
    float keys_[64 * 2];
    bool mask_[64];

    void load_dict(const std::string& path);
    const float* run_forward(const float feats[2 * 64]);  // -> [T*65] emissions (owned in mod_)
};
