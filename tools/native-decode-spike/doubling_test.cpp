// doubling_test — count_doublings() with no model, dictionary or ExecuTorch.
//
//   g++ -std=c++20 -I. -o /tmp/doubling_test doubling_test.cpp && /tmp/doubling_test
//
// Guards two things at once: that the cases which already recover keep working,
// and that `help`/`helo` still earns NO bonus — that pair is what killed the
// frequency prior (RESULTS.md), and a doubling bonus that fired on it would
// reintroduce the same regression by another route.
#include "doubling.h"
#include <cstdio>
#include <string>

static int pass=0, fail=0;
static void ck(const char* w, const char* g, int want){
  int got = count_doublings(w, g);
  bool ok = got==want; ok?pass++:fail++;
  printf("  %-12s vs %-10s -> %2d (want %2d)  %s\n", w, g, got, want, ok?"ok":"FAIL");
}
int main(){
  printf("count_doublings: how many collapsed pairs turn <word> into <greedy>?\n\n");
  printf(" -- the cases that already worked (must not regress) --\n");
  ck("hello","helo",1);        // the canonical one
  ck("good","god",1);
  ck("all","al",1);
  printf("\n -- the case the old +1 cap made UNREACHABLE --\n");
  ck("coffee","cofe",2);       // ff AND ee
  ck("committee","comite",3);  // mm, tt, ee
  ck("success","suces",2);     // cc, ss
  printf("\n -- must NOT be treated as doublings --\n");
  ck("help","helo",-1);        // the word that killed the frequency prior
  ck("code","cofe",-1);        // substitution, not a doubling
  ck("cat","cart",-1);         // greedy longer than word
  ck("stone","stone",0);       // identical: zero doublings, not a bonus case
  ck("xyz","abc",-1);
  ck("","abc",-1);
  printf("\n -- edge shapes --\n");
  ck("aa","a",1);
  ck("aaa","a",2);             // a run of three
  ck("bookkeeper","bokeper",3);// oo, kk, ee
  ck("abc","abc",0);
  printf("\n%d passed, %d failed\n", pass, fail);
  return fail?1:0;
}
