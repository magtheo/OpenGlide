/* text-probe: commit a UTF-8 string through a selectable text backend and
 * report, per codepoint, whether it was actually injectable. This isolates
 * the text-commit capability of each backend (ADR-0002) from any UI layer.
 *
 * Default is a SAFE DRY-RUN: it resolves injectability without firing events
 * into the focused app. Pass --fire to actually emit (point a scratchpad at
 * the focused window first).
 *
 * Usage: text-probe [--fire] --backend <xtest|uinput|im2> --text "<string>"
 */
#include "probe.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int g_fire = 0;

int utf8_next(const char **p, const char *end, uint32_t *cp) {
    const unsigned char *s = (const unsigned char *)*p;
    if (s >= (const unsigned char *)end) return 0;
    unsigned char c = s[0];
    uint32_t r; int n;
    if (c < 0x80)            { r = c;       n = 1; }
    else if ((c & 0xE0)==0xC0){ r = c & 0x1F; n = 2; }
    else if ((c & 0xF0)==0xE0){ r = c & 0x0F; n = 3; }
    else if ((c & 0xF8)==0xF0){ r = c & 0x07; n = 4; }
    else { *p += 1; return 0; }
    if ((const unsigned char *)*p + n > (const unsigned char *)end) { *p += 1; return 0; }
    for (int i = 1; i < n; i++) {
        unsigned char b = s[i];
        if ((b & 0xC0) != 0x80) { *p += 1; return 0; }
        r = (r << 6) | (b & 0x3F);
    }
    if (n == 2 && r < 0x80)  { *p += 1; return 0; }
    if (n == 3 && r < 0x800) { *p += 1; return 0; }
    if (n == 4 && (r < 0x10000 || r > 0x10FFFF)) { *p += 1; return 0; }
    if (r >= 0xD800 && r <= 0xDFFF) { *p += 1; return 0; }
    *p += n; *cp = r; return n;
}

uint32_t cp_to_xkeysym(uint32_t cp) {
    if (cp == 0) return 0;
    if (cp >= 0x20 && cp <= 0x7E) return cp;          /* ASCII */
    if (cp >= 0xA0 && cp <= 0xFF) return cp;          /* Latin-1 supplement */
    if (cp >= 0x100 && cp <= 0x10FFFF) return 0x01000000u + cp; /* Unicode keysym */
    return 0;
}

static void usage(const char *prog) {
    fprintf(stderr,
        "text-probe — commit UTF-8 through a selectable backend (ADR-0002)\n\n"
        "Usage: %s [--fire] --backend <xtest|uinput|im2> --text \"<string>\"\n\n"
        "Backends:\n"
        "  xtest  X TEST raw-key injection (X11 / XWayland)\n"
        "  uinput Linux uinput virtual-keyboard raw-key injection\n"
        "  im2    Wayland zwp_input_method_v2 commit_string (UTF-8)\n\n"
        "Without --fire the probe is analytic only (no events fired).\n",
        prog);
}

int main(int argc, char **argv) {
    const char *bname = NULL, *text = NULL;
    for (int i = 1; i < argc; i++) {
        if      (!strcmp(argv[i], "--fire"))               g_fire = 1;
        else if (!strcmp(argv[i], "--backend") && i+1 < argc) bname = argv[++i];
        else if (!strcmp(argv[i], "--text")    && i+1 < argc) text = argv[++i];
        else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) { usage(argv[0]); return 0; }
        else { usage(argv[0]); return 2; }
    }
    if (!bname || !text) { usage(argv[0]); return 2; }

    const Backend *b = NULL;
    if      (!strcmp(bname, "xtest"))  b = xtest_backend();
    else if (!strcmp(bname, "uinput")) b = uinput_backend();
    else if (!strcmp(bname, "im2"))    b = im2_backend();
    else { fprintf(stderr, "unknown backend '%s'\n", bname); usage(argv[0]); return 2; }

    fprintf(stderr, "=== text-probe: backend=%s  fire=%d ===\n", b->name, g_fire);
    fprintf(stderr, "text (%zu bytes): %s\n", strlen(text), text);

    if (b->init && b->init() != 0) { fprintf(stderr, "init failed\n"); return 1; }
    int rc = b->commit(text);
    if (b->fini) b->fini();
    return rc;
}
