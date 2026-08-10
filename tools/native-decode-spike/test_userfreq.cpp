// Personalization persistence test: bump words, verify they're written to the
// user-freq file (the bump->save->file path that survives crash/kill).
#include "swipe_engine.h"
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

int main(int argc, char** argv) {
    if (argc < 4) { std::fprintf(stderr, "usage: test_userfreq <model.pte> <dict> <out.tsv>\n"); return 2; }
    SwipeEngine eng(argv[1], argv[2], "", argv[3]);
    if (!eng.ready()) { std::fprintf(stderr, "engine not ready\n"); return 1; }

    eng.bump("hello"); eng.bump("hello"); eng.bump("world"); eng.bump("good");

    std::ifstream f(argv[3]);
    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    bool ok = content.find("hello\t2") != std::string::npos &&
              content.find("world\t1") != std::string::npos &&
              content.find("good\t1")  != std::string::npos;
    std::printf("%s\n--- file ---\n%s\n", ok ? "PASS — bump+save+persist verified"
                                            : "FAIL (file missing/wrong)", content.c_str());
    return ok ? 0 : 1;
}
