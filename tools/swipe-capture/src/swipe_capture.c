/* swipe_capture — spec-aligned gesture capture (S4.2 / S6.2 / S6.3).
 *
 * A minimal Xlib SwipeSurface: on LMB press it XGrabPointer's the pointer so the
 * glide CONTINUES to be captured even after the cursor leaves the window (S6.2),
 * and it records coordinates NORMALIZED to the keyboard frame but UNCLAMPED, so
 * out-of-bounds overshoot is preserved as signal (S6.3 — clipping, if ever
 * needed, belongs in the decoder adapter). Capture only; decode offline with
 * dictionary_decode.py.
 *
 * Usage: ./swipe_capture [capture.jsonl]
 *   hold LMB on the yellow key, glide (off-keyboard is captured), release
 *   right-click = skip    q / Esc = quit
 */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { char ch; double x, y; } Key;
static const Key KEYS[] = {
    {'q',0.05,0.167},{'w',0.15,0.167},{'e',0.25,0.167},{'r',0.35,0.167},{'t',0.45,0.167},
    {'y',0.55,0.167},{'u',0.65,0.167},{'i',0.75,0.167},{'o',0.85,0.167},{'p',0.95,0.167},
    {'a',0.10,0.500},{'s',0.20,0.500},{'d',0.30,0.500},{'f',0.40,0.500},{'g',0.50,0.500},
    {'h',0.60,0.500},{'j',0.70,0.500},{'k',0.80,0.500},{'l',0.90,0.500},
    {'z',0.20,0.833},{'x',0.30,0.833},{'c',0.40,0.833},{'v',0.50,0.833},{'b',0.60,0.833},
    {'n',0.70,0.833},{'m',0.80,0.833},
};
#define NKEYS ((int)(sizeof(KEYS)/sizeof(KEYS[0])))
static const char *WORDS[] = {
    "computer","hello","world","keyboard","mouse","window","open","glide",
    "water","house","music","plant","story","light","river","table",
    "money","party","stone","north","green","break","place","force"};
#define NWORDS ((int)(sizeof(WORDS)/sizeof(WORDS[0])))
#define WW 960
#define WH 420

static Display *dpy;
static Window win;
static GC gc;
static XFontStruct *font;
static int winx, winy;
static int idx = 0;

static void win_pos(void) {
    int rx, ry; Window child;
    XTranslateCoordinates(dpy, win, DefaultRootWindow(dpy), 0, 0, &rx, &ry, &child);
    winx = rx; winy = ry;
}

static void draw(void) {
    XClearWindow(dpy, win);
    char buf[128];
    snprintf(buf, sizeof buf,
             "Glide: %s   (start on the YELLOW key — glide OFF the keyboard to overshoot; it's captured)",
             WORDS[idx % NWORDS]);
    XSetForeground(dpy, gc, 0x222222);
    XDrawString(dpy, win, gc, 16, 26, buf, (int)strlen(buf));
    XDrawString(dpy, win, gc, 16, WH - 12, "right-click = skip    q / Esc = quit", 35);
    char hc = WORDS[idx % NWORDS][0];
    int kw = 60, kh = 60;
    for (int i = 0; i < NKEYS; i++) {
        int x = (int)(KEYS[i].x * WW), y = (int)(KEYS[i].y * WH);
        XSetForeground(dpy, gc, KEYS[i].ch == hc ? 0xffe08a : 0xffffff);
        XFillRectangle(dpy, win, gc, x - kw/2, y - kh/2, kw, kh);
        XSetForeground(dpy, gc, 0x222222);
        XDrawRectangle(dpy, win, gc, x - kw/2, y - kh/2, kw, kh);
        char s[2] = { (char)(KEYS[i].ch - 32), 0 };
        XDrawString(dpy, win, gc, x - 5, y + 5, s, 1);
    }
    XFlush(dpy);
}

