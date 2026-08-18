// Corpus test: run the native SwipeEngine over a flattened swipe corpus and
// report top-1 word accuracy + average decode latency (vs the Python decoder).
//
// `--sweep` additionally answers ADR-0005's open question: now that the window
// resizes freely, the letter block's aspect ratio is no longer pinned at 10:3 —
// how far can it drift before decode degrades? See reaspect() for the model and
// its limits. This is a PRE-CHECK, not the gate: it tells you where to look, and
// a real capture at the candidate aspects confirms it.
#include "swipe_engine.h"
#include "reaspect.h"
#include <cstdlib>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

struct Result {
    int n = 0, top1 = 0, top5 = 0;
    double total_ms = 0;
    // ADR-0006 F2 taxonomy: WHY each miss missed. Only `outranked` is fixable by
    // a re-ranker; the others need the decode/scoring to surface the word first,
    // and each of the three has a different fix.
    int miss_dict = 0, miss_alph = 0, miss_len = 0, miss_outranked = 0, miss_other = 0;
};

static Result run(SwipeEngine& eng, const char* corpus, float aspect, bool verbose,
                  bool wordModel, const float centres[26][2], bool diagnose = false) {
    Result r;
    const float k = aspect / kCaptureAspect;
    std::ifstream f(corpus);
    std::string line;
    while (std::getline(f, line)) {
        std::istringstream ss(line);
        std::string target; int np;
        if (!(ss >> target >> np) || np < 4) continue;
        std::vector<SwipePoint> pts(np);
        for (int i = 0; i < np; i++) { ss >> pts[i].x >> pts[i].y >> pts[i].t; }
        if (!wordModel || !reaspect_word(pts, target, centres, k))
            reaspect(pts, k);      // fallback: local-line model

        auto t0 = std::chrono::steady_clock::now();
        std::string greedy;
        DecodeDiag diag;
        if (diagnose) diag.target = target;
        auto cands = eng.decode(pts, &greedy, diagnose ? &diag : nullptr);
        const double ms = std::chrono::duration<double, std::milli>(
                              std::chrono::steady_clock::now() - t0).count();
        r.total_ms += ms;
        r.n++;
        const bool hit1 = !cands.empty() && cands[0].text == target;
        bool hit5 = false;
        for (int i = 0; i < (int)cands.size() && i < 5; i++) if (cands[i].text == target) hit5 = true;
        if (hit1) r.top1++;
        if (hit5) r.top5++;
        if (verbose)
            std::printf("#%-2d target=%-12s greedy=%-10s top1=%-12s %s  %.1fms\n",
                        r.n, target.c_str(), (greedy.empty() ? "-" : greedy.c_str()),
                        (cands.empty() ? "-" : cands[0].text.c_str()),
                        hit1 ? "OK" : "..", ms);

        if (diagnose && !hit1) {
            const std::string v = diag.verdict();
            if      (v == "not-in-dict")   r.miss_dict++;
            else if (v == "alph-pruned")   r.miss_alph++;
            else if (v == "length-pruned") r.miss_len++;
            else if (v == "out-ranked")    r.miss_outranked++;
            else                           r.miss_other++;
            std::printf("     -> %-13s ", v.c_str());
            if (v == "alph-pruned")
                std::printf("letter '%c' never in any timestep's top-K set", diag.alph_blocker);
            else if (v == "length-pruned")
                std::printf("|%d - %d| = %d > 3  (maxwlen %d)", (int)target.size(), diag.greedy_len,
                            std::abs((int)target.size() - diag.greedy_len), diag.max_word_len);
            else if (v == "out-ranked")
                std::printf("rank %d, score %.2f vs top %.2f (gap %.2f)", diag.rank + 1,
                            diag.score, diag.top_score, diag.top_score - diag.score);
            else if (v == "not-in-dict")
                std::printf("absent from the lexicon");
            if (diag.doublings > 0)
                std::printf("   [needs %d doubling%s]", diag.doublings, diag.doublings > 1 ? "s" : "");
            std::printf("\n");
        }
    }
    return r;
}

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr,
            "usage: corpus_test <model.pte> <dict> <corpus_flat.txt> [freq.txt] [lambda]\n"
            "                   [--aspect W:H | --aspect <ratio>] [--sweep]\n"
            "\n"
            "  --aspect   re-project the corpus onto a letter block of this aspect\n"
            "             (default 10:3, the aspect it was captured at)\n"
            "  --sweep    run a band of aspects and print top-1 for each — the\n"
            "             ADR-0005 pre-check for how far free resizing may go\n"
            "  --model    word (default) decomposes against the target word's\n"
            "             key-centre polyline, so overshoot scales with aspect;\n"
            "             line uses a local straight fit, which does NOT scale it\n"
            "  --diagnose for every miss, report WHY it missed — which prune\n"
            "             removed the word, or its rank if it was merely\n"
            "             out-scored — plus a tally (ADR-0006 F2)\n"
            "  --doublings N  cap on collapsed pairs the double-letter bonus\n"
            "                 rewards. 1 = the old behaviour (coffee unreachable),\n"
            "                 2 = default. A/B these against the corpus.\n"
            "  --alph K       per-timestep letter survival set: a word is scored\n"
            "                 only if every letter makes some timestep's top-K\n"
            "                 (G1, ADR-0006; default 3). The diagnosed harshest\n"
            "                 prune — widen to accept truncated/sloppy glides.\n"
            "  --user-freq F  personalization counts to load before decoding.\n"
            "                 Live-corpus labels come from the APP's decode, which\n"
            "                 personalizes (user_freq.tsv); scoring without it\n"
            "                 mismatches even uncorrected glides. Pass the same\n"
            "                 file the app used (~/.local/share/openglide/\n"
            "                 user_freq.tsv).\n");
        return 2;
    }

    // Positional args stay as they were; flags are optional and may follow.
    const char* model = argv[1];
    const char* dict = argv[2];
    const char* corpus = argv[3];
    const char* freq = "";
    double lambda = -1.0;
    float aspect = kCaptureAspect;
    bool sweep = false;
    bool wordModel = true;   // key-centre polyline; --model line for the old one
    bool diagnose = false;
    int  maxDoublings = -1;  // -1 = engine default
    int  alphTopk = -1;      // -1 = engine default
    const char* userFreq = "";

    int pos = 4;
    for (int i = 4; i < argc; i++) {
        if (!std::strcmp(argv[i], "--sweep")) { sweep = true; continue; }
        if (!std::strcmp(argv[i], "--diagnose")) { diagnose = true; continue; }
        if (!std::strcmp(argv[i], "--doublings") && i + 1 < argc) {
            maxDoublings = std::atoi(argv[++i]);
            continue;
        }
        if (!std::strcmp(argv[i], "--alph") && i + 1 < argc) {
            alphTopk = std::atoi(argv[++i]);
            continue;
        }
        if (!std::strcmp(argv[i], "--user-freq") && i + 1 < argc) {
            userFreq = argv[++i];
            continue;
        }
        if (!std::strcmp(argv[i], "--model") && i + 1 < argc) {
            const char* m = argv[++i];
            if (!std::strcmp(m, "line")) wordModel = false;
            else if (!std::strcmp(m, "word")) wordModel = true;
            else { std::fprintf(stderr, "--model must be word|line\n"); return 2; }
            continue;
        }
        if (!std::strcmp(argv[i], "--aspect") && i + 1 < argc) {
            const char* v = argv[++i];
            const char* colon = std::strchr(v, ':');
            aspect = colon ? float(std::atof(std::string(v, colon).c_str()) / std::atof(colon + 1))
                           : float(std::atof(v));
            if (!(aspect > 0.0f)) { std::fprintf(stderr, "bad --aspect\n"); return 2; }
            continue;
        }
        if (argv[i][0] == '-') { std::fprintf(stderr, "unknown flag %s\n", argv[i]); return 2; }
        if (pos == 4) { freq = argv[i]; pos++; }
        else if (pos == 5) { lambda = std::atof(argv[i]); pos++; }
    }

    SwipeEngine eng(model, dict, freq, userFreq);
    if (lambda >= 0.0) eng.set_freq_lambda(lambda);
    if (maxDoublings >= 0) eng.set_max_doublings(maxDoublings);
    if (alphTopk >= 1) eng.set_alph_topk(alphTopk);
    if (!eng.ready()) { std::fprintf(stderr, "engine not ready\n"); return 1; }

    // The geometry the decoder is actually scoring against — same source the UI
    // draws from, so the simulated intent matches the real keyboard.
    float centres[26][2];
    for (const KeyCenter& kc : eng.layout()) {
        const int idx = kc.label - 'a';
        if (idx >= 0 && idx < 26) { centres[idx][0] = kc.x; centres[idx][1] = kc.y; }
    }

    if (!sweep) {
        if (aspect != kCaptureAspect)
            std::printf("(re-projected to aspect %.2f — k=%.2f on y deviations)\n",
                        aspect, aspect / kCaptureAspect);
        const Result r = run(eng, corpus, aspect, true, wordModel, centres, diagnose);
        if (r.n > 0)
            std::printf("\n=== native corpus: %d glides | top-1 %d (%.0f%%) | top-5 %d (%.0f%%) | avg decode %.1f ms ===\n",
                        r.n, r.top1, 100.0 * r.top1 / r.n, r.top5, 100.0 * r.top5 / r.n,
                        r.total_ms / r.n);
        if (diagnose) {
            const int miss = r.n - r.top1;
            std::printf("\n=== why the %d miss(es) missed (ADR-0006 F2) ===\n", miss);
            std::printf("  out-ranked     %2d   <- a re-ranker CAN fix these\n", r.miss_outranked);
            std::printf("  length-pruned  %2d   <- widen/soften the |wlen-glen| band\n", r.miss_len);
            std::printf("  alph-pruned    %2d   <- widen the per-timestep top-3 letter set\n", r.miss_alph);
            std::printf("  not-in-dict    %2d   <- lexicon gap, no decoder change helps\n", r.miss_dict);
            if (r.miss_other) std::printf("  other          %2d\n", r.miss_other);
            std::printf("Only the first line is re-rankable. The rest need decode/scoring work,\n"
                        "and each line points at a DIFFERENT fix — that is the whole point.\n");
        }
        return 0;
    }

    // The band. 10:3 is the reference row — its top-1 is the baseline every
    // other row is judged against (a corpus miss is a miss at every aspect).
    static const struct { const char* name; float a; } band[] = {
        {"10:5.0 (tall)", 2.00f}, {"10:4.0", 2.50f}, {"10:3.5", 2.86f},
        {"10:3.0 (ref)",  3.333f},
        {"10:2.6", 3.85f}, {"10:2.2", 4.55f}, {"10:1.8 (wide)", 5.56f},
    };
    std::printf("\nintent model: %s\n", wordModel ? "key-centre polyline (overshoot IS scaled)"
                                                   : "local line (overshoot is NOT scaled)");
    std::printf("\n%-16s %8s %10s %10s %10s\n", "letter block", "k", "top-1", "top-5", "avg ms");
    std::printf("---------------------------------------------------------------\n");
    int baseline = -1;
    for (const auto& b : band) {
        const Result r = run(eng, corpus, b.a, false, wordModel, centres);
        if (r.n == 0) { std::fprintf(stderr, "empty corpus\n"); return 1; }
        if (b.a == 3.333f) baseline = r.top1;
        std::printf("%-16s %8.2f %6d %3.0f%% %6d %3.0f%% %10.1f\n",
                    b.name, b.a / kCaptureAspect,
                    r.top1, 100.0 * r.top1 / r.n,
                    r.top5, 100.0 * r.top5 / r.n, r.total_ms / r.n);
    }
    std::printf("---------------------------------------------------------------\n");
    std::printf("baseline top-1 at 10:3 = %d. Clamp the window's resize band to the\n"
                "rows that hold within noise of it, then CONFIRM with a real capture\n"
                "at the edge aspects (ADR-0005 verification gate).\n", baseline);
    return 0;
}
