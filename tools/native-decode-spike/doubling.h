// doubling.h — double-letter recovery arithmetic, split out so it can be tested
// without ExecuTorch (see doubling_test.cpp).
//
// A glide crosses a doubled key once, so the greedy CTC output emits a single
// letter and the true word is penalised. The decoder compensates with a bonus for
// candidates that ARE the greedy with pairs re-doubled.
#pragma once
#include <string>

// How many doubled pairs must collapse in `word` to yield `greedy`; -1 if `word`
// is not a doubling of it at all.
//
// The previous version required word.size() == greedy.size() + 1 — EXACTLY one
// doubling — which made words needing two structurally unreachable: `coffee`
// (ff and ee, greedy "cofe") could never be boosted regardless of search width.
// Walk both strings once, and whenever they diverge, accept it only if this
// character repeats the previous one (i.e. it is the half of a pair the glide
// collapsed).
//
//   coffee/cofe -> 2   hello/helo -> 1   good/god -> 1   help/helo -> -1
inline int count_doublings(const std::string& word, const std::string& greedy) {
    size_t i = 0, j = 0;
    int d = 0;
    while (i < word.size()) {
        if (j < greedy.size() && word[i] == greedy[j]) { i++; j++; continue; }
        if (i > 0 && word[i] == word[i - 1]) { i++; d++; continue; }   // collapsed pair
        return -1;
    }
    return (j == greedy.size()) ? d : -1;
}
