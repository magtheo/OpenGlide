// context_rescore.h — ADR-0006 layer 2: re-rank a glide's own candidates using
// the previous committed word. Split out so it can be tested without
// ExecuTorch or the model (see context_rescore_test.cpp), same pattern as
// doubling.h.
//
// Why this is safe from the frequency-prior trap (RESULTS.md: a plain
// unigram prior picked "help" over "hello" always, because help is ~180x more
// frequent in raw text — freq_lambda_ ships at 0.0 because of exactly this).
// A bigram is conditioned on the previous word, so "say hello" (649k) beats
// "say help" (unseen) while "need help" (3.4M) beats "need hello" (unseen) —
// the two contexts disagree, which a context-blind prior structurally cannot
// express. Source: Norvig's count_2w.txt (count_2w.txt in this directory).
//
// Why raw counts (add-1 smoothed) are enough, no marginal P(w|prev) needed:
// every candidate for one glide is compared under the SAME prev word, so the
// marginal (sum over all w' following prev) is identical for all of them and
// cancels out of any score DIFFERENCE. Only relative order matters here.
#pragma once
#include <cmath>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

// One decoder candidate as the rescorer sees it: text + its existing score
// (CTC + doubling + personalization, in nats — see swipe_engine.cpp). This
// mirrors SwipeEngine::Candidate without depending on swipe_engine.h.
struct RescoreCandidate {
    std::string text;
    double score;
};

// prev -> word -> raw co-occurrence count, loaded from a "word1 word2\tcount"
// file (Norvig's count_2w.txt format). Nested so lookup only touches entries
// for the one prev word that matters.
using BigramTable = std::unordered_map<std::string, std::unordered_map<std::string, double>>;

// Returns an empty table (not an error) if `path` can't be opened — the
// caller then gets prev.empty()-style no-op behaviour from every lookup,
// same as "no context". Matches load_freq()'s "missing file -> no prior"
// convention in swipe_engine.cpp.
inline BigramTable load_bigrams(const std::string& path) {
    BigramTable t;
    std::ifstream f(path);
    if (!f) return t;
    std::string w1, w2, line;
    while (std::getline(f, line)) {
        std::istringstream iss(line);
        double count;
        if (!(iss >> w1 >> w2 >> count)) continue;
        t[w1][w2] = count;
    }
    return t;
}

inline double bigram_count(const BigramTable& t, const std::string& prev, const std::string& word) {
    auto pit = t.find(prev);
    if (pit == t.end()) return 0.0;
    auto wit = pit->second.find(word);
    return (wit != pit->second.end()) ? wit->second : 0.0;
}

struct RescoreResult {
    int winner_idx = 0;       // index into candidates; 0 = CTC top-1 stood
    bool overridden = false;  // winner_idx != 0
    double margin = 0.0;      // combined-score gap, winner over runner-up (diagnostic)
};

// Re-rank `candidates` (already CTC-sorted, [0] is top-1) using the previous
// committed word `prev`. Empty `prev` or a single candidate is a guaranteed
// no-op — this is what makes the rescorer safe to run over every existing
// corpus (none of which record context): nothing changes without evidence.
//
// `lambda` weighs bigram evidence against the CTC score, same log-linear
// interpolation swipe_engine.cpp already uses for freq_lambda_/user_lambda_.
// It is NOT hand-tuned here — pick it by sweeping against labeled context
// data (context_rescore_probe.py), same as ambigMargin and the doubling cap
// were tuned. Until measured, treat any nonzero default as unverified.
//
// `max_ctc_gap` is the over-correction guard, and it is NOT optional (see
// RESULTS.md "context rescoring — the CTC-gap cap"). A pure CTC+bigram blend
// was tried first and shipped live; on real typing it overrode a decode CTC
// led by 5.5-7.9 nats — a clean, confident decision — because a common
// bigram can carry 10+ nats of weight, easily swamping even a large CTC
// gap. That is not "strong contextual evidence," it is one input drowning
// out another that was already conclusive. Only candidates within
// max_ctc_gap nats of the CTC top-1 are eligible to win at all — context can
// break a close call, never overrule one that wasn't close. The 0.16-nat
// `is/it: beyer->better` case (genuinely ambiguous, correctly fixed) and the
// 5.49/7.89-nat `im->in` cases (confident, wrongly flipped) are exactly the
// two ends this threshold sits between.
inline RescoreResult rescore(const std::string& prev,
                              const std::vector<RescoreCandidate>& candidates,
                              const BigramTable& bigrams,
                              double lambda,
                              double max_ctc_gap = 2.0) {
    RescoreResult r;
    if (prev.empty() || candidates.size() < 2) return r;

    const double top1_ctc = candidates[0].score;
    std::vector<double> combined(candidates.size());
    for (size_t i = 0; i < candidates.size(); i++) {
        if (top1_ctc - candidates[i].score > max_ctc_gap) {
            combined[i] = -1e300;   // not close enough to CTC top-1: ineligible
            continue;
        }
        const double count = bigram_count(bigrams, prev, candidates[i].text);
        combined[i] = candidates[i].score + lambda * std::log(count + 1.0);
    }
    size_t best = 0;
    for (size_t i = 1; i < combined.size(); i++)
        if (combined[i] > combined[best]) best = i;

    double second_best = combined[best == 0 ? 1 : 0];
    for (size_t i = 0; i < combined.size(); i++)
        if (i != best && combined[i] > second_best) second_best = combined[i];

    r.winner_idx = (int)best;
    r.overridden = (best != 0);
    r.margin = combined[best] - second_best;
    return r;
}
