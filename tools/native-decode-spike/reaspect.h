// reaspect.h — re-project a recorded glide onto a letter block of a different
// aspect ratio (ADR-0005 §3 / step 4).
//
// Header-only and templated on the point type ONLY so the model can be tested
// without ExecuTorch: `SwipePoint` lives in swipe_engine.h, which drags in the
// whole runtime. Any type with float `.x` and `.y` works.
#pragma once
#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

// The aspect the existing corpora were captured at: a 10-column, 3-row block of
// square keys.
static constexpr float kCaptureAspect = 10.0f / 3.0f;

// Re-project a recorded glide as if the SAME physical hand motion had been made
// on a letter block of a different aspect ratio.
//
// Key centres sit at fixed NORMALIZED positions and the user aims at keys, so the
// intended path is unchanged by a resize. Everything else in the stroke is
// physical, and normalizing by a shorter block magnifies it in y. So both models
// below split y into "intended" + deviation and scale only the deviation by
// k = target_aspect / capture_aspect. They differ in what they call intended.
//
// Both assume the user re-aims perfectly at the new key centres and does not
// change speed, so neither replaces capturing real glides at the new aspect.
//
// --- MODEL 1 (default): the TARGET WORD's key-centre polyline.
//
// A resize leaves one thing invariant: the keys the user aims at, which sit at
// fixed normalized positions. So the intended path is the polyline through the
// target word's key centres, and EVERYTHING else in the stroke — aiming error,
// curvature between keys, momentum, and crucially trailing overshoot — is
// physical, and a shorter block magnifies all of it in y.
//
// This is what the local-line model above misses. Trailing overshoot is a smooth,
// low-frequency excursion, so a local line fit follows it and calls it intended:
// the one mechanism RESULTS.md identifies as decode-corrupting was never being
// amplified. Projecting onto the key-centre polyline instead makes overshoot
// register as deviation, because the polyline STOPS at the last key.
//
// Projection distance is measured in key units (x*10, y*3), not raw normalized
// units — those are anisotropic, and using them would bias which segment each
// point is judged against.
//
// The projection is MONOTONIC: `seg` only ever moves forward through the word.
// A plain nearest-point search is wrong here because QWERTY paths cross
// themselves constantly — gliding "stone", the trailing overshoot past `e` lands
// nearer the s→t segment at the START of the word than the n→e segment it
// actually came from, so the overshoot got matched backwards and came out
// *smaller* after a resize. Walking the word in stroke order fixes that.
inline void project_to_poly(float px, float py,
                            const std::vector<std::pair<float, float>>& poly,
                            float& qx, float& qy, size_t& seg) {
    if (poly.empty()) { qx = px; qy = py; return; }
    if (poly.size() == 1) { qx = poly[0].first; qy = poly[0].second; return; }
    float best = 1e30f;
    size_t bestSeg = seg;
    qx = poly[seg].first; qy = poly[seg].second;
    for (size_t s = seg; s + 1 < poly.size(); s++) {
        // work in key units so x and y are comparable
        const float ax = poly[s].first * 10.f,     ay = poly[s].second * 3.f;
        const float bx = poly[s + 1].first * 10.f, by = poly[s + 1].second * 3.f;
        const float ux = px * 10.f, uy = py * 3.f;
        const float vx = bx - ax, vy = by - ay;
        const float L2 = vx * vx + vy * vy;
        float t = (L2 > 0.f) ? ((ux - ax) * vx + (uy - ay) * vy) / L2 : 0.f;
        t = std::min(1.f, std::max(0.f, t));
        const float cx = ax + t * vx, cy = ay + t * vy;
        const float d = (ux - cx) * (ux - cx) + (uy - cy) * (uy - cy);
        if (d < best) { best = d; qx = cx / 10.f; qy = cy / 3.f; bestSeg = s; }
    }
    seg = bestSeg;   // never walk backwards through the word
}

// Returns false if the word has a letter the layout doesn't know (caller falls
// back to the local-line model).
template <class P>
inline bool reaspect_word(std::vector<P>& pts, const std::string& word,
                          const float centres[26][2], float k) {
    if (k == 1.0f) return true;
    std::vector<std::pair<float, float>> poly;
    poly.reserve(word.size());
    for (char c : word) {
        if (c < 'a' || c > 'z') return false;
        poly.push_back({centres[c - 'a'][0], centres[c - 'a'][1]});
    }
    if (poly.empty()) return false;
    size_t seg = 0;                  // monotonic cursor through the word
    for (auto& p : pts) {
        float qx, qy;
        project_to_poly(p.x, p.y, poly, qx, qy, seg);
        p.y = qy + (p.y - qy) * k;   // only the block's HEIGHT changes
    }
    return true;
}

// --- MODEL 2 (--model line): a locally-straight fit.
//
// Kept for comparison, and used as the fallback when a target word has a letter
// the layout does not know. Its blind spot is the reason model 1 exists: trailing
// overshoot is smooth, so the local fit follows it and treats it as intended, and
// the one mechanism RESULTS.md shows corrupts decode never gets amplified.
template <class P>
inline void reaspect(std::vector<P>& pts, float k, int win = 5) {
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

