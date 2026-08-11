// aspect_model_test — exercises reaspect.h with no model, no dictionary and no
// ExecuTorch, so the aspect-simulation logic can be checked anywhere.
//
//   g++ -std=c++20 -I. -o /tmp/aspect_model_test aspect_model_test.cpp && /tmp/aspect_model_test
//
// The case that matters is trailing overshoot: RESULTS.md shows it is what
// corrupts decode, and the local-line model never amplified it, so the first
// aspect sweep was blind to exactly the failure it was meant to find.
#include "reaspect.h"
#include <cstdio>
#include <cmath>
#include <string>
#include <vector>
#include <utility>
#include <algorithm>
struct SwipePoint { float x, y, t; };   // stand-in: reaspect.h is templated

// real QWERTY centres, a..z
static float C[26][2] = {
 {0.10f,0.500f},{0.60f,0.833f},{0.40f,0.833f},{0.30f,0.500f},{0.25f,0.167f},
 {0.40f,0.500f},{0.50f,0.500f},{0.60f,0.500f},{0.75f,0.167f},{0.70f,0.500f},
 {0.80f,0.500f},{0.90f,0.500f},{0.80f,0.833f},{0.70f,0.833f},{0.85f,0.167f},
 {0.95f,0.167f},{0.05f,0.167f},{0.35f,0.167f},{0.20f,0.500f},{0.45f,0.167f},
 {0.65f,0.167f},{0.50f,0.833f},{0.15f,0.167f},{0.30f,0.833f},{0.55f,0.167f},{0.20f,0.833f}};

static int pass=0, fail=0;
static void check(const char* n, bool ok, const char* d=""){ ok?pass++:fail++;
  printf("  %-52s %s%s%s\n", n, ok?"ok":"FAIL", *d?"  ":"", d); }

// a glide for `word`: straight hops between key centres, plus optional trailing
// overshoot past the last key (the mouse-momentum case RESULTS.md flags).
static std::vector<SwipePoint> stroke(const std::string& w, float overshootY){
  std::vector<SwipePoint> p; float t=0;
  for(size_t i=0;i+1<w.size();i++){
    float ax=C[w[i]-'a'][0], ay=C[w[i]-'a'][1];
    float bx=C[w[i+1]-'a'][0], by=C[w[i+1]-'a'][1];
    for(int s=0;s<8;s++){ float f=s/8.f; p.push_back({ax+(bx-ax)*f, ay+(by-ay)*f, t}); t+=16; }
  }
  float lx=C[w.back()-'a'][0], ly=C[w.back()-'a'][1];
  p.push_back({lx,ly,t}); t+=16;
  if(overshootY!=0.f) for(int s=1;s<=6;s++){ float f=s/6.f;
      p.push_back({lx, ly+overshootY*f, t}); t+=16; }
  return p;
}
// excursion of the LAST point past the last key -- the trailing overshoot itself.
// (Global max is useless here: "stone" passes through n on the bottom row.)
static float tailY(const std::vector<SwipePoint>& p, char last){
  return p.back().y - C[last-'a'][1]; }

int main(){
  printf("aspect model: does trailing overshoot scale? (k=1.67 = a 10:1.8 board)\n\n");
  const float k=1.67f;

  // 1. clean stroke, no overshoot: intended path is exactly the polyline, so a
  //    resize must leave it untouched.
  { auto a=stroke("stone",0.f), b=a;
    reaspect_word(b,"stone",C,k);
    float d=0; for(size_t i=0;i<a.size();i++) d=std::fmax(d,std::fabs(a[i].y-b[i].y));
    check("clean stroke is unchanged by aspect", d<1e-4f); }

  // 2. THE POINT: trailing overshoot past the last key must be amplified.
  { auto a=stroke("stone",0.18f);
    auto w=a; reaspect_word(w,"stone",C,k);
    auto l=a; reaspect(l,k);
    float base=tailY(a,'e'), gotW=tailY(w,'e'), gotL=tailY(l,'e');
    char buf[160]; snprintf(buf,sizeof buf,"base=%.3f  word=%.3f (%.2fx)  line=%.3f (%.2fx)",
                            base,gotW,gotW/base,gotL,gotL/base);
    check("word model AMPLIFIES trailing overshoot ~k", gotW/base>1.5f, buf);
    check("line model does NOT (the blind spot)",       gotL/base<1.15f, buf); }

  // 3. overshoot big enough to leave the board is what the decoder's adapter
  //    then has to drop -- so the sweep can actually see the failure mode.
  { auto a=stroke("stun",0.20f);          // ends on 'n', bottom row (y=0.833)
    auto w=a; reaspect_word(w,"stun",C,k);
    int oobA=0,oobW=0; for(auto&q:a) if(q.y>1.f) oobA++; for(auto&q:w) if(q.y>1.f) oobW++;
    char buf[80]; snprintf(buf,sizeof buf,"10:3 -> %d pts, 10:1.8 -> %d pts", oobA, oobW);
    check("more points pushed out of bounds when short", oobW>oobA, buf); }

  // 4. mid-stroke aiming error is scaled too
  { auto a=stroke("stone",0.f); a[9].y+=0.05f; a[10].y+=0.05f;
    auto w=a; reaspect_word(w,"stone",C,k);
    check("mid-stroke deviation scaled", std::fabs(w[9].y-a[9].y)>0.02f); }

  // 5. unknown letters -> caller falls back
  { auto a=stroke("stone",0.f);
    check("rejects a word it cannot map", !reaspect_word(a,"st1ne",C,k)); }

  // 6. k==1 is the identity
  { auto a=stroke("stone",0.15f), b=a; reaspect_word(b,"stone",C,1.0f);
    float d=0; for(size_t i=0;i<a.size();i++) d=std::fmax(d,std::fabs(a[i].y-b[i].y));
    check("k=1 identity", d==0.f); }

  printf("\n%d passed, %d failed\n", pass, fail);
  return fail?1:0;
}
