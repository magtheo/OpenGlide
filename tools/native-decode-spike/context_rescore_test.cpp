// context_rescore_test — rescore() with a hand-built bigram table, no model,
// dictionary or ExecuTorch.
//
//   g++ -std=c++20 -I. -o /tmp/context_rescore_test context_rescore_test.cpp && /tmp/context_rescore_test
//
// Guards the two things that matter: (1) no context in means no change out —
// the safety property that lets this run over every existing corpus, none of
// which record context, without touching a single label; (2) weak bigram
// evidence must not flip a CTC decision, only strong evidence should — this
// is the over-correction guard ADR-0006 calls for, and it falls out of the
// log-linear blend rather than being a separate hand-coded rule.
#include "context_rescore.h"
#include <cstdio>

static int pass = 0, fail = 0;

static void expect(const char* name, bool ok) {
    ok ? pass++ : fail++;
    printf("  %-58s %s\n", name, ok ? "ok" : "FAIL");
}

int main() {
    printf("rescore(): CTC-plus-bigram log-linear re-ranking\n\n");

    printf(" -- no context recorded (every corpus before context logging landed) --\n");
    {
        // help beats hello on CTC alone, mirroring a decode-time score gap.
        std::vector<RescoreCandidate> cands = {{"help", 5.0}, {"hello", 3.0}};
        BigramTable bg;
        bg["say"]["hello"] = 649277;
        auto r = rescore("", cands, bg, /*lambda=*/1.0);
        expect("empty prev -> winner_idx 0, not overridden", r.winner_idx == 0 && !r.overridden);
    }
    {
        std::vector<RescoreCandidate> cands = {{"help", 5.0}};
        BigramTable bg;
        auto r = rescore("say", cands, bg, 1.0);
        expect("single candidate -> no-op regardless of context", r.winner_idx == 0 && !r.overridden);
    }

    printf("\n -- the frequency-prior trap (RESULTS.md), solved by context --\n");
    {
        // Real Norvig counts: "say hello" 649277, "say help" unseen.
        // CTC alone favors help (the same failure that killed freq_lambda_).
        std::vector<RescoreCandidate> cands = {{"help", 5.0}, {"hello", 4.8}};
        BigramTable bg;
        bg["say"]["hello"] = 649277;  // "say help" absent -> count 0
        auto r = rescore("say", cands, bg, /*lambda=*/1.0);
        expect("\"say ___\": strong bigram evidence overrides CTC's help",
               r.winner_idx == 1 && r.overridden);
    }
    {
        // Same candidates, opposite context: "need help" 3.4M, "need hello" unseen.
        std::vector<RescoreCandidate> cands = {{"help", 5.0}, {"hello", 4.8}};
        BigramTable bg;
        bg["need"]["help"] = 3410436;
        auto r = rescore("need", cands, bg, 1.0);
        expect("\"need ___\": bigram agrees with CTC -> help stands",
               r.winner_idx == 0 && !r.overridden);
    }

    printf("\n -- the over-correction guard: weak evidence must not flip a real gap --\n");
    {
        // A modest bigram edge (2x) must not overturn a large CTC gap — this
        // is what keeps context from reintroducing the frequency-prior failure
        // by a different route (a rare-but-plausible bigram flipping a clean
        // decode). lambda=1.0 here: log(3)-log(1) ~= 1.1 nats of pull, well
        // under the 4-nat CTC gap.
        std::vector<RescoreCandidate> cands = {{"think", 10.0}, {"tink", 6.0}};
        BigramTable bg;
        bg["to"]["think"] = 2;
        bg["to"]["tink"] = 1;   // barely any signal either way
        auto r = rescore("to", cands, bg, 1.0);
        expect("weak bigram edge does not override a clear CTC winner",
               r.winner_idx == 0 && !r.overridden);
    }
    {
        // No co-occurrence data at all for either candidate: falls back to
        // pure CTC order, identical to not having context.
        std::vector<RescoreCandidate> cands = {{"think", 10.0}, {"tink", 9.9}};
        BigramTable bg;  // empty
        auto r = rescore("xyzzy", cands, bg, 1.0);
        expect("no bigram data for this context -> CTC order stands",
               r.winner_idx == 0 && !r.overridden);
    }

    printf("\n -- margin is reported for diagnostics, same shape as E4's ambigMargin --\n");
    {
        std::vector<RescoreCandidate> cands = {{"help", 5.0}, {"hello", 4.8}, {"hell", 1.0}};
        BigramTable bg;
        bg["say"]["hello"] = 649277;
        auto r = rescore("say", cands, bg, 1.0);
        expect("margin is positive for the actual winner", r.margin > 0.0);
        expect("3-candidate list still resolves to the right winner", r.winner_idx == 1);
    }

    printf("\n -- the CTC-gap cap: this is the bug shipped live and measured wrong --\n");
    {
        // Real numbers from corpus-live.jsonl ("context rescoring - the
        // CTC-gap cap" in RESULTS.md): ctx="testing", im led by 5.49 nats,
        // im has ZERO count anywhere in count_2w.txt (no contractions at
        // all), "in" has a large count -> pure blend flipped it. Must NOT
        // flip with the gap cap in place.
        std::vector<RescoreCandidate> cands = {{"im", 3.19}, {"in", -2.30}};
        BigramTable bg;
        bg["testing"]["in"] = 649277;   // im: absent -> count 0
        auto r = rescore("testing", cands, bg, 1.0, /*max_ctc_gap=*/2.0);
        expect("5.49-nat CTC lead is NOT close enough to override",
               r.winner_idx == 0 && !r.overridden);
    }
    {
        // Same shape, larger gap (the "work"->im/in case, 7.89 nats).
        std::vector<RescoreCandidate> cands = {{"im", 2.62}, {"in", -5.27}};
        BigramTable bg;
        bg["work"]["in"] = 500000;
        auto r = rescore("work", cands, bg, 1.0, 2.0);
        expect("7.89-nat CTC lead is NOT close enough to override",
               r.winner_idx == 0 && !r.overridden);
    }
    {
        // The genuine win the cap must NOT block: is/it, beyer vs better,
        // 0.16-nat gap - a real coin-flip, exactly what this exists for.
        std::vector<RescoreCandidate> cands = {{"beyer", 0.90}, {"better", 0.74}};
        BigramTable bg;
        bg["it"]["better"] = 6061362;
        auto r = rescore("it", cands, bg, 1.0, 2.0);
        expect("0.16-nat gap still overrides (the cap doesn't block close calls)",
               r.winner_idx == 1 && r.overridden);
    }
    {
        // A 3rd-place candidate outside the gap must not win even if it has
        // the best bigram count of the bunch - eligibility is CTC-relative
        // per candidate, not just "beat someone."
        std::vector<RescoreCandidate> cands = {{"think", 10.0}, {"thing", 9.5}, {"tink", 2.0}};
        BigramTable bg;
        bg["a"]["tink"] = 999999999;   // huge count, but tink is 8 nats behind top-1
        auto r = rescore("a", cands, bg, 1.0, 2.0);
        expect("a distant 3rd stays ineligible regardless of its own bigram count",
               r.winner_idx != 2);
    }

    printf("\n%d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
