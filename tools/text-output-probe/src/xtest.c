/* xtest backend: injects each codepoint as a raw X key event via the X TEST
 * extension. Non-ASCII codepoints that do not map to a keycode on the active
 * XKB layout are uninjectable — this demonstrates the raw-key UTF-8 limit.
 * Resolves injectability unconditionally; fires events only when g_fire. */
#include "probe.h"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>
#include <stdio.h>
#include <string.h>

static Display *dpy;

static int xtest_init(void) {
    dpy = XOpenDisplay(NULL);
    if (!dpy) { perror("XOpenDisplay"); return -1; }
    int evb, erb, maj, min;
    if (!XTestQueryExtension(dpy, &evb, &erb, &maj, &min)) {
        fprintf(stderr, "[xtest] XTEST extension not present\n");
        return -1;
    }
    printf("[xtest] display=%s XTEST v%d.%d\n", XDisplayString(dpy), maj, min);
    return 0;
}

static int xtest_commit(const char *utf8) {
    const char *p = utf8, *end = utf8 + strlen(utf8);
    uint32_t cp; int inj = 0, skip = 0;
    KeyCode shift_kc = XKeysymToKeycode(dpy, XK_Shift_L);
    printf("[xtest] per-codepoint:\n");
    while (p < end) {
        int n = utf8_next(&p, end, &cp);
        if (!n) { skip++; continue; }
        uint32_t ks = cp_to_xkeysym(cp);
        KeyCode kc = ks ? XKeysymToKeycode(dpy, (KeySym)ks) : 0;
        if (!kc) {
            skip++;
            printf("  U+%04X  keysym=0x%06X  -> NO keycode (uninjectable on current layout)\n", cp, ks);
            continue;
        }
        KeySym lo, up; XConvertCase((KeySym)ks, &lo, &up);
        int shift = (ks == up && up != lo && shift_kc);
        if (g_fire) {
            if (shift) XTestFakeKeyEvent(dpy, shift_kc, True, 0);
            XTestFakeKeyEvent(dpy, kc, True, 0);
            XTestFakeKeyEvent(dpy, kc, False, 0);
            if (shift) XTestFakeKeyEvent(dpy, shift_kc, False, 0);
            XFlush(dpy);
        }
        inj++;
        printf("  U+%04X  keysym=0x%06X  keycode=%-3d -> %s%s\n",
               cp, ks, kc, g_fire ? "injected" : "injectable (dry-run)",
               shift ? " (shift)" : "");
    }
    printf("[xtest] RESULT: injectable=%d  uninjectable=%d   [fire=%d]\n", inj, skip, g_fire);
    if (skip) printf("[xtest] raw-key backends CANNOT commit arbitrary UTF-8 — confirmed.\n");
    return 0;
}

static void xtest_fini(void) { if (dpy) XCloseDisplay(dpy); }

static const Backend b = {
    "xtest", "X TEST raw-key injection (X11/XWayland)",
    xtest_init, xtest_commit, xtest_fini
};
const Backend *xtest_backend(void) { return &b; }