int main(int argc, char **argv) {
    const char *corpus = argc > 1 ? argv[1] : "capture.jsonl";
    dpy = XOpenDisplay(NULL);
    if (!dpy) { perror("XOpenDisplay"); return 1; }
    int scr = DefaultScreen(dpy);
    win = XCreateSimpleWindow(dpy, RootWindow(dpy, scr), 120, 120, WW, WH, 1,
                              BlackPixel(dpy, scr), 0xf0f0f0);
    XSelectInput(dpy, win, ExposureMask | ButtonPressMask | ButtonReleaseMask |
                         PointerMotionMask | ButtonMotionMask | KeyPressMask);
    XStoreName(dpy, win, "OpenGlide swipe-capture (pointer grab, unclamped)");
    XMapWindow(dpy, win);
    gc = XCreateGC(dpy, win, 0, NULL);
    font = XLoadQueryFont(dpy, "fixed");
    if (font) XSetFont(dpy, gc, font->fid);
    XSetBackground(dpy, gc, 0xf0f0f0);
    fprintf(stderr, "READY\n"); fflush(stderr);

    FILE *out = fopen(corpus, "a");
    if (!out) { perror("fopen corpus"); return 1; }

    struct P { double x, y; unsigned t; } *pts = NULL;
    size_t cap = 0, n = 0;
    int swiping = 0;
    Time t0 = 0;
    XEvent e;

    while (1) {
        XNextEvent(dpy, &e);
        if (e.type == Expose && e.xexpose.count == 0) { win_pos(); draw(); }
        else if (e.type == KeyPress) {
            KeySym ks = XLookupKeysym(&e.xkey, 0);
            if (ks == XK_Escape || ks == XK_q) break;
        }
        else if (e.type == ButtonPress && e.xbutton.button == Button3) {
            idx++; draw();  /* skip */
        }
        else if (e.type == ButtonPress && e.xbutton.button == Button1) {
            win_pos();
            XGrabPointer(dpy, win, False,
                         ButtonPressMask | ButtonReleaseMask | PointerMotionMask | ButtonMotionMask,
                         GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
            n = 0; swiping = 1; t0 = e.xbutton.time;
            if (cap < 16) { cap = 16; pts = realloc(pts, cap * sizeof *pts); }
            double nx = ((double)e.xbutton.x_root - winx) / WW;
            double ny = ((double)e.xbutton.y_root - winy) / WH;
            pts[n].x = nx; pts[n].y = ny; pts[n].t = 0; n = 1;
        }
        else if (e.type == MotionNotify && swiping) {
            if (n == cap) { cap *= 2; pts = realloc(pts, cap * sizeof *pts); }
            double nx = ((double)e.xmotion.x_root - winx) / WW;
            double ny = ((double)e.xmotion.y_root - winy) / WH;
            pts[n].x = nx; pts[n].y = ny; pts[n].t = e.xmotion.time - t0;
            n++;
        }
        else if (e.type == ButtonRelease && e.xbutton.button == Button1 && swiping) {
            swiping = 0;
            XUngrabPointer(dpy, CurrentTime);
            if (n == cap) { cap *= 2; pts = realloc(pts, cap * sizeof *pts); }
            pts[n].x = ((double)e.xbutton.x_root - winx) / WW;
            pts[n].y = ((double)e.xbutton.y_root - winy) / WH;
            pts[n].t = e.xbutton.time - t0; n++;

            const char *w = WORDS[idx % NWORDS];
            double xmin = 1e9, xmax = -1e9, ymin = 1e9, ymax = -1e9;
            fprintf(out, "{\"schema_version\":1,\"target\":\"%s\",\"layout_id\":\"en_qwerty_v1\",\"points\":[", w);
            for (size_t i = 0; i < n; i++) {
                fprintf(out, "%s{\"x\":%.4f,\"y\":%.4f,\"time_us\":%u}",
                        i ? "," : "", pts[i].x, pts[i].y, pts[i].t * 1000u);
                if (pts[i].x < xmin) xmin = pts[i].x;
                if (pts[i].x > xmax) xmax = pts[i].x;
                if (pts[i].y < ymin) ymin = pts[i].y;
                if (pts[i].y > ymax) ymax = pts[i].y;
            }
            fprintf(out, "],\"candidates\":[],\"selected\":null}\n");
            fflush(out);
            int over = (xmin < 0 || xmax > 1 || ymin < 0 || ymax > 1);
            fprintf(stderr, "[%s] target=%-9s pts=%-4zu x[%+.3f,%+.3f] y[%+.3f,%+.3f]%s\n",
                    over ? "OVERSHOOT" : "in-bounds", w, n, xmin, xmax, ymin, ymax,
                    over ? "  <- captured outside keyboard" : "");
            idx++;
            draw();
        }
    }
    free(pts);
    fclose(out);
    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
    return 0;
}
