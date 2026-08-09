/* surface-probe (X11/XWayland): maps an override-redirect window that the
 * window manager ignores, then monitors who holds KEYBOARD focus while the
 * probe window is clicked. Goal (spec §17 / ADR-0002): a visible, clickable
 * surface that never steals keyboard focus from the target app.
 *
 * Note: on GNOME Wayland there is no wlr-layer-shell (confirmed: the mutter
 * registry advertises no zwlr_layer_shell_v1), so the Wayland-native answer
 * on GNOME differs. This program tests the X11/XWayland override-redirect
 * mechanism, which is the X11-side answer and the XWayland fallback. */
#include <X11/Xlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void msleep(int ms){
    struct timespec ts = { ms/1000, (long)(ms%1000)*1000000L };
    nanosleep(&ts, NULL);
}

int main(int argc, char **argv){
    int seconds = (argc > 1) ? atoi(argv[1]) : 8;
    Display *d = XOpenDisplay(NULL);
    if(!d){ perror("XOpenDisplay"); return 1; }
    int scr = DefaultScreen(d);
    Window root = RootWindow(d, scr);

    XSetWindowAttributes wa; memset(&wa, 0, sizeof wa);
    wa.override_redirect = True;
    wa.event_mask = ButtonPressMask | ExposureMask;
    wa.background_pixel = WhitePixel(d, scr);
    Window w = XCreateWindow(d, root, 120, 120, 260, 130, 0,
                             CopyFromParent, InputOutput, CopyFromParent,
                             CWOverrideRedirect | CWEventMask | CWBackPixel, &wa);
    XMapWindow(d, w);
    XFlush(d);

    Window focus; int revert;
    XGetInputFocus(d, &focus, &revert);
    fprintf(stderr, "[surface] probe window 0x%lx mapped (override_redirect=True) on %s\n",
            (unsigned long)w, XDisplayString(d));
    fprintf(stderr, "[surface] initial keyboard focus = 0x%lx (revert=%d)\n",
            (unsigned long)focus, revert);
    fprintf(stderr, "[surface] expectation: after clicking the probe, focus stays 0x%lx (NOT 0x%lx)\n",
            (unsigned long)focus, (unsigned long)w);

    for (int t = 0; t < seconds * 2; t++) {
        XGetInputFocus(d, &focus, &revert);
        printf("[surface] t=%4.1fs  focus=0x%-8lx probe=0x%lx  focus_is_probe=%s\n",
               t / 2.0, (unsigned long)focus, (unsigned long)w,
               focus == w ? "YES_STOLE_FOCUS" : "no");
        fflush(stdout);
        while (XPending(d)) {
            XEvent e; XNextEvent(d, &e);
            if (e.type == ButtonPress) {
                XGetInputFocus(d, &focus, &revert);
                fprintf(stderr, "[surface] CLICK on probe at (%d,%d); focus now=0x%lx (probe=0x%lx) %s\n",
                        e.xbutton.x, e.xbutton.y, (unsigned long)focus, (unsigned long)w,
                        focus == w ? "=> STOLE FOCUS" : "=> focus retained");
            }
        }
        XFlush(d);
        msleep(500);
    }
    XDestroyWindow(d, w);
    XCloseDisplay(d);
    return 0;
}
