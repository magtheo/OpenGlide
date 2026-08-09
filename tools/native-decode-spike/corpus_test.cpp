// Corpus test: run the native SwipeEngine over a flattened swipe corpus and
// report top-1 word accuracy + average decode latency (vs the Python decoder).
#include "swipe_engine.h"
#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 4) { std::fprintf(stderr, "usage: corpus_test <model.pte> <dict> <corpus_flat.txt>\n"); return 2; }
    SwipeEngine eng(argv[1], argv[2]);
    if (!eng.ready()) { std::fprintf(stderr, "engine not ready\n"); return 1; }

    std::ifstream f(argv[3]);
    std::string line;
    int n = 0, top1 = 0, top5 = 0;
    double total_ms = 0;
    while (std::getline(f, line)) {
        std::istringstream ss(line);
        std::string target; int np;
        if (!(ss >> target >> np) || np < 4) continue;
        std::vector<SwipePoint> pts(np);
        for (int i = 0; i < np; i++) { ss >> pts[i].x >> pts[i].y >> pts[i].t; }

        auto t0 = std::chrono::steady_clock::now();
        std::string greedy;
        auto cands = eng.decode(pts, &greedy);
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        total_ms += ms;
        n++;
        bool hit1 = !cands.empty() && cands[0].text == target;
        bool hit5 = false;
        for (int i = 0; i < (int)cands.size() && i < 5; i++) if (cands[i].text == target) hit5 = true;
        if (hit1) top1++;
        if (hit5) top5++;
        std::printf("#%-2d target=%-12s greedy=%-10s top1=%-12s %s  %.1fms\n",
                    n, target.c_str(), (greedy.empty() ? "-" : greedy.c_str()),
                    (cands.empty() ? "-" : cands[0].text.c_str()),
                    hit1 ? "OK" : "..", ms);
    }
    if (n > 0)
        std::printf("\n=== native corpus: %d glides | top-1 %d (%.0f%%) | top-5 %d (%.0f%%) | avg decode %.1f ms ===\n",
                    n, top1, 100.0 * top1 / n, top5, 100.0 * top5 / n, total_ms / n);
    return 0;
}
