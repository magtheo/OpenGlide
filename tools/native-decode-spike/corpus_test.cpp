// Corpus test: run the native SwipeEngine over a flattened swipe corpus and
// report top-1 word accuracy + average decode latency (vs the Python decoder).
//
// `--sweep` additionally answers ADR-0005's open question: now that the window
// resizes freely, the letter block's aspect ratio is no longer pinned at 10:3 —
// how far can it drift before decode degrades? See reaspect() for the model and
// its limits. This is a PRE-CHECK, not the gate: it tells you where to look, and
// a real capture at the candidate aspects confirms it.
#include "swipe_engine.h"
#include <cstdlib>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

// The aspect the existing corpora were captured at: a 10-column, 3-row block of
// square keys.
static constexpr float kCaptureAspect = 10.0f / 3.0f;

// Re-project a recorded glide as if the SAME physical hand motion had been made
// on a letter block of a different aspect ratio.
//
// Key centres sit at fixed NORMALIZED positions and the user aims at keys, so
// the intended path through normalized space is unchanged by a resize. What does
// change is everything produced by physical mouse momentum — wobble, curvature,
// overshoot — because normalizing by a shorter block magnifies it in y. So split
// y into a smoothed "intended" component and a deviation, and scale only the
// deviation by k = target_aspect / capture_aspect.
//
// Limits, stated plainly: the local fit is a crude high-pass, and this assumes
// the user re-aims perfectly at the new key centres (no re-learning cost, no
// change in speed). It cannot replace capturing real glides at the new aspect —
// it exists to narrow the range those captures need to cover.
static void reaspect(std::vector<SwipePoint>& pts, float k, int win = 5) {
    if (k == 1.0f || (int)pts.size() < 3) return;
    const int n = (int)pts.size();
    const int half = win / 2;
    std::vector<float> sm(n);
    for (int i = 0; i < n; i++) {
        int lo = i - half, hi = i + half;
        if (lo < 0) lo = 0;
        if (hi > n - 1) hi = n - 1;
        // Local least-squares LINE, evaluated at i — not a moving average. An
        // average is biased at the ends of a sloped path, which would invent a
        // deviation precisely where the decoder is most sensitive (RESULTS.md:
        // trailing overshoot corrupts decode, leading overshoot does not). A
        // local line reproduces a straight glide exactly, so a clean stroke is
        // left alone at every aspect and only real wobble is scaled.
        const int m = hi - lo + 1;
        double sx = 0, sy = 0, sxx = 0, sxy = 0;
        for (int j = lo; j <= hi; j++) {
            const double x = j, y = pts[j].y;
            sx += x; sy += y; sxx += x * x; sxy += x * y;
        }
        const double den = double(m) * sxx - sx * sx;
        double a, b;
        if (den == 0.0) { b = 0.0; a = sy / m; }
        else { b = (double(m) * sxy - sx * sy) / den; a = (sy - b * sx) / m; }
        sm[i] = float(a + b * double(i));
    }
    for (int i = 0; i < n; i++) pts[i].y = sm[i] + (pts[i].y - sm[i]) * k;
}

struct Result { int n = 0, top1 = 0, top5 = 0; double total_ms = 0; };

static Result run(SwipeEngine& eng, const char* corpus, float aspect, bool verbose) {
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
        reaspect(pts, k);

        auto t0 = std::chrono::steady_clock::now();
        std::string greedy;
        auto cands = eng.decode(pts, &greedy);
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
            "             ADR-0005 pre-check for how far free resizing may go\n");
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

    int pos = 4;
    for (int i = 4; i < argc; i++) {
        if (!std::strcmp(argv[i], "--sweep")) { sweep = true; continue; }
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

    SwipeEngine eng(model, dict, freq);
    if (lambda >= 0.0) eng.set_freq_lambda(lambda);
    if (!eng.ready()) { std::fprintf(stderr, "engine not ready\n"); return 1; }

    if (!sweep) {
        if (aspect != kCaptureAspect)
            std::printf("(re-projected to aspect %.2f — k=%.2f on y deviations)\n",
                        aspect, aspect / kCaptureAspect);
        const Result r = run(eng, corpus, aspect, true);
        if (r.n > 0)
            std::printf("\n=== native corpus: %d glides | top-1 %d (%.0f%%) | top-5 %d (%.0f%%) | avg decode %.1f ms ===\n",
                        r.n, r.top1, 100.0 * r.top1 / r.n, r.top5, 100.0 * r.top5 / r.n,
                        r.total_ms / r.n);
        return 0;
    }

    // The band. 10:3 is the reference row — its top-1 is the baseline every
    // other row is judged against (a corpus miss is a miss at every aspect).
    static const struct { const char* name; float a; } band[] = {
        {"10:5.0 (tall)", 2.00f}, {"10:4.0", 2.50f}, {"10:3.5", 2.86f},
        {"10:3.0 (ref)",  3.333f},
        {"10:2.6", 3.85f}, {"10:2.2", 4.55f}, {"10:1.8 (wide)", 5.56f},
    };
    std::printf("\n%-16s %8s %10s %10s %10s\n", "letter block", "k", "top-1", "top-5", "avg ms");
    std::printf("---------------------------------------------------------------\n");
    int baseline = -1;
    for (const auto& b : band) {
        const Result r = run(eng, corpus, b.a, false);
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
